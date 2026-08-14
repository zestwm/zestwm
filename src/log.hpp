/* Shared logging helpers for stderr and persistent file output. */
#pragma once

#include <string_view>

namespace wm::log {

    /* Set explicit log file path override for subsequent log writes. */
    void set_log_path_override(std::string_view path);

    /* Append one log line to persistent log file destination. */
    void append_log_line(std::string_view line);

    /* Emit warning line to stderr and persistent log destination. */
    void warn_and_log(std::string_view line);

} /* namespace wm::log */
