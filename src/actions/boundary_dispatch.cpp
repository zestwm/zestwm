/*
 * Boundary dispatcher helpers for action entrypoints.
 * Keeps parser payload decoding and current-monitor context construction out of action logic files.
 *
 * Implementation notes:
 * - All helpers are intentionally tiny and branch-only for predictable behavior.
 * - No heap allocations, no ownership transfer, no mutation of ActionCommand payloads.
 * - Type checks rely on ActionPayloadKind tag before variant extraction.
 *
 * Safety model:
 * - Wrong-kind payloads are treated as non-fatal boundary mismatches and mapped to fallback/null.
 * - This prevents parser/runtime boundary noise from leaking into execution logic paths.
 */
#include "actions/boundary_dispatch.hpp"

namespace wm::actions::boundary {

    int command_int(const wm::config::parse::ActionCommand* cmd, const int fallback) noexcept {
        /* Boundary decode: integer payload or fallback. */
        if (!cmd || cmd->kind != wm::config::parse::ActionPayloadKind::Int)
            return fallback;
        if (const auto* payload = std::get_if<wm::config::parse::IntPayload>(&cmd->payload))
            return payload->value;
        return fallback;
    }

    float command_float(const wm::config::parse::ActionCommand* cmd, const float fallback) noexcept {
        /* Boundary decode: float payload or fallback. */
        if (!cmd || cmd->kind != wm::config::parse::ActionPayloadKind::Float)
            return fallback;
        if (const auto* payload = std::get_if<wm::config::parse::FloatPayload>(&cmd->payload))
            return payload->value;
        return fallback;
    }

    const Layout* command_layout(const wm::config::parse::ActionCommand* cmd) noexcept {
        /* Boundary decode: layout payload or nullptr. */
        if (!cmd || cmd->kind != wm::config::parse::ActionPayloadKind::SetLayoutDispatch)
            return nullptr;
        if (const auto* payload = std::get_if<wm::config::parse::SetLayoutDispatchPayload>(&cmd->payload))
            return payload->layout;
        return nullptr;
    }

    const std::vector<std::string>* command_spawn_args(const wm::config::parse::ActionCommand* cmd) noexcept {
        /* Boundary decode: spawn args payload or nullptr. */
        if (!cmd || cmd->kind != wm::config::parse::ActionPayloadKind::SpawnArgv)
            return nullptr;
        if (const auto* payload = std::get_if<wm::config::parse::SpawnArgvPayload>(&cmd->payload))
            return &payload->args;
        return nullptr;
    }

    const LayoutMsgPayload* command_layoutmsg_payload(const wm::config::parse::ActionCommand* cmd) noexcept {
        /* Boundary decode: layoutmsg payload or nullptr. */
        if (!cmd || cmd->kind != wm::config::parse::ActionPayloadKind::LayoutMsgDispatch)
            return nullptr;
        if (const auto* payload = std::get_if<wm::config::parse::LayoutMsgDispatchPayload>(&cmd->payload))
            return &payload->payload;
        return nullptr;
    }

} // namespace wm::actions::boundary
