/* Section block parser implementation for misc/general/dwindle blocks. */
#include "config/parse/sections.hpp"

#include "config.hpp"
#include "config/parse/common.hpp"
#include "config/parse/expand.hpp"
#include "config/parse/general.hpp"
#include "config/parse/utils.hpp"
#include "config/parse/values.hpp"
#include "log.hpp"

#include <array>
#include <sstream>
#include <string>
#include <utility>
#include <string_view>

namespace wm::config::parse::sections {
    namespace {
        /* Common handler signature for table-driven section assignments.
         *
         * Inputs:
         * - `val`: already-trimmed right-hand side token.
         * - `path`/`lineno`: source context for parse diagnostics.
         */
        using ValueHandlerFn = void (*)(std::string_view val, const char* path, unsigned lineno);

        /* Key -> handler mapping entry used by section dispatch tables. */
        struct DispatchEntry {
            std::string_view key;
            ValueHandlerFn   handler;
        };

        /* Expand one config value after optional quote stripping.
         *
         * This keeps expand semantics consistent across all section handlers.
         */
        [[nodiscard]] static std::string expand_quoted_value(std::string_view raw) {
            return expand::expand_all(strip_outer_quotes(std::string(raw)), g_config.conf_vars);
        }

        /* Build standardized unknown-key error details for strict sections. */
        [[nodiscard]] static std::string make_unknown_key_detail(std::string_view section, std::string_view key) {
            std::string detail;
            detail.reserve(24U + section.size() + key.size());
            detail = "unknown ";
            detail.append(section);
            detail += " key '";
            detail.append(key);
            detail += "'";
            return detail;
        }

        static void handle_misc_font_family(std::string_view val, const char*, unsigned) {
            g_config.wm_misc.font_family = expand_quoted_value(val);
        }
        static void handle_misc_focus_on_activate(std::string_view val, const char* path, unsigned lineno) {
            g_config.wm_misc.focus_on_activate = common::expect_or_throw(values::parse_bool_val(val), path, lineno);
        }
        static void handle_misc_mouse_move_focuses_monitor(std::string_view val, const char* path, unsigned lineno) {
            g_config.wm_misc.mouse_move_focuses_monitor = common::expect_or_throw(values::parse_bool_val(val), path, lineno);
        }
        static void handle_misc_background_color(std::string_view val, const char*, unsigned) {
            g_config.wm_misc.background_color = expand_quoted_value(val);
        }

        /* Resolve `misc` key handlers.
         *
         * Policy:
         * - Returns nullptr for unknown keys; caller throws (strict section).
         */
        [[nodiscard]] static ValueHandlerFn find_misc_handler(std::string_view key) noexcept {
            static constexpr std::array<DispatchEntry, 4> kMiscDispatch = {{
                {"font_family", handle_misc_font_family},
                {"focus_on_activate", handle_misc_focus_on_activate},
                {"mouse_move_focuses_monitor", handle_misc_mouse_move_focuses_monitor},
                {"background_color", handle_misc_background_color},
            }};
            for (const auto& entry : kMiscDispatch) {
                if (entry.key == key)
                    return entry.handler;
            }
            return nullptr;
        }

        static void handle_snap_enabled(std::string_view val, const char* path, unsigned lineno) {
            g_config.snap_enabled = common::expect_or_throw(values::parse_bool_val(val), path, lineno);
        }
        static void handle_snap_window_gap(std::string_view val, const char* path, unsigned lineno) {
            g_config.snap = common::expect_or_throw(values::parse_uint_val(val), path, lineno);
        }
        static void handle_snap_monitor_gap(std::string_view val, const char* path, unsigned lineno) {
            g_config.snap = common::expect_or_throw(values::parse_uint_val(val), path, lineno);
        }

        /* Resolve nested `general:snap` key handlers.
         *
         * Policy:
         * - Returns nullptr for unknown keys; caller may warn-ignore for compat keys.
         */
        [[nodiscard]] static ValueHandlerFn find_snap_handler(std::string_view key) noexcept {
            static constexpr std::array<DispatchEntry, 3> kSnapDispatch = {{
                {"enabled", handle_snap_enabled},
                {"window_gap", handle_snap_window_gap},
                {"monitor_gap", handle_snap_monitor_gap},
            }};
            for (const auto& entry : kSnapDispatch) {
                if (entry.key == key)
                    return entry.handler;
            }
            return nullptr;
        }

        static void handle_general_border_size(std::string_view val, const char* path, unsigned lineno) {
            g_config.borderpx = common::expect_or_throw(values::parse_uint_val(val), path, lineno);
        }
        static void handle_general_gaps_in(std::string_view val, const char* path, unsigned lineno) {
            g_config.gaps_in = common::expect_or_throw(values::parse_uint_val(val), path, lineno);
        }
        static void handle_general_resize_hints(std::string_view val, const char* path, unsigned lineno) {
            g_config.resizehints = common::expect_or_throw(values::parse_bool_val(val), path, lineno);
        }
        static void handle_general_col_active_border(std::string_view val, const char*, unsigned) {
            g_config.active_border_color = expand_quoted_value(val);
        }
        static void handle_general_col_inactive_border(std::string_view val, const char*, unsigned) {
            g_config.inactive_border_color = expand_quoted_value(val);
        }
        static void handle_general_layout(std::string_view val, const char* path, unsigned lineno) {
            std::string        layout_name;
            std::istringstream iss(strip_outer_quotes(std::string(val)));
            iss >> layout_name;
            if (layout_name.empty()) [[unlikely]]
                common::throw_parse_error(path, lineno, "general.layout needs a layout name");
            g_config.startup_layout = general::layout_by_name(layout_name);
        }

        /* Resolve top-level `general` key handlers.
         *
         * Policy:
         * - Returns nullptr for unknown keys; caller applies warn-ignore compatibility path.
         */
        [[nodiscard]] static ValueHandlerFn find_general_handler(std::string_view key) noexcept {
            static constexpr std::array<DispatchEntry, 6> kGeneralDispatch = {{
                {"border_size", handle_general_border_size},
                {"gaps_in", handle_general_gaps_in},
                {"resize_hints", handle_general_resize_hints},
                {"col.active_border", handle_general_col_active_border},
                {"col.inactive_border", handle_general_col_inactive_border},
                {"layout", handle_general_layout},
            }};
            for (const auto& entry : kGeneralDispatch) {
                if (entry.key == key)
                    return entry.handler;
            }
            return nullptr;
        }

        static void handle_dwindle_preserve_split(std::string_view val, const char* path, unsigned lineno) {
            g_config.dwindle_preserve_split = common::expect_or_throw(values::parse_bool_val(val), path, lineno);
        }
        static void handle_dwindle_force_split(std::string_view val, const char* path, unsigned lineno) {
            g_config.dwindle_force_split = common::expect_or_throw(values::parse_int_val(val), path, lineno);
        }
        static void handle_dwindle_use_active_for_splits(std::string_view val, const char* path, unsigned lineno) {
            g_config.dwindle_use_active_for_splits = common::expect_or_throw(values::parse_bool_val(val), path, lineno);
        }
        static void handle_dwindle_default_split_ratio(std::string_view val, const char* path, unsigned lineno) {
            g_config.dwindle_default_split_ratio = common::expect_or_throw(values::parse_float_val(val), path, lineno);
        }
        static void handle_dwindle_split_width_multiplier(std::string_view val, const char* path, unsigned lineno) {
            g_config.dwindle_split_width_multiplier = common::expect_or_throw(values::parse_float_val(val), path, lineno);
        }

        /* Resolve `dwindle` key handlers.
         *
         * Policy:
         * - Returns nullptr for unknown keys; caller throws (strict section).
         */
        [[nodiscard]] static ValueHandlerFn find_dwindle_handler(std::string_view key) noexcept {
            static constexpr std::array<DispatchEntry, 5> kDwindleDispatch = {{
                {"preserve_split", handle_dwindle_preserve_split},
                {"force_split", handle_dwindle_force_split},
                {"use_active_for_splits", handle_dwindle_use_active_for_splits},
                {"default_split_ratio", handle_dwindle_default_split_ratio},
                {"split_width_multiplier", handle_dwindle_split_width_multiplier},
            }};
            for (const auto& entry : kDwindleDispatch) {
                if (entry.key == key)
                    return entry.handler;
            }
            return nullptr;
        }
    } // namespace

    /* Emit warning line with parser source context through shared logger. */
    static void warn_section(std::string_view path, unsigned lineno, std::string_view section, std::string_view key) {
        std::string msg;
        msg.reserve(96U + path.size() + section.size() + key.size());
        msg = "zestwm: ";
        msg.append(path.empty() ? std::string_view("?") : path);
        msg += ":";
        msg += std::to_string(lineno);
        msg += ": ";
        msg.append(section);
        msg += " key '";
        msg.append(key);
        msg += "' ignored on X11";
        wm::log::warn_and_log(msg);
    }

    /* Parse `misc { ... }` block.
     *
     * Structural contract:
     * - Nested `misc {` is rejected.
     * - Function returns only when the matching closing brace is consumed.
     *
     * Semantic contract:
     * - Known keys are validated then committed to `wm_misc`.
     * - Unknown keys are hard errors (strict block).
     */
    void parse_misc_block(std::istream& in, unsigned int& lineno, const char* path) {
        int         nest = 1;
        std::string line;
        while (std::getline(in, line)) {
            ++lineno;
            line = strip_line_comment(std::move(line));
            if (line.empty())
                continue;
            if (line == "misc {") [[unlikely]]
                common::throw_parse_error(path, lineno, "nested misc { is not allowed");
            if (line == "}") {
                --nest;
                if (nest == 0)
                    return;
                if (nest < 0) [[unlikely]]
                    common::throw_parse_error(path, lineno, "unexpected '}'");
                continue;
            }
            auto [key, val] = common::parse_key_value_or_throw(line, path, lineno, "misc");
            if (ValueHandlerFn handler = find_misc_handler(key); handler) {
                handler(val, path, lineno);
            } else [[unlikely]] {
                common::throw_parse_error(path, lineno, make_unknown_key_detail("misc", key));
            }
        }
        common::throw_parse_error(path, "unterminated misc { block");
    }

    /* Parse `general { ... }` including optional nested `snap { ... }`.
     *
     * Structural contract:
     * - `snap {` is valid only directly under `general {`.
     * - Mismatched braces and unterminated nested blocks throw.
     *
     * Compatibility contract:
     * - Known keys are applied immediately.
     * - Unsupported cross-backend keys are intentionally warn-ignore to keep
     *   shared configs portable on X11.
     */
    void parse_general_block(std::istream& in, unsigned int& lineno, const char* path) {
        enum class State : unsigned char {
            general,
            snap,
        };
        State       state = State::general;
        int         nest  = 1;
        std::string line;
        while (std::getline(in, line)) {
            ++lineno;
            line = strip_line_comment(std::move(line));
            if (line.empty())
                continue;
            if (line == "snap {") {
                if (state != State::general || nest != 1) [[unlikely]]
                    common::throw_parse_error(path, lineno, "snap { only directly inside general { }");
                state = State::snap;
                ++nest;
                continue;
            }
            if (line == "}") {
                --nest;
                if (nest < 0) [[unlikely]]
                    common::throw_parse_error(path, lineno, "unexpected '}'");
                if (nest == 0) {
                    if (state != State::general) [[unlikely]]
                        common::throw_parse_error(path, lineno, "unclosed snap {");
                    return;
                }
                if (state == State::snap && nest == 1) {
                    state = State::general;
                    continue;
                }
                common::throw_parse_error(path, lineno, "unexpected '}'");
            }
            auto [key, val] = common::parse_key_value_or_throw(line, path, lineno, "general");

            if (state == State::snap) {
                if (ValueHandlerFn handler = find_snap_handler(key); handler) {
                    handler(val, path, lineno);
                } else if (key == "border_overlap" || key == "respect_gaps") {
                    /* Legacy/X11 parser compatibility keys: accepted but currently ignored. */
                } else [[unlikely]] {
                    warn_section(path ? path : "?", lineno, "general:snap", key);
                }
                continue;
            }
            if (ValueHandlerFn handler = find_general_handler(key); handler) {
                handler(val, path, lineno);
            } else [[unlikely]] {
                warn_section(path ? path : "?", lineno, "general", key);
            }
        }
        common::throw_parse_error(path, "unterminated general { block");
    }

    /* Parse `dwindle { ... }` block.
     *
     * Structural contract:
     * - Nested `dwindle {` is rejected.
     * - Returns only after consuming the matching closing brace.
     *
     * Semantic contract:
     * - Known keys update dwindle defaults after typed validation.
     * - Unknown keys are hard errors (strict block).
     */
    void parse_dwindle_block(std::istream& in, unsigned int& lineno, const char* path) {
        int         nest = 1;
        std::string line;
        while (std::getline(in, line)) {
            ++lineno;
            line = strip_line_comment(std::move(line));
            if (line.empty())
                continue;
            if (line == "dwindle {") [[unlikely]]
                common::throw_parse_error(path, lineno, "nested dwindle { is not allowed");
            if (line == "}") {
                --nest;
                if (nest == 0)
                    return;
                if (nest < 0) [[unlikely]]
                    common::throw_parse_error(path, lineno, "unexpected '}'");
                continue;
            }
            auto [key, val] = common::parse_key_value_or_throw(line, path, lineno, "dwindle");
            if (ValueHandlerFn handler = find_dwindle_handler(key); handler) {
                handler(val, path, lineno);
            } else [[unlikely]] {
                common::throw_parse_error(path, lineno, make_unknown_key_detail("dwindle", key));
            }
        }
        common::throw_parse_error(path, "unterminated dwindle { block");
    }

} /* namespace wm::config::parse::sections */
