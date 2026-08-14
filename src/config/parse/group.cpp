/* Config parser implementation for group/groupbar section. */
#include "config/parse/group.hpp"

#include "config.hpp"
#include "config/group_focus_policy.hpp"
#include "config/parse/common.hpp"
#include "config/parse/expand.hpp"
#include "config/parse/utils.hpp"
#include "config/parse/values.hpp"
#include "log.hpp"

#include <array>
#include <cctype>
#include <utility>
#include <string>
#include <string_view>

namespace wm::config::parse {
    namespace {
        /* Parsing mode for one `group { ... }` block.
         *
         * `group`:
         * - Top-level keys that configure grouped-window behavior and border policy.
         *
         * `groupbar`:
         * - Nested subsection that controls groupbar rendering/layout settings.
         */
        enum class GroupState : unsigned char {
            group,
            groupbar,
        };

        /* Expand one quoted/raw value through shared config-variable expansion.
         *
         * Notes:
         * - Keeps `vars` explicit to avoid hidden global coupling in tests.
         * - Preserves current parser semantics (`strip_outer_quotes` + `expand_all`).
         */
        [[nodiscard]] static std::string expand_quoted_value(std::string_view raw, const ConfVars& vars) {
            return expand::expand_all(strip_outer_quotes(trim(raw)), vars);
        }

        /* Parse accepted aliases for groupbar edge placement.
         *
         * Accepted tokens:
         * - long form: top|left|right|bottom
         * - short form: t|l|r|b
         *
         * Return domain:
         * - 0=top, 1=left, 2=right, 3=bottom (legacy runtime encoding)
         */
        [[nodiscard]] static int parse_groupbar_position(std::string_view raw_value, const char* path, unsigned lineno) {
            std::string value{raw_value};
            for (char& ch : value)
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

            if (value == "top" || value == "t")
                return 0;
            if (value == "left" || value == "l")
                return 1;
            if (value == "right" || value == "r")
                return 2;
            if (value == "bottom" || value == "b")
                return 3;

            common::throw_parse_error(path, lineno, "groupbar position must be top|left|right|bottom or t|l|r|b");
        }

        using ValueHandlerFn = void (*)(std::string_view val, const char* path, unsigned lineno);

        struct DispatchEntry {
            std::string_view key;
            ValueHandlerFn   handler;
        };

        /* ---- groupbar key handlers -------------------------------------------------
         * Each handler parses one known key and writes directly into runtime config
         * globals for the active parser process.
         * -------------------------------------------------------------------------- */
        static void handle_groupbar_enabled(std::string_view val, const char* path, unsigned lineno) {
            g_config.groupbar_enabled = common::expect_or_throw(values::parse_bool_val(val), path, lineno);
        }
        static void handle_groupbar_render_titles(std::string_view val, const char* path, unsigned lineno) {
            g_config.groupbar_render_titles = common::expect_or_throw(values::parse_bool_val(val), path, lineno);
        }
        static void handle_groupbar_col_active(std::string_view val, const char*, unsigned) {
            g_config.groupbar_col_active = expand_quoted_value(val, g_config.conf_vars);
        }
        static void handle_groupbar_col_inactive(std::string_view val, const char*, unsigned) {
            g_config.groupbar_col_inactive = expand_quoted_value(val, g_config.conf_vars);
        }
        static void handle_groupbar_col_background(std::string_view val, const char*, unsigned) {
            g_config.groupbar_col_background = expand_quoted_value(val, g_config.conf_vars);
        }
        static void handle_groupbar_indicator_height(std::string_view val, const char* path, unsigned lineno) {
            g_config.groupbar_indicator_height = common::expect_or_throw(values::parse_int_val(val), path, lineno);
        }
        static void handle_groupbar_indicator_gap(std::string_view val, const char* path, unsigned lineno) {
            g_config.groupbar_indicator_gap = common::expect_or_throw(values::parse_int_val(val), path, lineno);
        }
        static void handle_groupbar_position(std::string_view val, const char* path, unsigned lineno) {
            g_config.groupbar_position = parse_groupbar_position(strip_outer_quotes(std::string(val)), path, lineno);
        }
        static void handle_groupbar_font_family(std::string_view val, const char*, unsigned) {
            g_config.groupbar_font_family = expand_quoted_value(val, g_config.conf_vars);
        }
        static void handle_groupbar_font_size(std::string_view val, const char* path, unsigned lineno) {
            g_config.groupbar_font_size = common::expect_or_throw(values::parse_int_val(val), path, lineno);
        }

        static void handle_group_col_border_active(std::string_view val, const char*, unsigned) {
            g_config.group_border_active_color = expand_quoted_value(val, g_config.conf_vars);
        }
        static void handle_group_col_border_inactive(std::string_view val, const char*, unsigned) {
            g_config.group_border_inactive_color = expand_quoted_value(val, g_config.conf_vars);
        }
        static void handle_group_auto_group(std::string_view val, const char* path, unsigned lineno) {
            g_config.group_auto_group = common::expect_or_throw(values::parse_bool_val(val), path, lineno);
        }
        static void handle_group_insert_after_current(std::string_view val, const char* path, unsigned lineno) {
            g_config.group_insert_after_current = common::expect_or_throw(values::parse_bool_val(val), path, lineno);
        }
        static void handle_group_focus_removed_window(std::string_view val, const char* path, unsigned lineno) {
            g_config.group_focus_removed_window = common::expect_or_throw(wm::config::parse_group_focus_removed_policy(val), path, lineno);
        }
        static void handle_group_drag_into_group(std::string_view val, const char* path, unsigned lineno) {
            g_config.group_drag_into_group = common::expect_or_throw(values::parse_int_val(val), path, lineno);
        }
        static void handle_group_drag_out_of_group(std::string_view val, const char* path, unsigned lineno) {
            g_config.group_drag_out_of_group = common::expect_or_throw(values::parse_bool_val(val), path, lineno);
        }
        static void handle_group_merge_groups_on_drag(std::string_view val, const char* path, unsigned lineno) {
            g_config.group_merge_groups_on_drag = common::expect_or_throw(values::parse_bool_val(val), path, lineno);
        }

        /* Resolve groupbar key handler; returns nullptr for unknown keys.
         *
         * Dispatch table contract:
         * - Keep keys unique and stable (first match wins).
         * - Add new groupbar keys here rather than extending `if/else` chains.
         */
        [[nodiscard]] static ValueHandlerFn find_groupbar_handler(std::string_view key) noexcept {
            static constexpr std::array<DispatchEntry, 10> kGroupbarDispatch = {{
                {"enabled", handle_groupbar_enabled},
                {"render_titles", handle_groupbar_render_titles},
                {"col.active", handle_groupbar_col_active},
                {"col.inactive", handle_groupbar_col_inactive},
                {"col.background", handle_groupbar_col_background},
                {"indicator_height", handle_groupbar_indicator_height},
                {"indicator_gap", handle_groupbar_indicator_gap},
                {"position", handle_groupbar_position},
                {"font_family", handle_groupbar_font_family},
                {"font_size", handle_groupbar_font_size},
            }};
            for (const auto& entry : kGroupbarDispatch) {
                if (entry.key == key)
                    return entry.handler;
            }
            return nullptr;
        }

        /* Resolve top-level group key handler; returns nullptr for unknown keys.
         *
         * Dispatch table contract:
         * - Handlers perform parsing + assignment only (no structural control flow).
         * - Unknown keys are handled by the caller with warning/no-op policy.
         */
        [[nodiscard]] static ValueHandlerFn find_group_handler(std::string_view key) noexcept {
            static constexpr std::array<DispatchEntry, 8> kGroupDispatch = {{
                {"col.border_active", handle_group_col_border_active},
                {"col.border_inactive", handle_group_col_border_inactive},
                {"auto_group", handle_group_auto_group},
                {"insert_after_current", handle_group_insert_after_current},
                {"focus_removed_window", handle_group_focus_removed_window},
                {"drag_into_group", handle_group_drag_into_group},
                {"drag_out_of_group", handle_group_drag_out_of_group},
                {"merge_groups_on_drag", handle_group_merge_groups_on_drag},
            }};
            for (const auto& entry : kGroupDispatch) {
                if (entry.key == key)
                    return entry.handler;
            }
            return nullptr;
        }
    } // namespace

    /* Emit group parser warning with source context.
     *
     * Used for:
     * - Unknown keys intentionally ignored to keep forward-compat behavior.
     * - Backend-specific no-op branches where parse should continue.
     */
    static void warn_group(std::string_view path, unsigned lineno, std::string_view section, std::string_view key) {
        std::string msg;
        msg.reserve(128U + path.size() + section.size() + key.size());
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

    /* Parse one `group { ... }` block including optional nested `groupbar { ... }`.
     *
     * Structural contract:
     * - `groupbar {` is only valid at direct depth-1 inside `group {`.
     * - Throws on malformed braces, unexpected closures, or invalid key/value syntax.
     *
     * Behavioral contract:
     * - Known keys mutate current runtime config globals.
     * - Unknown keys are warned and ignored (non-fatal).
     * - Returns only after matching closing brace for the outer `group {`.
     */
    void parse_group_block(std::istream& in, unsigned int& lineno, const char* path) {
        GroupState  state = GroupState::group;
        int         nest  = 1;
        std::string line;

        while (std::getline(in, line)) {
            ++lineno;
            line = strip_line_comment(std::move(line));
            if (line.empty())
                continue;

            if (line == "groupbar {") {
                if (state != GroupState::group || nest != 1) [[unlikely]]
                    common::throw_parse_error(path, lineno, "groupbar { only directly inside group { }");
                state = GroupState::groupbar;
                ++nest;
                continue;
            }

            if (line == "}") {
                --nest;
                if (nest < 0) [[unlikely]]
                    common::throw_parse_error(path, lineno, "unexpected '}'");
                if (nest == 0) {
                    if (state != GroupState::group) [[unlikely]]
                        common::throw_parse_error(path, lineno, "unclosed groupbar {");
                    return;
                }
                if (state == GroupState::groupbar && nest == 1) {
                    state = GroupState::group;
                    continue;
                }
                common::throw_parse_error(path, lineno, "unexpected '}'");
            }

            auto [key, val] = common::parse_key_value_or_throw(line, path, lineno, "group");

            if (state == GroupState::groupbar) {
                if (ValueHandlerFn handler = find_groupbar_handler(key); handler) {
                    handler(val, path, lineno);
                } else [[unlikely]] {
                    warn_group(path, lineno, "groupbar", key);
                }
                continue;
            }

            if (ValueHandlerFn handler = find_group_handler(key); handler) {
                handler(val, path, lineno);
            } else if (key == "col.border_locked_active" || key == "col.border_locked_inactive") {
                /* Reserved/unsupported on X11 path; intentionally ignored. */
            } else [[unlikely]] {
                warn_group(path, lineno, "group", key);
            }
        }

        common::throw_parse_error(path, "unterminated group { block");
    }

} /* namespace wm::config::parse */
