/* Niri-style `window-rule` block parser (see docs/config/window-rules.md). */
#include "config/parse/window_rule_parse.hpp"

#include "config/parse/common.hpp"
#include "config/parse/expand.hpp"
#include "config/parse/utils.hpp"
#include "config/parse/values.hpp"
#include "config/parse/window_rule.hpp"
#include "special_workspace_registry.hpp"
#include "workspace_id.hpp"
#include "workspace_ref.hpp"
#include "workspace_registry.hpp"

#include <cctype>
#include <expected>
#include <sstream>
#include <string_view>

namespace wm::config::parse {

    namespace {
        [[nodiscard]] bool ascii_ieq(std::string_view a, std::string_view b) {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); ++i) {
                const unsigned char ca = static_cast<unsigned char>(a[i]);
                const unsigned char cb = static_cast<unsigned char>(b[i]);
                if (std::tolower(ca) != std::tolower(cb))
                    return false;
            }
            return true;
        }

        /* Parse on/off boolean tokens used in effect arguments. */
        [[nodiscard]] std::optional<bool> parse_bool_on(std::string_view s) {
            const std::string t = trim(s);
            if (t.empty())
                return std::nullopt;
            if (ascii_ieq(t, "on") || ascii_ieq(t, "true") || ascii_ieq(t, "yes") || t == "1")
                return true;
            if (ascii_ieq(t, "off") || ascii_ieq(t, "false") || ascii_ieq(t, "no") || t == "0")
                return false;
            return std::nullopt;
        }

        [[nodiscard]] std::regex compile_regex(const char* path, unsigned lineno, std::string_view pattern_sv) {
            try {
                return std::regex(std::string(pattern_sv), std::regex::ECMAScript | std::regex::optimize);
            } catch (const std::regex_error& e) { common::throw_parse_error(path, lineno, std::string("window-rule: invalid regex: ") + e.what()); }
        }

        [[nodiscard]] std::expected<WindowRuleAxisSpan, std::string> parse_axis_span(std::string_view token) {
            const std::string t = trim(token);
            if (t.empty())
                return std::unexpected("empty token");
            WindowRuleAxisSpan out{};
            if (t.back() == '%') {
                const auto n = wm::config::values::parse_double_val(std::string_view(t.data(), t.size() - 1U));
                if (!n)
                    return std::unexpected(n.error());
                out.percent = true;
                out.value   = *n;
                return out;
            }
            const auto n = wm::config::values::parse_double_val(t);
            if (!n)
                return std::unexpected(n.error());
            out.percent = false;
            out.value   = *n;
            return out;
        }

        [[nodiscard]] std::pair<std::optional<WorkspaceRef>, bool> parse_workspace_effect_token(const char* path, unsigned lineno, std::string_view token_raw,
                                                                                                const ConfVars& conf_vars) {
            std::string tok    = trim(expand::expand_all(strip_outer_quotes(std::string(token_raw)), conf_vars));
            bool        silent = false;
            if (tok.size() >= 7U && ascii_ieq(std::string_view(tok.data() + tok.size() - 7, 7), " silent")) {
                silent = true;
                tok    = trim(tok.substr(0, tok.size() - 7));
            }
            if (tok.empty()) [[unlikely]]
                common::throw_parse_error(path, lineno, "window-rule: open-on-workspace needs workspace argument");
            if (tok.size() >= 8U && ascii_ieq(std::string_view(tok.data(), 8), "special:")) {
                const auto pref = wm::workspace_ref::parse_workspace_ref_token(tok);
                if (!pref)
                    common::throw_parse_error(path, lineno, std::string("window-rule open-on-workspace: ") + pref.error());
                if (!pref->is_special())
                    common::throw_parse_error(path, lineno, "window-rule open-on-workspace: internal parse error for special:");
                if (!special_workspace_registry_ensure_tag(std::string(pref->special_tag)))
                    common::throw_parse_error(path, lineno, "window-rule open-on-workspace: special tag registry full (max 97 tags)");
                return {*pref, silent};
            }

            if (tok == "0")
                return {std::nullopt, silent};

            bool all_digits = true;
            for (const unsigned char ch : tok) {
                if (ch < '0' || ch > '9') {
                    all_digits = false;
                    break;
                }
            }
            if (all_digits) {
                const auto parsed_id = parse_workspace_id(tok);
                if (!parsed_id)
                    common::throw_parse_error(path, lineno, std::string("window-rule open-on-workspace: ") + parsed_id.error());
                return {WorkspaceRef::normal(*parsed_id), silent};
            }

            const WorkspaceMeta* meta = workspace_registry_find_by_name(tok);
            if (!meta)
                common::throw_parse_error(path, lineno, "window-rule open-on-workspace: unknown workspace name '" + tok + "'");
            return {WorkspaceRef::normal(meta->id), silent};
        }

        void set_match_regex(std::optional<std::regex>* slot, const char* path, unsigned lineno, std::string_view duplicate_label, std::string_view arg) {
            if (*slot) [[unlikely]]
                common::throw_parse_error(path, lineno, std::string("window-rule: duplicate matcher ") + std::string(duplicate_label));
            *slot = compile_regex(path, lineno, arg);
        }

        [[nodiscard]] std::vector<std::string> split_tokens(std::string_view s) {
            std::vector<std::string> out;
            std::string              cur;
            bool                     in_quote   = false;
            char                     quote_char = '\0';
            bool                     escaped    = false;
            for (const char ch : s) {
                if (escaped) {
                    escaped = false;
                    cur += ch;
                    continue;
                }
                if (in_quote && ch == '\\') {
                    escaped = true;
                    cur += ch;
                    continue;
                }
                if (!in_quote && (ch == '"' || ch == '\'')) {
                    in_quote   = true;
                    quote_char = ch;
                    cur += ch;
                    continue;
                }
                if (in_quote && ch == quote_char) {
                    in_quote   = false;
                    quote_char = '\0';
                    cur += ch;
                    continue;
                }
                if (!in_quote && std::isspace(static_cast<unsigned char>(ch))) {
                    if (!cur.empty()) {
                        out.push_back(cur);
                        cur.clear();
                    }
                    continue;
                }
                cur += ch;
            }
            if (!cur.empty())
                out.push_back(cur);
            return out;
        }

        void parse_matcher_directive(WindowRuleMatcherDirective* d, const char* path, unsigned lineno, std::string_view text) {
            const std::vector<std::string> tokens = split_tokens(text);
            for (size_t i = 0; i < tokens.size();) {
                std::string        key;
                std::string        val;
                const std::string& tok = tokens[i];
                const std::size_t  eq  = tok.find('=');
                if (eq != std::string::npos) {
                    key = trim(tok.substr(0, eq));
                    val = strip_outer_quotes(trim(tok.substr(eq + 1U)));
                    ++i;
                } else if ((i + 2U) < tokens.size() && tokens[i + 1U] == "=") {
                    key = trim(tok);
                    val = strip_outer_quotes(trim(tokens[i + 2U]));
                    i += 3U;
                } else {
                    common::throw_parse_error(path, lineno, "window-rule: matcher token must use key=value");
                }
                if (ascii_ieq(key, "title")) {
                    set_match_regex(&d->title, path, lineno, key, val);
                } else if (ascii_ieq(key, "app-id")) {
                    set_match_regex(&d->app_id, path, lineno, key, val);
                } else if (ascii_ieq(key, "is-active")) {
                    const auto b = parse_bool_on(val);
                    if (!b)
                        common::throw_parse_error(path, lineno, "window-rule: is-active needs bool");
                    d->is_active = b;
                } else if (ascii_ieq(key, "is-focused")) {
                    const auto b = parse_bool_on(val);
                    if (!b)
                        common::throw_parse_error(path, lineno, "window-rule: is-focused needs bool");
                    d->is_focused = b;
                } else if (ascii_ieq(key, "is-floating")) {
                    const auto b = parse_bool_on(val);
                    if (!b)
                        common::throw_parse_error(path, lineno, "window-rule: is-floating needs bool");
                    d->is_floating = b;
                } else if (ascii_ieq(key, "is-urgent")) {
                    const auto b = parse_bool_on(val);
                    if (!b)
                        common::throw_parse_error(path, lineno, "window-rule: is-urgent needs bool");
                    d->is_urgent = b;
                } else
                    common::throw_parse_error(path, lineno, "window-rule: unsupported matcher key '" + key + "'");
            }
        }

        [[nodiscard]] bool matcher_directive_nonempty(const WindowRuleMatcherDirective& d) {
            return d.title.has_value() || d.app_id.has_value() || d.is_active.has_value() || d.is_focused.has_value() || d.is_floating.has_value() || d.is_urgent.has_value();
        }

        void parse_rule_property(WindowRuleEntry* e, const char* path, unsigned lineno, const ConfVars& conf_vars, std::string_view key, std::string_view value) {
            const std::string v = trim(expand::expand_all(strip_outer_quotes(std::string(value)), conf_vars));
            if (ascii_ieq(key, "open-floating")) {
                const auto b = parse_bool_on(v);
                if (!b)
                    common::throw_parse_error(path, lineno, "window-rule: open-floating needs bool");
                e->effect_open_floating = b;
            } else if (ascii_ieq(key, "open-focused")) {
                const auto b = parse_bool_on(v);
                if (!b)
                    common::throw_parse_error(path, lineno, "window-rule: open-focused needs bool");
                e->effect_open_focused = b;
            } else if (ascii_ieq(key, "open-fullscreen")) {
                const auto b = parse_bool_on(v);
                if (!b)
                    common::throw_parse_error(path, lineno, "window-rule: open-fullscreen needs bool");
                e->effect_open_fullscreen = b;
            } else if (ascii_ieq(key, "open-maximized")) {
                const auto b = parse_bool_on(v);
                if (!b)
                    common::throw_parse_error(path, lineno, "window-rule: open-maximized needs bool");
                e->effect_open_maximized = b;
            } else if (ascii_ieq(key, "size")) {
                std::istringstream iss(v);
                std::string        wtok;
                std::string        htok;
                if (!(iss >> wtok >> htok))
                    common::throw_parse_error(path, lineno, "window-rule: size needs two arguments");
                std::string extra;
                if (iss >> extra)
                    common::throw_parse_error(path, lineno, "window-rule: size takes exactly two arguments");
                const auto w = parse_axis_span(wtok);
                const auto h = parse_axis_span(htok);
                if (!w || !h)
                    common::throw_parse_error(path, lineno, "window-rule: invalid size value");
                e->effect_open_size = std::make_pair(*w, *h);
            } else if (ascii_ieq(key, "move")) {
                std::istringstream iss(v);
                std::string        xtok;
                std::string        ytok;
                if (!(iss >> xtok >> ytok))
                    common::throw_parse_error(path, lineno, "window-rule: move needs two arguments");
                std::string extra;
                if (iss >> extra)
                    common::throw_parse_error(path, lineno, "window-rule: move takes exactly two arguments");
                const auto x = parse_axis_span(xtok);
                const auto y = parse_axis_span(ytok);
                if (!x || !y)
                    common::throw_parse_error(path, lineno, "window-rule: invalid move value");
                e->effect_open_move = std::make_pair(*x, *y);
            } else if (ascii_ieq(key, "center")) {
                const auto b = parse_bool_on(v);
                if (!b)
                    common::throw_parse_error(path, lineno, "window-rule: center needs bool");
                e->effect_open_center = b;
            } else if (ascii_ieq(key, "open-on-output")) {
                if (v.empty())
                    common::throw_parse_error(path, lineno, "window-rule: open-on-output needs selector");
                e->effect_open_on_output = v;
            } else if (ascii_ieq(key, "open-on-workspace")) {
                auto [target, silent]      = parse_workspace_effect_token(path, lineno, v, conf_vars);
                e->effect_workspace_target = target;
                e->effect_workspace_silent = silent;
            } else
                common::throw_parse_error(path, lineno, "window-rule: unsupported property '" + std::string(key) + "'");
        }

    } // namespace

    void parse_window_rule_block(std::istream& in, unsigned& lineno, std::string_view source_name, const ConfVars& conf_vars) {
        WindowRuleEntry   e;
        std::string       line;
        bool              closed = false;
        const std::string source_name_owned(source_name);
        while (std::getline(in, line)) {
            ++lineno;
            line = strip_line_comment(std::move(line));
            if (line.empty())
                continue;
            if (line == "}") {
                closed = true;
                break;
            }

            const std::string t = trim(line);
            if (t.size() >= 6U && ascii_ieq(std::string_view(t.data(), 6), "match ")) {
                WindowRuleMatcherDirective d{};
                parse_matcher_directive(&d, source_name_owned.c_str(), lineno, std::string_view(t).substr(6));
                if (!matcher_directive_nonempty(d))
                    common::throw_parse_error(source_name_owned.c_str(), lineno, "window-rule: empty match directive");
                e.matches.push_back(std::move(d));
                continue;
            }
            if (t.size() >= 8U && ascii_ieq(std::string_view(t.data(), 8), "exclude ")) {
                WindowRuleMatcherDirective d{};
                parse_matcher_directive(&d, source_name_owned.c_str(), lineno, std::string_view(t).substr(8));
                if (!matcher_directive_nonempty(d))
                    common::throw_parse_error(source_name_owned.c_str(), lineno, "window-rule: empty exclude directive");
                e.excludes.push_back(std::move(d));
                continue;
            }
            auto [key, val] = common::parse_key_value_or_throw(t, source_name_owned.c_str(), lineno, "window-rule { }");
            parse_rule_property(&e, source_name_owned.c_str(), lineno, conf_vars, key, val);
        }
        if (!closed)
            common::throw_parse_error(source_name_owned.c_str(), lineno, "window-rule { }: unclosed block (missing })");
        if (e.matches.empty())
            common::throw_parse_error(source_name_owned.c_str(), lineno, "window-rule { }: at least one match directive is required");
        window_rules.push_back(std::move(e));
    }

} /* namespace wm::config::parse */
