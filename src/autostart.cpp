/* XDG autostart execution helpers for system/user .desktop directories. */
#include "autostart.hpp"
#include "sys/spawn.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace {

    /* Trim ASCII whitespace around config value content. */
    [[nodiscard]] std::string trim_ascii(std::string_view value) noexcept {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos)
            return {};
        const auto last = value.find_last_not_of(" \t\r\n");
        return std::string(value.substr(first, last - first + 1));
    }

    /* Return right-hand side for "Key=value" line when key matches exactly. */
    [[nodiscard]] std::optional<std::string> parse_key_value(std::string_view line, std::string_view key) noexcept {
        if (!line.starts_with(key))
            return std::nullopt;
        if (line.size() <= key.size() || line[key.size()] != '=')
            return std::nullopt;
        return trim_ascii(line.substr(key.size() + 1));
    }

    /* Split semicolon-delimited desktop list and drop empty tokens. */
    [[nodiscard]] std::vector<std::string> split_semicolon_list(std::string_view raw) {
        std::vector<std::string> out;
        std::size_t              start = 0;
        while (start <= raw.size()) {
            const std::size_t end   = raw.find(';', start);
            const std::size_t slice = (end == std::string_view::npos) ? raw.size() : end;
            const auto        token = trim_ascii(raw.substr(start, slice - start));
            if (!token.empty())
                out.push_back(token);
            if (end == std::string_view::npos)
                break;
            start = end + 1;
        }
        return out;
    }

    /* Split XDG_CURRENT_DESKTOP list (':'-delimited) to comparable names. */
    [[nodiscard]] std::vector<std::string> parse_current_desktops() {
        std::vector<std::string> desktops;
        const char*              value = std::getenv("XDG_CURRENT_DESKTOP");
        if (!value || !*value)
            return desktops;
        std::string_view raw{value};
        std::size_t      start = 0;
        while (start <= raw.size()) {
            const std::size_t end   = raw.find(':', start);
            const std::size_t slice = (end == std::string_view::npos) ? raw.size() : end;
            const auto        token = trim_ascii(raw.substr(start, slice - start));
            if (!token.empty())
                desktops.push_back(token);
            if (end == std::string_view::npos)
                break;
            start = end + 1;
        }
        return desktops;
    }

    /* Return true when one desktop token exists in both lists. */
    [[nodiscard]] bool has_desktop_overlap(const std::vector<std::string>& current, const std::vector<std::string>& allowed) noexcept {
        for (const auto& cur : current) {
            for (const auto& want : allowed) {
                if (cur == want)
                    return true;
            }
        }
        return false;
    }

    /* Check TryExec candidate path (absolute or PATH lookup) is executable. */
    [[nodiscard]] bool can_tryexec_run(std::string_view tryexec) {
        const std::string value = trim_ascii(tryexec);
        if (value.empty())
            return false;
        if (value.find('/') != std::string::npos)
            return ::access(value.c_str(), X_OK) == 0;
        if (const char* path_env = std::getenv("PATH"); path_env && *path_env) {
            std::string_view path_list{path_env};
            std::size_t      start = 0;
            while (start <= path_list.size()) {
                const std::size_t end   = path_list.find(':', start);
                const std::size_t slice = (end == std::string_view::npos) ? path_list.size() : end;
                const std::string dir(path_list.substr(start, slice - start));
                const auto        full = std::filesystem::path(dir) / value;
                if (::access(full.c_str(), X_OK) == 0)
                    return true;
                if (end == std::string_view::npos)
                    break;
                start = end + 1;
            }
        }
        return false;
    }

    /* Remove freedesktop Exec field codes (%f/%u/%F/%U/%i/%c/%k) and quotes. */
    [[nodiscard]] std::string sanitize_exec_command(std::string_view raw_exec) noexcept {
        std::string out;
        out.reserve(raw_exec.size());
        for (std::size_t i = 0; i < raw_exec.size(); ++i) {
            const char ch = raw_exec[i];
            if (ch == '"' || ch == '\'')
                continue;
            if (ch != '%') {
                out.push_back(ch);
                continue;
            }
            if ((i + 1) >= raw_exec.size())
                continue;
            const char code = raw_exec[i + 1];
            if (code == 'f' || code == 'F' || code == 'u' || code == 'U' || code == 'i' || code == 'c' || code == 'k') {
                ++i;
                continue;
            }
            if (code == '%') {
                out.push_back('%');
                ++i;
                continue;
            }
        }
        return trim_ascii(out);
    }

    /* Parse desktop file for enable flags and last Exec command candidate. */
    [[nodiscard]] std::optional<std::string> parse_desktop_exec(const std::filesystem::path& desktop_path, const std::vector<std::string>& current_desktops) {
        std::ifstream in(desktop_path);
        if (!in.is_open())
            return std::nullopt;

        std::optional<std::string> exec_line;
        bool                       in_desktop_entry = false;
        std::string                line;
        while (std::getline(in, line)) {
            const std::string      trimmed_line = trim_ascii(line);
            const std::string_view view{trimmed_line};
            if (view.empty() || view.starts_with('#'))
                continue;
            if (view.starts_with('[') && view.ends_with(']')) {
                in_desktop_entry = (view == "[Desktop Entry]");
                continue;
            }
            if (!in_desktop_entry)
                continue;
            if (const auto hidden = parse_key_value(view, "Hidden"); hidden && *hidden == "true")
                return std::nullopt;
            if (const auto no_display = parse_key_value(view, "NoDisplay"); no_display && *no_display == "true")
                return std::nullopt;
            if (const auto enabled = parse_key_value(view, "X-GNOME-Autostart-enabled"); enabled && *enabled == "false")
                return std::nullopt;
            if (const auto tryexec = parse_key_value(view, "TryExec"); tryexec && !can_tryexec_run(*tryexec))
                return std::nullopt;
            if (const auto only_show_in = parse_key_value(view, "OnlyShowIn"); only_show_in) {
                const auto tokens = split_semicolon_list(*only_show_in);
                if (!tokens.empty() && !has_desktop_overlap(current_desktops, tokens))
                    return std::nullopt;
            }
            if (const auto not_show_in = parse_key_value(view, "NotShowIn"); not_show_in) {
                const auto tokens = split_semicolon_list(*not_show_in);
                if (!tokens.empty() && has_desktop_overlap(current_desktops, tokens))
                    return std::nullopt;
            }
            if (const auto exec = parse_key_value(view, "Exec"))
                exec_line = *exec;
        }

        if (!exec_line || exec_line->empty())
            return std::nullopt;
        const std::string cmd = sanitize_exec_command(*exec_line);
        if (cmd.empty())
            return std::nullopt;
        return cmd;
    }

    /* Collect .desktop files from one directory keyed by basename. */
    void collect_desktop_files(const std::filesystem::path& dir, std::map<std::string, std::filesystem::path, std::less<>>& files_by_name) {
        if (!std::filesystem::is_directory(dir))
            return;

        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (!entry.is_regular_file())
                continue;
            if (entry.path().extension() != ".desktop")
                continue;
            files_by_name[entry.path().filename().string()] = entry.path();
        }
    }

    /* Run one command via /bin/sh -c in detached child process. */
    void spawn_command_detached(xcb_connection_t* xc, std::string_view command) noexcept {
        wm::sys::spawn_detached_shell(xc, command);
    }

    /* Execute autostart scan in isolated worker process after startup grace sleep. */
    void run_xdg_autostart_worker(xcb_connection_t* xc) {
        ::sleep(2);

        std::map<std::string, std::filesystem::path, std::less<>> desktop_files;
        collect_desktop_files("/etc/xdg/autostart", desktop_files);
        if (const char* home = std::getenv("HOME"); home && *home)
            collect_desktop_files(std::filesystem::path(home) / ".config" / "autostart", desktop_files);

        const auto current_desktops = parse_current_desktops();
        for (const auto& [_, desktop_file] : desktop_files) {
            const auto cmd = parse_desktop_exec(desktop_file, current_desktops);
            if (!cmd)
                continue;
            spawn_command_detached(xc, *cmd);
        }
    }

} // namespace

void run_xdg_autostart(xcb_connection_t* xc) {
    const pid_t pid = ::fork();
    if (pid < 0) [[unlikely]]
        return;
    if (pid != 0)
        return;

    wm::sys::prepare_detached_child(xc);
    run_xdg_autostart_worker(nullptr);
    _exit(0);
}
