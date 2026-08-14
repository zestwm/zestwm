/* Config bind/button action argument parser implementation.
 *
 * This module parses textual bind config into typed `ActionCommand` payloads.
 */
#include "config/parse/action.hpp"

#include "actions.hpp"
#include "actions/workspace.hpp"
#include "config.hpp"
#include "layoutmsg.hpp"
#include "special_workspace_registry.hpp"
#include "workspace_ref.hpp"
#include "workspace_registry.hpp"
#include "config/parse/utils.hpp"
#include "config/parse/values.hpp"

#include <cctype>
#include <array>
#include <expected>
#include <sstream>
#include <string>
#include <vector>
#include <string_view>

namespace wm::config::parse {
    using ParsePayloadResult = std::expected<ActionPayload, std::string>;
    using ArgParserFn        = ParsePayloadResult (*)(std::string_view argstr);

    /* ASCII-only case-insensitive equality for short parser keywords. */
    [[nodiscard]] static bool ascii_ieq_sv(std::string_view a, std::string_view b) {
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

    /* Optional ` silent` suffix on workspace dispatcher arguments. */
    [[nodiscard]] static bool strip_optional_workspace_silent_suffix(std::string& s) {
        if (s.size() >= 7U && ascii_ieq_sv(std::string_view(s.data() + s.size() - 7, 7), " silent")) {
            s.resize(s.size() - 7U);
            s = wm::config::parse::trim(s);
            return true;
        }
        return false;
    }

    /* ASCII-only case-insensitive prefix probe for dispatcher tokens. */
    [[nodiscard]] static bool has_ascii_prefix(std::string_view s, std::string_view prefix) {
        if (s.size() < prefix.size())
            return false;
        return ascii_ieq_sv(s.substr(0, prefix.size()), prefix);
    }

    /* Build parser error result for invalid action argument payloads. */
    [[nodiscard]] static ParsePayloadResult parse_action_error(std::string_view msg) {
        return std::unexpected(std::string(msg));
    }

    /* Parse integer action argument using shared value parser diagnostics. */
    [[nodiscard]] static std::expected<int, std::string> parse_int_or_error(std::string_view value) {
        const auto parsed = wm::config::values::parse_int_val(value);
        if (!parsed)
            return std::unexpected(parsed.error());
        return parsed;
    }

    /* Parse unsigned action argument with optional base override. */
    [[nodiscard]] static std::expected<unsigned int, std::string> parse_uint_or_error(std::string_view value, int base = 0) {
        const auto parsed = wm::config::values::parse_uint_val(value, base);
        if (!parsed)
            return std::unexpected(parsed.error());
        return parsed;
    }

    /* Parse floating-point action argument for splitratio-like actions. */
    [[nodiscard]] static std::expected<float, std::string> parse_float_or_error(std::string_view value) {
        const auto parsed = wm::config::values::parse_float_val(value);
        if (!parsed)
            return std::unexpected(parsed.error());
        return parsed;
    }

    /* Parse workspace token as numeric WorkspaceId or configured workspace name. */
    [[nodiscard]] static std::expected<WorkspaceId, std::string> parse_workspace_id_or_name(std::string_view token) {
        if (token.empty())
            return std::unexpected("workspace token is empty");

        bool all_digits = true;
        for (unsigned char ch : token) {
            if (ch < '0' || ch > '9') {
                all_digits = false;
                break;
            }
        }

        /* If token is numeric-like, rely on strict WorkspaceId parsing (no name fallback). */
        if (all_digits)
            return parse_workspace_id(token);

        if (const WorkspaceMeta* meta = workspace_registry_find_by_name(token); meta)
            return meta->id;
        return std::unexpected("unknown workspace name");
    }

    struct ParsedWorkspaceDispatchToken {
        /* Parsed logical workspace target (`normal` or `special`). */
        WorkspaceRef ref = WorkspaceRef::unset();
        /* Optional hidden-id bridge for special workspaces (phase migration metadata). */
        std::optional<WorkspaceId> hidden_id = std::nullopt;
    };

    /* Parse bind/workspace dispatcher token: `special:<tag>` or numeric/name normal workspace. */
    [[nodiscard]] static std::expected<ParsedWorkspaceDispatchToken, std::string> parse_workspace_dispatch_token(std::string_view token) {
        if (token.size() >= 8U && wm::workspace_ref::ascii_ieq_8(token, "special:")) {
            const auto pr = wm::workspace_ref::parse_workspace_ref_token(token);
            if (!pr)
                return std::unexpected(pr.error());
            if (!pr->is_special())
                return std::unexpected("internal: expected special workspace ref");
            if (!special_workspace_registry_ensure_tag(std::string(pr->special_tag)))
                return std::unexpected("special workspace tag registry full (max 97 tags)");
            ParsedWorkspaceDispatchToken out{};
            out.ref       = *pr;
            out.hidden_id = special_workspace_registry_hidden_id_by_tag(pr->special_tag);
            return out;
        }
        const auto id = parse_workspace_id_or_name(token);
        if (!id)
            return std::unexpected(id.error());
        return ParsedWorkspaceDispatchToken{.ref = WorkspaceRef::normal(*id)};
    }

    /* Convert dispatcher argument text into typed payload by selected action handler. */
    /* Direction parser (`l/r/u/d/t/b`): consumes first byte and lowercases it. */
    [[nodiscard]] static ParsePayloadResult parse_direction(std::string_view s) noexcept {
        int value = 0;
        if (!s.empty())
            value = static_cast<int>(static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(s[0]))));
        return IntPayload{.value = value};
    }

    /* Integer parser adapter: converts shared int expected result into typed payload. */
    [[nodiscard]] static ParsePayloadResult parse_int_dispatch(std::string_view s) {
        const auto parsed = parse_int_or_error(s);
        if (!parsed)
            return std::unexpected(parsed.error());
        return IntPayload{.value = *parsed};
    }

    /* Float parser adapter used by split-ratio style dispatchers. */
    [[nodiscard]] static ParsePayloadResult parse_float_dispatch(std::string_view s) {
        const auto parsed = parse_float_or_error(s);
        if (!parsed)
            return std::unexpected(parsed.error());
        return FloatPayload{.value = *parsed};
    }

    /* Workspace parser core:
     * - trims token,
     * - applies optional ` silent` suffix policy,
     * - builds typed WorkspaceArgPayload on success. */
    /* silent_default: force `silent=true` by default for this action family.
     * allow_silent_suffix: when true, parsed ` silent` suffix can enable silent mode. */
    [[nodiscard]] static ParsePayloadResult parse_workspace_dispatch(std::string_view s, bool silent_default, bool allow_silent_suffix) {
        std::string ws_arg     = wm::config::parse::trim(std::string(s));
        bool        silent     = silent_default;
        const bool  had_suffix = strip_optional_workspace_silent_suffix(ws_arg);
        if (allow_silent_suffix && had_suffix)
            silent = true;

        const auto parsed_ref = parse_workspace_dispatch_token(ws_arg);
        if (!parsed_ref)
            return std::unexpected(parsed_ref.error());
        auto payload = WorkspaceArgPayload{
            .ref       = parsed_ref->ref,
            .silent    = silent,
            .hidden_id = parsed_ref->hidden_id,
        };
        return WorkspaceDispatchPayload{.payload = std::move(payload)};
    }

    /* `viewworkspace`: strips optional suffix for compatibility but does not force silent mode. */
    [[nodiscard]] static ParsePayloadResult parse_viewworkspace_dispatch(std::string_view s) {
        return parse_workspace_dispatch(s, false, false);
    }

    /* `movetoworkspaceid`: optional ` silent` suffix can enable silent move. */
    [[nodiscard]] static ParsePayloadResult parse_movetoworkspaceid_dispatch(std::string_view s) {
        return parse_workspace_dispatch(s, false, true);
    }

    /* `movetoworkspacesilent`: silent mode is enabled by default. */
    [[nodiscard]] static ParsePayloadResult parse_movetoworkspacesilent_dispatch(std::string_view s) {
        return parse_workspace_dispatch(s, true, true);
    }

    /* Spawn parser:
     * - shell argv when metacharacters are present,
     * - direct argv tokenization otherwise. */
    [[nodiscard]] static ParsePayloadResult parse_spawn_dispatch(std::string_view s) {
        std::string cmd        = std::string(s);
        const bool  need_shell = cmd.find_first_of(" \t|><") != std::string::npos;
        if (need_shell) {
            return SpawnArgvPayload{.args = {"/bin/sh", "-c", std::move(cmd)}};
        }
        std::istringstream       iss{cmd};
        std::vector<std::string> tok;
        for (std::string w; iss >> w;)
            tok.push_back(std::move(w));
        if (tok.empty())
            return std::unexpected("spawn needs a command");
        return SpawnArgvPayload{.args = std::move(tok)};
    }

    /* Layoutmsg parser: tokenizes subcommand stream into by-value LayoutMsgPayload. */
    [[nodiscard]] static ParsePayloadResult parse_layoutmsg_dispatch(std::string_view s) {
        std::istringstream       iss{std::string(s)};
        std::vector<std::string> tok;
        for (std::string w; iss >> w;)
            tok.push_back(std::move(w));
        if (tok.empty())
            return parse_action_error("layoutmsg needs subcommand");

        const auto parsed_layoutmsg = parse_layoutmsg_tokens(tok);
        if (!parsed_layoutmsg)
            return parse_action_error(parsed_layoutmsg.error());
        return LayoutMsgDispatchPayload{.payload = *parsed_layoutmsg};
    }

    /* Group-active parser: accepts `f/forward` and `b/back` aliases. */
    [[nodiscard]] static ParsePayloadResult parse_group_active_dispatch(std::string_view s) {
        std::string u = std::string(s);
        for (char& c : u)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (u == "f" || u == "forward") {
            return IntPayload{.value = 1};
        }
        if (u == "b" || u == "back") {
            return IntPayload{.value = -1};
        }
        return parse_action_error("changegroupactive needs f or b");
    }

    /* Setlayout parser: strict unsigned parse plus bounds check against `g_config.layouts`. */
    [[nodiscard]] static ParsePayloadResult parse_setlayout_dispatch(std::string_view s) {
        const auto parsed_idx = parse_uint_or_error(s);
        if (!parsed_idx)
            return std::unexpected(parsed_idx.error());
        const unsigned int idx = *parsed_idx;
        if (idx >= g_config.layouts.size()) {
            std::string msg = "setlayout index ";
            msg += std::to_string(idx);
            msg += " out of range";
            return std::unexpected(std::move(msg));
        }
        return SetLayoutDispatchPayload{.layout = &g_config.layouts[idx]};
    }

    /* Special-toggle parser:
     * - accepts raw tag or `special:<tag>`,
     * - ensures registry slot exists before payload allocation. */
    [[nodiscard]] static ParsePayloadResult parse_toggle_special_dispatch(std::string_view s) {
        std::string tag = wm::config::parse::trim(std::string(s));
        if (tag.empty())
            return parse_action_error("togglespecialworkspace needs an argument (use a tag, special:<tag>, or special: for default scratchpad)");

        ParsedWorkspaceDispatchToken parsed_special{};
        if (has_ascii_prefix(tag, "special:")) {
            const auto parsed_ref = parse_workspace_dispatch_token(tag);
            if (!parsed_ref)
                return std::unexpected(parsed_ref.error());
            if (!parsed_ref->ref.is_special())
                return parse_action_error("togglespecialworkspace expects a special workspace target");
            parsed_special = *parsed_ref;
        } else {
            if (!special_workspace_registry_ensure_tag(tag))
                return parse_action_error("special workspace tag registry full (max 97 tags)");
            parsed_special.ref       = WorkspaceRef::special(tag);
            parsed_special.hidden_id = special_workspace_registry_hidden_id_by_tag(tag);
        }

        auto payload = WorkspaceArgPayload{
            .ref       = parsed_special.ref,
            .silent    = false,
            .hidden_id = parsed_special.hidden_id,
        };
        return WorkspaceDispatchPayload{.payload = std::move(payload)};
    }

    /* Zero-argument parser adapter for pure toggle handlers. */
    [[nodiscard]] static ParsePayloadResult parse_noarg_dispatch(std::string_view) noexcept {
        return NoArgPayload{};
    }

    /* Groupmode parser: currently supports only toggle-compatible values. */
    [[nodiscard]] static ParsePayloadResult parse_groupmode_dispatch(std::string_view s) {
        if (s.empty() || s == "toggle" || s == "-1") {
            return IntPayload{.value = -1};
        }
        return parse_action_error("groupmode accepts only: toggle");
    }

    /* Static mapping entry between action callback and its parser function. */
    struct DispatchEntry {
        KeyFn             fn;
        ArgParserFn       parser;
        ActionPayloadKind kind;
    };

    /* Compile-time dispatch table for known action handlers.
     *
     * NOTE: keep entries ordered by semantic groups for readability.
     * If this table grows beyond ~30 entries, consider sorting by `fn`
     * and switching to binary-search dispatch. */
    static constexpr std::array<DispatchEntry, 31> kActionParsers = {{
        {movefocus, parse_direction, ActionPayloadKind::Int},
        {swapwindow, parse_direction, ActionPayloadKind::Int},
        {movewindoworgroup, parse_direction, ActionPayloadKind::Int},
        {cyclefocus, parse_int_dispatch, ActionPayloadKind::Int},
        {focusmonitor, parse_int_dispatch, ActionPayloadKind::Int},
        {movetomonitor, parse_int_dispatch, ActionPayloadKind::Int},
        {movegroup, parse_int_dispatch, ActionPayloadKind::Int},
        {moveoutofgroup, parse_int_dispatch, ActionPayloadKind::Int},
        {sendtogroup, parse_int_dispatch, ActionPayloadKind::Int},
        {focussplit, parse_int_dispatch, ActionPayloadKind::Int},
        {cyclegroup, parse_int_dispatch, ActionPayloadKind::Int},
        {cyclelayout, parse_int_dispatch, ActionPayloadKind::Int},
        {splitratio, parse_float_dispatch, ActionPayloadKind::Float},
        {layoutmsg, parse_layoutmsg_dispatch, ActionPayloadKind::LayoutMsgDispatch},
        {spawn, parse_spawn_dispatch, ActionPayloadKind::SpawnArgv},
        {viewworkspace, parse_viewworkspace_dispatch, ActionPayloadKind::WorkspaceDispatch},
        {movetoworkspaceid, parse_movetoworkspaceid_dispatch, ActionPayloadKind::WorkspaceDispatch},
        {movetoworkspacesilent, parse_movetoworkspacesilent_dispatch, ActionPayloadKind::WorkspaceDispatch},
        {togglespecialworkspace, parse_toggle_special_dispatch, ActionPayloadKind::WorkspaceDispatch},
        {quit, parse_noarg_dispatch, ActionPayloadKind::NoArg},
        {killclient, parse_noarg_dispatch, ActionPayloadKind::NoArg},
        {togglefloating, parse_noarg_dispatch, ActionPayloadKind::NoArg},
        {togglefullscreen, parse_noarg_dispatch, ActionPayloadKind::NoArg},
        {cyclenext, parse_noarg_dispatch, ActionPayloadKind::NoArg},
        {cycleprev, parse_noarg_dispatch, ActionPayloadKind::NoArg},
        {movemouse, parse_noarg_dispatch, ActionPayloadKind::NoArg},
        {resizemouse, parse_noarg_dispatch, ActionPayloadKind::NoArg},
        {changegroupactive, parse_group_active_dispatch, ActionPayloadKind::Int},
        {setlayout, parse_setlayout_dispatch, ActionPayloadKind::SetLayoutDispatch},
        {togglegroup, parse_noarg_dispatch, ActionPayloadKind::NoArg},
        {groupmode, parse_groupmode_dispatch, ActionPayloadKind::Int},
    }};

    [[nodiscard]] static ActionCommand             make_action_command(KeyFn fn, ActionPayloadKind kind, ActionPayload&& payload) {
        ActionCommand out{};
        out.fn      = fn;
        out.kind    = kind;
        out.payload = std::move(payload);
        return out;
    }

    void execute_action_command(const ActionCommand& cmd) {
        if (!cmd.fn)
            return;
        cmd.fn(&cmd);
    }

    [[nodiscard]] ParseCommandResult parse_action_command(KeyFn fn, const std::string& argstr) {
        const std::string      trimmed = wm::config::parse::trim(argstr);
        const std::string_view s       = trimmed;

        for (const auto& entry : kActionParsers) {
            if (entry.fn != fn)
                continue;
            auto parsed = entry.parser(s);
            if (!parsed)
                return std::unexpected(parsed.error());
            return make_action_command(fn, entry.kind, std::move(*parsed));
        }
        return std::unexpected("unsupported action parser for callback");
    }

} /* namespace wm::config::parse */
