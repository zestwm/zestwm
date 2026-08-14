/* Runtime config loading/orchestration implementation for zestwm.conf. */
#include "config.hpp"
#include "config/group_focus_policy.hpp"
#include "workspace_id.hpp"
#include "workspace_registry.hpp"
#include "config/env.hpp"
#include "config/parse/core.hpp"
#include "default_config.hpp"
#include "log.hpp"

#include <array>
#include <cstdlib>

#include <cstring>
#include <filesystem>
#include <format>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

ConfigState                            g_config;

static std::string                     wmconf_error_notice;
static unsigned int                    wmconf_error_count;
static std::string                     wmconf_loaded_cfgpath;
static std::vector<std::string>        wmconf_loaded_file_list;
static std::unordered_set<std::string> wmconf_loaded_file_set;

/* ========================================================================
 *  ERROR HANDLING & LOGGING (C++23 std::format + exceptions)
 * ======================================================================== */

template <typename T>
/* Convert placeholder argument to string for lightweight brace-substitution formatter. */
static std::string conf_die_arg_to_string(T&& value) {
    std::ostringstream oss;
    oss << std::forward<T>(value);
    return oss.str();
}

/* Append remaining format tail when no replacement arguments are left. */
static void conf_die_compose(std::string& out, std::string_view fmt) {
    out.append(fmt);
}

template <typename T, typename... Rest>
/* Expand '{}' placeholders recursively into final error message text. */
static void conf_die_compose(std::string& out, std::string_view fmt, T&& value, Rest&&... rest) {
    const size_t pos = fmt.find("{}");
    if (pos == std::string_view::npos) {
        out.append(fmt);
        return;
    }
    out.append(fmt.substr(0, pos));
    out.append(conf_die_arg_to_string(std::forward<T>(value)));
    conf_die_compose(out, fmt.substr(pos + 2), std::forward<Rest>(rest)...);
}

template <typename... Args>
/* Build fatal config error message, persist it, and raise as runtime_error. */
[[noreturn]] static void conf_die(std::string_view fmt, Args&&... args) {
    std::string msg;
    conf_die_compose(msg, fmt, std::forward<Args>(args)...);
    wm::log::append_log_line(msg);
    throw std::runtime_error(msg);
}

using wm::config::env::flush_envd_exports;
using wm::config::parse::core::CoreContext;
using wm::config::parse::core::parse_config_file;
using wm::config::parse::core::parse_config_stream;

/* Emit parse warning to stderr and persistent config log. */
template <typename... Args>
static void conf_bind_warn(std::string_view path, unsigned lineno, std::format_string<Args...> fmt, Args&&... args) {
    const std::string_view source = path.empty() ? std::string_view("?") : path;
    const std::string      detail = std::format(fmt, std::forward<Args>(args)...);
    const std::string      msg    = std::format("zestwm: {}:{}: {}", source, lineno, detail);
    wm::log::warn_and_log(msg);
}

/* Resolve effective config file path from XDG config dir or HOME fallback. */
static std::string default_config_path(void) {
    const char* xdg = ::getenv("XDG_CONFIG_HOME");
    std::string base;
    if (xdg && *xdg)
        base = xdg;
    else {
        const char* h = ::getenv("HOME");
        if (!h)
            conf_die("zestwm: HOME unset, cannot find config");
        base = std::string(h) + "/.config";
    }
    return base + "/zestwm/zestwm.conf";
}

/* Load embedded defaults and user config file, then validate required defaults. */
void wmconf_load(const char* path) {
    wmconf_free();
    const std::string cfgpath = path ? path : default_config_path();
    wmconf_loaded_cfgpath     = cfgpath;
    wmconf_loaded_file_list.clear();
    wmconf_loaded_file_set.clear();
    std::vector<std::string> include_stack;
    wmconf_error_notice.clear();
    wmconf_error_count   = 0;
    CoreContext core_ctx = {
        g_config, wmconf_loaded_file_list, wmconf_loaded_file_set, wmconf_error_notice, wmconf_error_count,
    };
    try {
        std::error_code ec;
        const bool      user_cfg_exists = std::filesystem::exists(std::filesystem::path(cfgpath), ec);
        if (user_cfg_exists) {
            parse_config_file(core_ctx, std::filesystem::path(cfgpath), 0, include_stack);
        } else {
            std::string        embedded_cfg(reinterpret_cast<const char*>(EXAMPLE_CONFIG_BYTES), EXAMPLE_CONFIG_SIZE);
            std::istringstream embedded{embedded_cfg};
            parse_config_stream(core_ctx, embedded, std::filesystem::path("examples"), "<embedded-default>", 0, include_stack);
            wmconf_loaded_file_list.push_back(cfgpath);
        }
        flush_envd_exports();
    } catch (const std::exception& e) {
        ++wmconf_error_count;
        if (wmconf_error_notice.empty())
            wmconf_error_notice = e.what();
        conf_bind_warn(cfgpath.c_str(), 0, "{}", e.what());
    }

    if (g_config.colors.size() < 1)
        g_config.colors.resize(1);
    if (g_config.colors[0][0].empty() || g_config.colors[0][1].empty() || g_config.colors[0][2].empty())
        g_config.colors[0] = {"#bbbbbb", "#222222", "#444444"};

    if (g_config.colors.size() < 2)
        g_config.colors.resize(2);
    if (g_config.colors[1][0].empty() || g_config.colors[1][1].empty() || g_config.colors[1][2].empty())
        g_config.colors[1] = {"#eeeeee", "#005577", "#005577"};
    if (g_config.layouts.empty()) {
        g_config.layouts.push_back(Layout{"T", tree});
        g_config.layouts.push_back(Layout{"M", monocle});
        conf_bind_warn(cfgpath.c_str(), 0, "no valid g_config.layouts found; using fallback tree/monocle");
    } else if (g_config.layouts.size() == 1) {
        g_config.layouts.push_back(Layout{"M", monocle});
    }

    if (workspace_registry_count() == 0U)
        workspace_registry_ensure_id(kWorkspaceIdMin);
}

/* Return first parse error, optionally annotated with count of additional errors. */
const char* wmconf_last_error(void) {
    static std::string msg;

    if (!wmconf_error_notice.empty() && wmconf_error_count > 1) {
        msg = wmconf_error_notice + " (+" + std::to_string(wmconf_error_count - 1) + " more)";
        return msg.c_str();
    }
    return wmconf_error_notice.empty() ? nullptr : wmconf_error_notice.c_str();
}

/* Return canonical list of config files loaded (main file plus includes). */
const std::vector<std::string>& wmconf_loaded_files(void) noexcept {
    return wmconf_loaded_file_list;
}
