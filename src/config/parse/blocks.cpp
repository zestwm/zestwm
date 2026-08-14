/* Config block parser implementation for input/device sections. */
#include "config/parse/blocks.hpp"

#include "config.hpp"
#include "config/parse/common.hpp"
#include "config/parse/expand.hpp"
#include "config/parse/utils.hpp"
#include "config/parse/values.hpp"

#include <cctype>
#include <string>

namespace wm::config::parse {

    /* Validate safe device name charset accepted by runtime input matching. */
    [[nodiscard]] static bool safe_device_name(std::string_view name) {
        if (name.empty())
            return false;
        for (unsigned char c : name) {
            if (!std::isalnum(c) && c != '_' && c != '-' && c != '.' && c != ' ')
                return false;
        }
        return true;
    }

    /* Emit consistent unknown-key parser error for named block contexts. */
    [[noreturn]] static void throw_unknown_key(const char* path, unsigned lineno, std::string_view context_label, std::string_view key) {
        std::string detail = "unknown ";
        detail += context_label;
        detail += " key '";
        detail += key;
        detail += "'";
        common::throw_parse_error(path, lineno, detail);
    }

    /* Parse one device block and append validated device overrides to g_config.wm_input. */
    void parse_device_block(std::istream& in, unsigned int& lineno, const char* path) {
        InputDeviceConf dev;
        int             nest = 1;
        std::string     line;
        while (std::getline(in, line)) {
            lineno++;
            line = strip_line_comment(std::move(line));
            if (line.empty())
                continue;
            if (line == "device {") [[unlikely]]
                common::throw_parse_error(path, lineno, "nested device { is not allowed");
            if (line == "}") {
                nest--;
                if (nest == 0)
                    break;
                if (nest < 0) [[unlikely]]
                    common::throw_parse_error(path, lineno, "unexpected '}'");
                continue;
            }
            auto [key, val] = common::parse_key_value_or_throw(line, path, lineno, "device");
            if (key == "name") {
                std::string n = wm::config::expand::expand_all(strip_outer_quotes(val), g_config.conf_vars);
                if (!safe_device_name(n))
                    common::throw_parse_error(path, lineno, "invalid device name (use letters, digits, space, ._-)");
                dev.name = std::move(n);
            } else if (key == "sensitivity") {
                dev.sensitivity_set = true;
                dev.sensitivity     = common::expect_or_throw(wm::config::values::parse_float_val(val), path, lineno);
            } else if (key == "repeat_rate") {
                dev.repeat_rate_set = true;
                dev.repeat_rate_hz  = common::expect_or_throw(wm::config::values::parse_int_val(val), path, lineno);
            } else if (key == "repeat_delay") {
                dev.repeat_delay_set = true;
                dev.repeat_delay_ms  = common::expect_or_throw(wm::config::values::parse_int_val(val), path, lineno);
            } else {
                throw_unknown_key(path, lineno, "device", key);
            }
        }
        if (nest != 0)
            common::throw_parse_error(path, "unterminated device { block");
        if (dev.name.empty())
            common::throw_parse_error(path, lineno, "device { } needs name = ...");
        g_config.wm_input.devices.push_back(std::move(dev));
    }

    /* Parse input block (and nested touchpad/device sections) into g_config.wm_input fields. */
    void parse_input_block(std::istream& in, unsigned int& lineno, const char* path) {
        enum {
            ST_INPUT,
            ST_TOUCH
        } st     = ST_INPUT;
        int nest = 1;

        g_config.wm_input.input_block = true;
        std::string line;
        while (std::getline(in, line)) {
            lineno++;
            line = strip_line_comment(std::move(line));
            if (line.empty())
                continue;
            if (line == "device {") {
                if (st != ST_INPUT)
                    common::throw_parse_error(path, lineno, "device { only at input block scope (not inside touchpad)");
                parse_device_block(in, lineno, path);
                continue;
            }
            if (line == "touchpad {") {
                if (st != ST_INPUT || nest != 1)
                    common::throw_parse_error(path, lineno, "touchpad { only directly inside input { }");
                st = ST_TOUCH;
                nest++;
                continue;
            }
            if (line == "}") {
                nest--;
                if (nest < 0) [[unlikely]]
                    common::throw_parse_error(path, lineno, "unexpected '}'");
                if (nest == 0) {
                    if (st != ST_INPUT)
                        common::throw_parse_error(path, lineno, "unclosed touchpad {");
                    return;
                }
                if (st == ST_TOUCH && nest == 1) {
                    st = ST_INPUT;
                    continue;
                }
                common::throw_parse_error(path, lineno, "unexpected '}'");
            }
            if (line == "input {")
                common::throw_parse_error(path, lineno, "nested input { is not allowed");
            auto [key, val] = common::parse_key_value_or_throw(line, path, lineno, "input");
            if (st == ST_TOUCH) {
                if (key == "natural_scroll") {
                    g_config.wm_input.touch_natural_set = true;
                    g_config.wm_input.touch_natural     = common::expect_or_throw(wm::config::values::parse_bool_val(val), path, lineno);
                } else {
                    throw_unknown_key(path, lineno, "touchpad", key);
                }
                continue;
            }
            if (key == "kb_layout")
                g_config.wm_input.kb_layout = wm::config::expand::expand_all(strip_outer_quotes(val), g_config.conf_vars);
            else if (key == "kb_variant")
                g_config.wm_input.kb_variant = wm::config::expand::expand_all(strip_outer_quotes(val), g_config.conf_vars);
            else if (key == "kb_model")
                g_config.wm_input.kb_model = wm::config::expand::expand_all(strip_outer_quotes(val), g_config.conf_vars);
            else if (key == "kb_options")
                g_config.wm_input.kb_options = wm::config::expand::expand_all(strip_outer_quotes(val), g_config.conf_vars);
            else if (key == "kb_rules")
                g_config.wm_input.kb_rules = wm::config::expand::expand_all(strip_outer_quotes(val), g_config.conf_vars);
            else if (key == "repeat_rate") {
                g_config.wm_input.repeat_rate_set = true;
                g_config.wm_input.repeat_rate_hz  = common::expect_or_throw(wm::config::values::parse_int_val(val), path, lineno);
            } else if (key == "repeat_delay") {
                g_config.wm_input.repeat_delay_set = true;
                g_config.wm_input.repeat_delay_ms  = common::expect_or_throw(wm::config::values::parse_int_val(val), path, lineno);
            } else if (key == "follow_mouse") {
                const int mode = common::expect_or_throw(wm::config::values::parse_int_val(val), path, lineno);
                if (mode < 0 || mode > 3)
                    common::throw_parse_error(path, lineno, "follow_mouse must be an integer in range 0..3");
                g_config.wm_input.follow_mouse = mode;
            } else if (key == "sensitivity") {
                g_config.wm_input.sensitivity_set = true;
                g_config.wm_input.sensitivity     = common::expect_or_throw(wm::config::values::parse_float_val(val), path, lineno);
            } else {
                throw_unknown_key(path, lineno, "input", key);
            }
        }
        common::throw_parse_error(path, "unterminated input { block");
    }

} /* namespace wm::config::parse */
