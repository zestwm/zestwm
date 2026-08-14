/* Shared logging helpers implementation. */
#include "log.hpp"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <string>
#include <string_view>

namespace wm::log {
    /* Build local timestamp prefix as YYYY-MM-DD HH:MM:SS. */
    [[nodiscard]] static std::string timestamp_prefix() {
        const std::time_t now = std::time(nullptr);
        if (now == static_cast<std::time_t>(-1))
            return {};

        std::tm local_tm{};
        if (!localtime_r(&now, &local_tm))
            return {};

        char buf[32];
        if (std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &local_tm) == 0U)
            return {};
        return std::string(buf);
    }

    /* Resolve default log file destination from XDG state path or HOME fallback. */
    [[nodiscard]] static std::string resolve_default_log_path() {
        const char* xdg = std::getenv("XDG_STATE_HOME");
        if (xdg && *xdg)
            return std::string(xdg) + "/zestwm/zestwm.log";

        const char* home = std::getenv("HOME");
        if (home && *home)
            return std::string(home) + "/.local/state/zestwm/zestwm.log";

        return "/tmp/zestwm.log";
    }

    /* Keep active log path process-local and lazily initialized. */
    [[nodiscard]] static std::string& active_log_path() {
        static std::string path;
        if (path.empty())
            path = resolve_default_log_path();
        return path;
    }

    /* Override active log destination for current process lifetime. */
    void set_log_path_override(std::string_view path) {
        active_log_path() = std::string(path);
    }

    /* Append a single line to log file, creating parent directory when missing. */
    void append_log_line(std::string_view line) {
        std::filesystem::path log_path(active_log_path());
        std::error_code       ec;
        std::filesystem::create_directories(log_path.parent_path(), ec);

        FILE* file = std::fopen(active_log_path().c_str(), "a");
        if (!file)
            return;
        const std::string ts = timestamp_prefix();
        if (ts.empty())
            std::fprintf(file, "%.*s\n", static_cast<int>(line.size()), line.data());
        else
            std::fprintf(file, "[%s] %.*s\n", ts.c_str(), static_cast<int>(line.size()), line.data());
        std::fclose(file);
    }

    /* Mirror warning to stderr and persist the same line in log file. */
    void warn_and_log(std::string_view line) {
        const std::string msg(line);
        std::fputs(msg.c_str(), stderr);
        std::fputc('\n', stderr);
        append_log_line(msg);
    }

} /* namespace wm::log */
