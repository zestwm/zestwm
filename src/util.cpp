/* Fatal error reporting for startup paths. */
#include "log.hpp"
#include "util.hpp"

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

void die(const char* fmt, ...) {
    va_list ap;
    va_list ap_log;
    int     saved_errno;
    char    log_buf[1024];
    int     len;

    saved_errno = errno;

    va_start(ap, fmt);
    va_copy(ap_log, ap);
    vfprintf(stderr, fmt, ap);
    len = vsnprintf(log_buf, sizeof(log_buf), fmt, ap_log);
    va_end(ap);
    va_end(ap_log);

    if (len >= 0) {
        std::string msg(log_buf, static_cast<std::size_t>(len < static_cast<int>(sizeof(log_buf) - 1) ? len : sizeof(log_buf) - 1));
        if (fmt[0] && fmt[std::strlen(fmt) - 1] == ':')
            msg += std::string(" ") + std::strerror(saved_errno);
        wm::log::append_log_line(msg);
    }

    if (fmt[0] && fmt[std::strlen(fmt) - 1] == ':')
        fprintf(stderr, " %s", std::strerror(saved_errno));
    fputc('\n', stderr);

    std::exit(1);
}
