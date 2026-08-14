/*
 * Workspace boundary payload decode helpers.
 * Performs safe ActionCommand -> WorkspaceArgPayload extraction.
 *
 * Implementation notes:
 * - Fast boundary checks only (kind/variant).
 * - Payload is copied out once and normalized to canonical WorkspaceRef.
 * - No allocations beyond std::optional/value copy.
 *
 * Safety model:
 * - Invalid parser payloads are treated as non-fatal no-op at boundary.
 * - Execution logic never sees malformed WorkspaceDispatch variants.
 */
#include "actions/workspace_boundary_dispatch.hpp"
#include "wm_state.hpp"

namespace wm::actions::workspace_boundary {

    std::optional<WorkspaceArgPayload> workspace_payload(const wm::config::parse::ActionCommand* arg) noexcept {
        /* Boundary guard: require workspace dispatch payload kind. */
        if (!arg || arg->kind != wm::config::parse::ActionPayloadKind::WorkspaceDispatch) [[unlikely]]
            return std::nullopt;
        const auto* payload = std::get_if<wm::config::parse::WorkspaceDispatchPayload>(&arg->payload);
        if (!payload) [[unlikely]]
            return std::nullopt;
        /* Normalize special refs so execution code works with canonical workspace identities. */
        WorkspaceArgPayload out = payload->payload;
        out.ref                 = workspace_normalize_special_ref_with_hidden_id(out.ref, out.hidden_id);
        return out;
    }

} // namespace wm::actions::workspace_boundary
