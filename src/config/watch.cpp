/* Config file auto-reload: watch loaded paths via inotify (parent directories) or mtime polling.
 * On possible change, raises SIGHUP so the WM reload path runs (see `wm_startup` sighup handler). */
#include "config.hpp"

#include "wm_state.hpp"

#include <csignal>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <sys/inotify.h>
#include <unistd.h>

#include <filesystem>

namespace {

    bool                                         confwatch_enabled = false;
    std::vector<std::filesystem::path>           confwatch_paths;
    std::vector<std::filesystem::file_time_type> confwatch_mtimes;
    bool                                         confwatch_inotify = false;
    int                                          confwatch_fd      = -1;
    std::map<int, std::set<std::string>>         confwatch_wd_names;

} // namespace

void wmconf_watch_init(void) {
    const auto&     files = wmconf_loaded_files();
    std::error_code ec;
    int             fd;

    confwatch_enabled = false;
    confwatch_inotify = false;
    confwatch_paths.clear();
    confwatch_mtimes.clear();
    confwatch_wd_names.clear();
    if (confwatch_fd >= 0) {
        close(confwatch_fd);
        confwatch_fd = -1;
    }
    if (files.empty())
        return;
    for (const auto& s : files) {
        std::filesystem::path p = std::filesystem::weakly_canonical(std::filesystem::path(s), ec);
        if (ec)
            p = std::filesystem::path(s);
        confwatch_paths.push_back(p);
        confwatch_mtimes.push_back(std::filesystem::last_write_time(p, ec));
        if (ec)
            confwatch_mtimes.back() = std::filesystem::file_time_type::min();
    }
    fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (fd >= 0) {
        for (const auto& p : confwatch_paths) {
            std::filesystem::path parent = p.parent_path();
            int                   wd     = inotify_add_watch(fd, parent.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE_SELF | IN_MOVE_SELF);
            if (wd >= 0)
                confwatch_wd_names[wd].insert(p.filename().string());
        }
        if (!confwatch_wd_names.empty()) {
            confwatch_fd      = fd;
            confwatch_inotify = true;
            confwatch_enabled = true;
            return;
        }
        close(fd);
    }
    confwatch_enabled = !confwatch_paths.empty();
}

void wmconf_watch_maybe_reload(void) {
    std::error_code                 ec;
    std::filesystem::file_time_type now;
    char                            buf[4096];
    ssize_t                         nread;
    bool                            changed = false;

    if (!confwatch_enabled || !running)
        return;
    if (confwatch_inotify) {
        if (confwatch_fd < 0 || confwatch_wd_names.empty())
            return;
        for (;;) {
            nread = read(confwatch_fd, buf, sizeof(buf));
            if (nread <= 0)
                break;
            for (ssize_t off = 0; off < nread;) {
                const auto* ev = reinterpret_cast<const struct inotify_event*>(buf + off);
                if ((ev->mask & (IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE)) && ev->len > 0) {
                    auto it = confwatch_wd_names.find(ev->wd);
                    if (it != confwatch_wd_names.end() && it->second.count(ev->name))
                        changed = true;
                }
                if (ev->mask & (IN_DELETE_SELF | IN_MOVE_SELF))
                    changed = true;
                off += static_cast<ssize_t>(sizeof(struct inotify_event)) + ev->len;
            }
        }
        if (changed)
            raise(SIGHUP);
        return;
    }
    for (size_t i = 0; i < confwatch_paths.size(); i++) {
        now = std::filesystem::last_write_time(confwatch_paths[i], ec);
        if (ec)
            continue;
        if (now != confwatch_mtimes[i]) {
            confwatch_mtimes[i] = now;
            raise(SIGHUP);
            return;
        }
    }
}

int wmconf_watch_inotify_fd(void) noexcept {
    return confwatch_inotify ? confwatch_fd : -1;
}

int wmconf_watch_poll_timeout_ms(void) noexcept {
    return confwatch_inotify ? -1 : 1000;
}

void wmconf_watch_shutdown(void) noexcept {
    if (confwatch_fd >= 0) {
        close(confwatch_fd);
        confwatch_fd = -1;
        confwatch_wd_names.clear();
    }
}
