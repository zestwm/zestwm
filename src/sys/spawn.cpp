/*
 * Detached spawn implementation: fork, session detach, then shell or argv exec.
 */
#include "sys/spawn.hpp"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <unistd.h>

#include <vector>

namespace wm::sys {
    namespace {
        /* Best-effort close of the WM XCB socket so children do not share it. */
        void close_xcb_fd(xcb_connection_t* xc) noexcept {
            if (!xc)
                return;
            const int fd = xcb_get_file_descriptor(xc);
            if (fd >= 0)
                static_cast<void>(::close(fd));
        }
    } // namespace

    void prepare_detached_child(xcb_connection_t* xc) noexcept {
        close_xcb_fd(xc);
        static_cast<void>(::setsid());
        struct sigaction sa{};
        sigemptyset(&sa.sa_mask);
        sa.sa_flags   = 0;
        sa.sa_handler = SIG_DFL;
        static_cast<void>(::sigaction(SIGCHLD, &sa, nullptr));
    }

    void spawn_detached_shell(xcb_connection_t* xc, std::string_view cmd) noexcept {
        if (cmd.empty()) [[unlikely]]
            return;
        const pid_t pid = ::fork();
        if (pid < 0) [[unlikely]]
            return;
        if (pid != 0)
            return;

        prepare_detached_child(xc);
        const std::string owned{cmd};
        static_cast<void>(::execl("/bin/sh", "sh", "-c", owned.c_str(), static_cast<char*>(nullptr)));
        ::dprintf(2, "zestwm: spawn exec failed for '%s': %s\n", owned.c_str(), std::strerror(errno));
        _exit(127);
    }

    void spawn_detached_argv(xcb_connection_t* xc, const std::vector<std::string>& args) noexcept {
        if (args.empty() || args[0].empty()) [[unlikely]]
            return;
        const pid_t pid = ::fork();
        if (pid < 0) [[unlikely]]
            return;
        if (pid != 0)
            return;

        prepare_detached_child(xc);
        /* Project owned strings into a null-terminated argv for execvp. */
        std::vector<char*> argv;
        argv.reserve(args.size() + 1U);
        for (const std::string& s : args)
            argv.push_back(const_cast<char*>(s.c_str()));
        argv.push_back(nullptr);
        static_cast<void>(::execvp(argv[0], argv.data()));
        ::dprintf(2, "zestwm: execvp '%s' failed: %s\n", argv[0], std::strerror(errno));
        _exit(127);
    }

} // namespace wm::sys
