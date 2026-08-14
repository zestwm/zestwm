/* General option parser helpers implementation. */
#include "config/parse/general.hpp"

#include "config.hpp"
#include "config/parse/bindings.hpp"
#include "config/parse/common.hpp"
#include "config/parse/values.hpp"
#include "log.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <string_view>

namespace wm::config::parse::general {
    namespace {
        using GeneralHandlerFn = void (*)(std::string_view val, const char* path, unsigned lineno);

        struct GeneralDispatchEntry {
            std::string_view key;
            GeneralHandlerFn handler;
        };

        static void apply_borderpx(std::string_view val, const char* path, unsigned lineno) {
            g_config.borderpx = common::expect_or_throw(values::parse_uint_val(val), path, lineno);
        }

        static void apply_snap(std::string_view val, const char* path, unsigned lineno) {
            g_config.snap = common::expect_or_throw(values::parse_uint_val(val), path, lineno);
        }

        static void apply_lockfullscreen(std::string_view val, const char* path, unsigned lineno) {
            g_config.lockfullscreen = common::expect_or_throw(values::parse_int_val(val), path, lineno);
        }

        static void apply_refreshrate(std::string_view val, const char* path, unsigned lineno) {
            g_config.refreshrate = common::expect_or_throw(values::parse_int_val(val), path, lineno);
        }

        static void apply_activeopacity(std::string_view val, const char* path, unsigned lineno) {
            g_config.activeopacity = common::expect_or_throw(values::parse_double_val(val), path, lineno);
        }

        static void apply_inactiveopacity(std::string_view val, const char* path, unsigned lineno) {
            g_config.inactiveopacity = common::expect_or_throw(values::parse_double_val(val), path, lineno);
        }

        static void apply_dim_special(std::string_view val, const char* path, unsigned lineno) {
            const double parsed  = common::expect_or_throw(values::parse_double_val(val), path, lineno);
            g_config.dim_special = std::clamp(parsed, 0.0, 1.0);
        }

        [[nodiscard]] static GeneralHandlerFn find_general_handler(std::string_view key) noexcept {
            static constexpr std::array<GeneralDispatchEntry, 7> kGeneralDispatch = {{
                {"borderpx", apply_borderpx},
                {"snap", apply_snap},
                {"lockfullscreen", apply_lockfullscreen},
                {"refreshrate", apply_refreshrate},
                {"activeopacity", apply_activeopacity},
                {"inactiveopacity", apply_inactiveopacity},
                {"dim_special", apply_dim_special},
            }};
            for (const auto& entry : kGeneralDispatch) {
                if (entry.key == key)
                    return entry.handler;
            }
            return nullptr;
        }
    } // namespace

    /* Emit warning line with parser source context through shared logger. */
    static void warn_general_unknown_modkey(const char* path, unsigned lineno, std::string_view bad_token, std::string_view raw_value) {
        std::string detail;
        detail.reserve(64U + bad_token.size() + raw_value.size());
        detail = "modkey: unknown modifier '";
        detail.append(bad_token);
        detail += "' in '";
        detail.append(raw_value);
        detail += "' (ignored)";
        wm::log::warn_and_log(common::make_context_error(path, lineno, detail));
    }

    [[nodiscard]] static bool apply_modkey_option(std::string_view val, const char* path, unsigned lineno, unsigned int& out_modkey) {
        unsigned int mod_mask = 0U;
        std::string  bad_token;
        if (parse_mods_checked(val, &mod_mask, &bad_token)) {
            out_modkey = mod_mask;
            return true;
        }
        warn_general_unknown_modkey(path, lineno, bad_token, val);
        return false;
    }

    /* Resolve configured layout name to runtime layout callback. */
    void (*try_layout_by_name(std::string_view name) noexcept)(Monitor*) {
        if (name == "tree" || name == "dwindle")
            return tree;
        if (name == "monocle")
            return monocle;
        return nullptr;
    }

    void (*layout_by_name(std::string_view name))(Monitor*) {
        if (void (*fn)(Monitor*) = try_layout_by_name(name); fn)
            return fn;
        throw std::runtime_error(std::string("zestwm: unknown layout '") + std::string(name) + "'");
    }

    /* Apply one parsed general option key/value to global runtime config state. */
    void apply_general(std::string_view key, std::string_view val, const char* path, unsigned lineno) {
        if (GeneralHandlerFn handler = find_general_handler(key); handler) [[likely]] {
            handler(val, path, lineno);
            return;
        }

        if (key == "modkey") [[unlikely]] {
            static_cast<void>(apply_modkey_option(val, path, lineno, g_config.modkey));
            return;
        }

        common::throw_parse_error(path, lineno, std::string("unknown option '") + std::string(key) + "'");
    }

} /* namespace wm::config::parse::general */
