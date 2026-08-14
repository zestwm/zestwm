/* Core config parser orchestration implementation. */
#include "config/parse/core.hpp"

#include "config/env.hpp"
#include "config/parse/bindings.hpp"
#include "config/parse/blocks.hpp"
#include "config/parse/common.hpp"
#include "config/parse/expand.hpp"
#include "config/parse/general.hpp"
#include "config/parse/group.hpp"
#include "config/parse/sections.hpp"
#include "config/parse/utils.hpp"
#include "config/parse/window_rule_parse.hpp"
#include "config/parse/workspace.hpp"
#include "log.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wm::config::parse::core {

    namespace {
        /* Maximum recursive include depth for `source = ...` expansion. */
        constexpr unsigned kMaxSourceDepth = 32;

        using BlockHandlerFn = void (*)(CoreContext&, std::istream&, unsigned int&, std::string_view);
        struct BlockDispatchEntry {
            std::string_view token;
            BlockHandlerFn   handler;
        };

        /* Build parser context object for binding-related line parsers.
         *
         * This keeps call sites concise and ensures a single source of truth for
         * `BindingsContext` field wiring.
         */
        [[nodiscard]] static BindingsContext make_bindings_context(CoreContext& ctx, const char* source_path, unsigned lineno) {
            return BindingsContext{ctx.config.keys, ctx.config.buttons, ctx.config.layouts, ctx.config.conf_vars, source_path, lineno, general::layout_by_name};
        }

        static void handle_misc_block(CoreContext&, std::istream& in, unsigned int& lineno, std::string_view source_name) {
            const std::string source_name_owned(source_name);
            sections::parse_misc_block(in, lineno, source_name_owned.c_str());
        }

        static void handle_dwindle_block(CoreContext&, std::istream& in, unsigned int& lineno, std::string_view source_name) {
            const std::string source_name_owned(source_name);
            sections::parse_dwindle_block(in, lineno, source_name_owned.c_str());
        }

        static void handle_general_block(CoreContext&, std::istream& in, unsigned int& lineno, std::string_view source_name) {
            const std::string source_name_owned(source_name);
            sections::parse_general_block(in, lineno, source_name_owned.c_str());
        }

        static void handle_group_block(CoreContext&, std::istream& in, unsigned int& lineno, std::string_view source_name) {
            const std::string source_name_owned(source_name);
            parse_group_block(in, lineno, source_name_owned.c_str());
        }

        static void handle_input_block(CoreContext&, std::istream& in, unsigned int& lineno, std::string_view source_name) {
            const std::string source_name_owned(source_name);
            parse_input_block(in, lineno, source_name_owned.c_str());
        }

        static void handle_device_block(CoreContext&, std::istream& in, unsigned int& lineno, std::string_view source_name) {
            const std::string source_name_owned(source_name);
            parse_device_block(in, lineno, source_name_owned.c_str());
        }

        static void handle_window_rule_block(CoreContext& ctx, std::istream& in, unsigned int& lineno, std::string_view source_name) {
            parse_window_rule_block(in, lineno, source_name, ctx.config.conf_vars);
        }

        static void handle_binds_block(CoreContext& ctx, std::istream& in, unsigned int& lineno, std::string_view source_name) {
            const std::string source_name_owned(source_name);
            parse_binds_block(make_bindings_context(ctx, source_name_owned.c_str(), lineno), in, lineno);
        }

        [[nodiscard]] static BlockHandlerFn find_block_handler(std::string_view line) noexcept {
            static constexpr std::array<BlockDispatchEntry, 8> kBlockDispatch = {{
                {"misc {", handle_misc_block},
                {"dwindle {", handle_dwindle_block},
                {"general {", handle_general_block},
                {"group {", handle_group_block},
                {"input {", handle_input_block},
                {"device {", handle_device_block},
                {"window-rule {", handle_window_rule_block},
                {"binds {", handle_binds_block},
            }};
            for (const auto& entry : kBlockDispatch) {
                if (entry.token == line)
                    return entry.handler;
            }
            return nullptr;
        }

        /* Handle assignment-style top-level directive.
         *
         * Returns true when key was fully handled, false when caller should fallback to
         * generic parser dispatch.
         */
        [[nodiscard]] static bool handle_assignment_directive(CoreContext& ctx, const std::filesystem::path& base_dir, std::string_view source_name, unsigned depth,
                                                              std::vector<std::string>& include_stack, unsigned lineno, std::string_view key, std::string_view val) {
            const std::string source_name_owned(source_name);
            if (key == "source") {
                std::string p = expand::expand_all(strip_outer_quotes(std::string(val)), ctx.config.conf_vars);
                if (p.empty())
                    common::throw_parse_error(source_name_owned.c_str(), lineno, "source needs a path");
                std::filesystem::path inc(p);
                if (!inc.is_absolute())
                    inc = base_dir / inc;
                parse_config_file(ctx, inc, depth + 1, include_stack);
                return true;
            }

            if (!key.empty() && key[0] == '$') {
                std::string name(key.substr(1));
                if (!expand::valid_conf_var_name(name))
                    common::throw_parse_error(source_name_owned.c_str(), lineno, "invalid variable name in '" + std::string(key) + "'");
                ctx.config.conf_vars[name] = expand::expand_all(strip_outer_quotes(std::string(val)), ctx.config.conf_vars);
                return true;
            }

            if (key == "env") {
                wm::config::env::apply_env_line(val, false, source_name_owned.c_str(), lineno, ctx.config.conf_vars);
                return true;
            }
            if (key == "envd") {
                wm::config::env::apply_env_line(val, true, source_name_owned.c_str(), lineno, ctx.config.conf_vars);
                return true;
            }

            if (key == "font") {
                std::string v = expand::expand_all(val, ctx.config.conf_vars);
                if (ctx.config.wm_misc.font_family.empty())
                    ctx.config.wm_misc.font_family = std::move(v);
                else
                    ctx.config.wm_misc.font_family += "," + v;
                return true;
            }

            return false;
        }

        /* Record parser error summary and emit contextual warning line. */
        void record_config_error(CoreContext& ctx, std::string_view path, unsigned lineno, std::string_view msg) {
            ++ctx.error_count;
            if (ctx.error_notice.empty())
                ctx.error_notice = std::string(msg);
            const char* source = path.empty() ? "?" : path.data();
            std::string full   = common::make_context_error(source, lineno, msg);
            wm::log::warn_and_log(full);
        }

        /* Skip nested block body after a parsing error to resume top-level scanning. */
        void skip_block_body(std::istream& in, unsigned int& lineno) {
            int         depth = 1;
            std::string line;

            while (depth > 0 && std::getline(in, line)) {
                ++lineno;
                line = strip_line_comment(std::move(line));
                if (line.empty())
                    continue;
                for (char c : line) {
                    if (c == '{')
                        ++depth;
                    else if (c == '}')
                        --depth;
                }
            }
        }

        /* Track canonical include path in first-seen order with O(1) duplicate checks. */
        static void remember_loaded_file(CoreContext& ctx, std::string_view canonical_path) {
            std::string canonical{canonical_path};
            if (!ctx.loaded_file_set.insert(canonical).second)
                return;
            ctx.loaded_file_list.emplace_back(std::move(canonical));
        }
    } // namespace

    /* Parse one config stream and dispatch top-level directives with recovery. */
    void parse_config_stream(CoreContext& ctx, std::istream& in, const std::filesystem::path& base_dir, std::string_view source_name, unsigned depth,
                             std::vector<std::string>& include_stack) {
        std::string       line;
        unsigned int      lineno = 0;
        const std::string source_name_owned(source_name);
        const char*       source_c = source_name_owned.c_str();

        while (std::getline(in, line)) {
            bool opened_block = false;
            ++lineno;
            line = strip_line_comment(std::move(line));
            if (line.empty())
                continue;

            try {
                if (const BlockHandlerFn block_handler = find_block_handler(line); block_handler) {
                    opened_block = true;
                    block_handler(ctx, in, lineno, source_name);
                    continue;
                }

                auto [key, val] = common::parse_key_value_or_throw(line, source_c, lineno);
                if (handle_assignment_directive(ctx, base_dir, source_name, depth, include_stack, lineno, key, val))
                    continue;

                if (key == "layout")
                    parse_layout_line(make_bindings_context(ctx, source_c, lineno), val);
                else if (key == "exec-once") {
                    std::string cmd = trim(val);
                    if (!cmd.empty())
                        ctx.config.exec_once.push_back(expand::expand_all(cmd, ctx.config.conf_vars));
                } else if (key == "workspace") {
                    workspace::apply_workspace_line(val, ctx.config.conf_vars, source_name, lineno);
                } else
                    general::apply_general(key, val, source_c, lineno);
            } catch (const std::exception& e) {
                record_config_error(ctx, source_c, lineno, e.what());
                if (opened_block)
                    skip_block_body(in, lineno);
            }
        }
    }

    /* Parse one config file with canonical include path checks and cycle guard. */
    void parse_config_file(CoreContext& ctx, const std::filesystem::path& file_path, unsigned depth, std::vector<std::string>& include_stack) {
        if (depth > kMaxSourceDepth) [[unlikely]]
            throw std::runtime_error("zestwm: source nesting exceeds max depth (" + std::to_string(kMaxSourceDepth) + ")");

        std::error_code             ec;
        const std::filesystem::path canon = std::filesystem::weakly_canonical(file_path, ec);
        if (ec) [[unlikely]]
            throw std::runtime_error("zestwm: cannot resolve config path '" + file_path.string() + "'");

        const std::string canon_s = canon.string();
        for (const auto& seen : include_stack) {
            if (seen == canon_s) [[unlikely]]
                throw std::runtime_error("zestwm: source include cycle: '" + canon_s + "'");
        }

        include_stack.push_back(canon_s);
        remember_loaded_file(ctx, canon_s);

        std::ifstream in(canon);
        if (!in) [[unlikely]]
            throw std::runtime_error("zestwm: cannot open config '" + canon_s + "'");

        parse_config_stream(ctx, in, canon.parent_path(), canon_s, depth, include_stack);
        include_stack.pop_back();
    }

} /* namespace wm::config::parse::core */
