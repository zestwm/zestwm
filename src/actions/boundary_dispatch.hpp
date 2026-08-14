/*
 * Boundary dispatcher helpers for action entrypoints.
 * Centralizes ActionCommand payload decoding at boundaries.
 *
 * Purpose:
 * - Keep parser-oriented decoding logic out of execution files (`actions.cpp`, `actions/workspace.cpp`).
 * - Expose small, deterministic helpers used by boundary wrapper entrypoints.
 *
 * Ownership/lifetime contract:
 * - Returned pointers (`Layout*`, `LayoutMsgPayload*`) and `vector*` for spawn are BORROWED from ActionCommand payload storage.
 * - Helpers do not allocate, free, or extend payload lifetime.
 * - Callers must consume returned pointers immediately in the same action-dispatch flow.
 *
 * Fallback contract:
 * - When payload type does not match expected variant, helpers return the provided fallback
 *   (`command_int`, `command_float`) or nullptr (`command_layout`, `command_spawn_args`, `command_layoutmsg_payload`).
 * - Helpers are noexcept and side-effect free.
 *
 * Context boundary contract:
 * - Pass explicit `WMState` from `wm::state::build_runtime_state_root()` at boundaries that dispatch nested events.
 */
#pragma once

#include "config/parse/action.hpp"
#include "layoutmsg.hpp"

#include <string>
#include <vector>

namespace wm::actions::boundary {

    /* Decode integer payload from ActionCommand; returns fallback on mismatch. */
    [[nodiscard]] int command_int(const wm::config::parse::ActionCommand* cmd, int fallback = 0) noexcept;
    /* Decode float payload from ActionCommand; returns fallback on mismatch. */
    [[nodiscard]] float command_float(const wm::config::parse::ActionCommand* cmd, float fallback = 0.0F) noexcept;
    /* Decode layout pointer payload from ActionCommand; returns nullptr on mismatch. */
    [[nodiscard]] const Layout* command_layout(const wm::config::parse::ActionCommand* cmd) noexcept;
    /* Decode spawn args payload from ActionCommand; returns nullptr on mismatch. */
    [[nodiscard]] const std::vector<std::string>* command_spawn_args(const wm::config::parse::ActionCommand* cmd) noexcept;
    /* Decode layoutmsg payload from ActionCommand; returns nullptr on mismatch. */
    [[nodiscard]] const LayoutMsgPayload* command_layoutmsg_payload(const wm::config::parse::ActionCommand* cmd) noexcept;
} // namespace wm::actions::boundary
