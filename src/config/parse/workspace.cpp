/* Workspace directive parser implementation (`workspace = ...`).
 * The parser normalizes selector/token forms and writes directly to workspace registries.
 * It is intentionally strict on structural errors and permissive on unknown policy keys
 * (policy-key validation/warnings are delegated to registry merge helpers). */
#include "config/parse/workspace.hpp"

#include "config/parse/expand.hpp"
#include "config/parse/utils.hpp"
#include "special_workspace_registry.hpp"
#include "workspace_id.hpp"
#include "workspace_ref.hpp"
#include "workspace_registry.hpp"
#include "workspace_selector.hpp"

#include <cctype>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace wm::config::parse::workspace {

    namespace {
        /* Split comma-separated workspace tokens while preserving commas inside quotes.
         *
         * Parsing rules:
         * - commas split only when outside quotes;
         * - quote chars `'` and `"` open/close quoted regions;
         * - backslash escapes are honored only inside quotes;
         * - each token is trimmed and outer quotes are removed.
         *
         * This allows display names like `"my,workspace"` while keeping classic CSV behavior
         * for unquoted token lists. */
        [[nodiscard]] std::vector<std::string> split_workspace_csv(std::string_view value, const auto& throw_at) {
            std::vector<std::string> parts;
            std::size_t              start      = 0U;
            bool                     in_quote   = false;
            char                     quote_char = '\0';
            bool                     escaped    = false;
            for (std::size_t i = 0; i < value.size(); ++i) {
                const char ch = value[i];
                if (escaped) {
                    escaped = false;
                    continue;
                }
                if (in_quote && ch == '\\') {
                    escaped = true;
                    continue;
                }
                if ((ch == '"' || ch == '\'') && !in_quote) {
                    in_quote   = true;
                    quote_char = ch;
                    continue;
                }
                if (in_quote && ch == quote_char) {
                    in_quote   = false;
                    quote_char = '\0';
                    continue;
                }
                if (ch == ',' && !in_quote) {
                    parts.push_back(strip_outer_quotes(trim(value.substr(start, i - start))));
                    start = i + 1U;
                }
            }
            if (in_quote) [[unlikely]]
                throw_at("workspace: unclosed quote in value list");
            parts.push_back(strip_outer_quotes(trim(value.substr(start))));
            return parts;
        }

        /* ASCII case-insensitive equality for short parser keywords.
         * Locale-independent behavior is required for predictable config parsing. */
        [[nodiscard]] bool ascii_ieq_sv(std::string_view a, std::string_view b) {
            if (a.size() != b.size())
                return false;
            for (std::size_t i = 0; i < a.size(); ++i) {
                const unsigned char ca = static_cast<unsigned char>(a[i]);
                const unsigned char cb = static_cast<unsigned char>(b[i]);
                if (std::tolower(ca) != std::tolower(cb))
                    return false;
            }
            return true;
        }

        /* Structural `key:value` shape check used for special-workspace trailing tokens.
         * This is intentionally broad: unknown keys are reported by merge helpers. */
        [[nodiscard]] bool token_has_key_value_shape(std::string_view tok) {
            const std::size_t colon = tok.find(':');
            if (colon == std::string_view::npos || colon == 0U)
                return false;
            return true;
        }

        /* Recognize known normal-workspace policy keys.
         * Tokens with ':' but unknown keys are treated as display names (not rules). */
        [[nodiscard]] bool token_is_normal_rule_field(std::string_view tok) {
            const std::size_t colon = tok.find(':');
            if (colon == std::string_view::npos || colon == 0U)
                return false;
            const std::string_view key = tok.substr(0U, colon);
            return ascii_ieq_sv(key, "gapsin") || ascii_ieq_sv(key, "gapsout") || ascii_ieq_sv(key, "bordersize") || ascii_ieq_sv(key, "border") || ascii_ieq_sv(key, "monitor") ||
                ascii_ieq_sv(key, "layout") || ascii_ieq_sv(key, "on-created-empty") || ascii_ieq_sv(key, "default") || ascii_ieq_sv(key, "persistent") ||
                ascii_ieq_sv(key, "defaultName");
        }

        /* `special:<tag>` selector; tag may be empty for default special.
         * Returns true and outputs the normalized tag payload when selector matches. */
        [[nodiscard]] bool workspace_head_is_special(std::string_view head, std::string& out_tag) {
            if (head.size() < 8U || !wm::workspace_ref::ascii_ieq_8(head.substr(0, 8), "special:"))
                return false;
            out_tag = trim(head.substr(8));
            return true;
        }

        /* True for `name:<value>` selector shape (case-insensitive prefix). */
        [[nodiscard]] bool workspace_head_is_name_selector(std::string_view head) {
            return head.size() >= 5U && ascii_ieq_sv(head.substr(0, 5), "name:");
        }

        /* Parse/trim payload part of `name:<value>` selector; throws on empty payload.
         * `throw_at` injects source/line context from the top-level parse function. */
        [[nodiscard]] std::string parse_name_selector_value_or_throw(std::string_view head, const auto& throw_at) {
            const std::string inner = trim(head.substr(5));
            if (inner.empty()) [[unlikely]]
                throw_at("workspace name: value is empty");
            return inner;
        }

        /* Resolve normal workspace id from selector; append/ensure registry entries as needed.
         *
         * Resolution order:
         * 1) `name:<value>` selector;
         * 2) decimal id selector;
         * 3) fallback to plain display-name selector.
         *
         * Returns stable `WorkspaceId` after any required registry mutation. */
        [[nodiscard]] WorkspaceId resolve_or_create_normal_workspace_id(std::string_view head, const auto& throw_at) {
            if (workspace_head_is_name_selector(head)) {
                const std::string    inner = parse_name_selector_value_or_throw(head, throw_at);
                const WorkspaceMeta* found = workspace_registry_find_by_name(inner);
                if (found)
                    return found->id;
                workspace_registry_append_named(inner);
                return static_cast<WorkspaceId>(workspace_registry_count());
            }
            if (const auto parsed_head = parse_workspace_id(head)) {
                workspace_registry_ensure_id(*parsed_head);
                return *parsed_head;
            }
            const WorkspaceMeta* found = workspace_registry_find_by_name(head);
            if (found)
                return found->id;
            workspace_registry_append_named(std::string(head));
            return static_cast<WorkspaceId>(workspace_registry_count());
        }
    } // namespace

    /* Parse one `workspace = ...` directive and mutate workspace registries accordingly.
     *
     * High-level flow:
     * 1) expand variables and strip outer quotes from RHS;
     * 2) split into tokens (quote-aware CSV);
     * 3) resolve selector (`special:` vs normal workspace forms);
     * 4) apply trailing display-name/rule tokens with selector-specific constraints. */
    void apply_workspace_line(std::string_view raw_value, ConfVars& conf_vars, std::string_view source_name, unsigned lineno) {
        const std::string              expanded = expand::expand_all(strip_outer_quotes(std::string(raw_value)), conf_vars);
        const std::string              value    = trim(expanded);
        const std::string              source_name_owned(source_name);
        auto                           throw_at = [&](const char* msg) { throw std::runtime_error("zestwm: " + source_name_owned + ":" + std::to_string(lineno) + ": " + msg); };
        const std::vector<std::string> parts    = split_workspace_csv(value, throw_at);
        if (value.empty() || parts.empty()) [[unlikely]]
            throw_at("workspace needs a non-empty value");

        /* Single-token form configures only the selector side (`workspace = <selector>`). */
        if (parts.size() == 1U) {
            const std::string& only = parts[0];
            std::string        special_tag;
            if (workspace_head_is_special(only, special_tag)) {
                /* 97-tag cap is the registry hard limit used by hidden-id special workspace mapping. */
                if (!special_workspace_registry_ensure_tag(std::move(special_tag)))
                    throw_at("at most 97 special workspace tags");
                return;
            }
            static_cast<void>(resolve_or_create_normal_workspace_id(only, throw_at));
            return;
        }

        const std::string& head         = parts[0];
        WorkspaceId        workspace_id = 0U;

        /* Special selector path: only `key:value` rule tokens are accepted after selector. */
        std::string special_tag_head;
        if (workspace_head_is_special(head, special_tag_head)) {
            /* 97-tag cap is the registry hard limit used by hidden-id special workspace mapping. */
            if (!special_workspace_registry_ensure_tag(std::string(special_tag_head)))
                throw_at("at most 97 special workspace tags");
            for (std::size_t i = 1; i < parts.size(); ++i) {
                const std::string& tok = parts[i];
                if (tok.empty())
                    continue;
                if (token_has_key_value_shape(tok))
                    special_workspace_registry_merge_rule_token(special_tag_head, tok, source_name_owned, lineno);
                else
                    throw_at("special workspace line expects only `key:value` rules after the selector");
            }
            return;
        }

        /* Dynamic bracket selectors (`r[...]`, `w[...]`, ...) store runtime-matched policy rules. */
        if (wm::workspace_selector::looks_like_workspace_selector(head)) {
            for (std::size_t i = 1; i < parts.size(); ++i) {
                const std::string& tok = parts[i];
                if (tok.empty())
                    continue;
                if (!token_is_normal_rule_field(tok))
                    throw_at("workspace selector line expects only `key:value` rules after the selector");
                workspace_registry_merge_selector_rule_token(head, tok, source_name_owned, lineno);
            }
            return;
        }

        workspace_id = resolve_or_create_normal_workspace_id(head, throw_at);

        /* Normal selector path:
         * - at most one non-rule trailing token is interpreted as display name;
         * - known `key:value` tokens are merged as workspace policy rules. */
        bool have_display_name = false;
        for (std::size_t i = 1; i < parts.size(); ++i) {
            const std::string& tok = parts[i];
            if (tok.empty())
                continue;
            if (token_is_normal_rule_field(tok)) {
                workspace_registry_merge_rule_token(workspace_id, tok, source_name_owned, lineno);
                continue;
            }
            if (have_display_name)
                throw_at("workspace line has multiple non-rule tokens after workspace selector");
            workspace_registry_set_name_for_id(workspace_id, tok);
            have_display_name = true;
        }
    }

} /* namespace wm::config::parse::workspace */
