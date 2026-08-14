/*
 * Workspace boundary payload decode helpers.
 * Keeps parser payload extraction outside workspace execution logic.
 *
 * Purpose:
 * - Provide a tiny parser-boundary API for workspace action wrappers.
 * - Keep `actions/workspace.cpp` focused on workspace behavior, not payload decoding.
 *
 * Payload contract:
 * - Input must be ActionPayloadKind::WorkspaceDispatch.
 * - Output is normalized (`workspace_normalize_special_ref_with_hidden_id`) so all
 *   downstream execution paths receive canonical WorkspaceRef values.
 *
 * Ownership/lifetime:
 * - Returned `WorkspaceArgPayload` is a value copy in std::optional.
 * - No borrowed pointers escape this API.
 *
 * Error policy:
 * - Missing command, kind mismatch, or null payload => std::nullopt.
 * - No exceptions; function is noexcept and side-effect free.
 */
#pragma once

#include "config/parse/action.hpp"

#include <optional>

namespace wm::actions::workspace_boundary {

    /* Decode and normalize workspace dispatch payload from parser command boundary.
 * Returns nullopt on invalid/missing payload. */
    [[nodiscard]] std::optional<WorkspaceArgPayload> workspace_payload(const wm::config::parse::ActionCommand* arg) noexcept;

} // namespace wm::actions::workspace_boundary
