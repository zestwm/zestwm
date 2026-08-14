/*
 * Workspace action API.
 *
 * Scope:
 * - Workspace view/move dispatchers used by parser-bound actions.
 * - Special-workspace overlay open/close/focus helpers.
 *
 * Behavior model:
 * - Normal workspaces use `WorkspaceRef::normal(id)` and update monitor active
 *   workspace state through `view_workspace_id(...)`.
 * - Special workspaces use `WorkspaceRef::special(tag)` and route through
 *   overlay semantics (`wm_ensure_special_overlay_visible`, toggle helpers).
 *
 * Notes:
 * - `movetoworkspace_ref(..., silent=true)` performs silent move:
 *   ownership changes, current view is kept.
 * - Overlay helpers are side-effecting (focus/restack/arrange) and should be
 *   called from action paths, not from pure query code.
 */
#pragma once

#include "types.hpp"

#include <string_view>

namespace wm::config::parse {
    struct ActionCommand;
}

namespace wm::state {
    struct WMState;
}

/* Resolve and view a workspace reference.
 *
 * Behavior:
 * - Normal refs are routed to `view_workspace_id(...)`.
 * - Special refs are routed to overlay toggle/show policy.
 *
 * Side effects:
 * - May change selected monitor, focus, restack, and EWMH state.
 */
void view_workspace_ref(wm::state::WMState& state, const WorkspaceRef& r);

/* View a normal workspace id on selected/resolved monitor.
 *
 * Monitor routing:
 * - If registry metadata binds this workspace to a monitor selector, the target
 *   monitor is resolved first and may become selected.
 *
 * Side effects:
 * - Updates active workspace, viewed world snapshot, optional per-workspace layout,
 *   focus policy, arrange/restack pipeline, and desktop export properties.
 *
 * Return:
 * - `false` when the request is a no-op (already active on target monitor).
 * - `true` when a state transition was applied.
 */
[[nodiscard]] bool view_workspace_id(wm::state::WMState& state, WorkspaceId workspace_id);

/* Move currently selected client to target workspace reference.
 *
 * Routing:
 * - Supports both normal workspace ids and special overlay tags.
 *
 * Silent mode:
 * - `silent=false`: may also switch viewed workspace/overlay and focus.
 * - `silent=true`: ownership changes only; current view is preserved.
 *
 * Side effects:
 * - Mutates tree membership, workspace ownership/export props, border policy,
 *   optional overlay autohide/focus recovery, and may trigger arrange/focus updates.
 */
void movetoworkspace_ref(wm::state::WMState& state, const WorkspaceRef& w, bool silent);

/* Convenience wrapper for normal workspace-id target. */
void movetoworkspace_id(wm::state::WMState& state, WorkspaceId workspace_id, bool silent = true);

/* Action payload entrypoint for `movetoworkspace`.
 *
 * Expects `WorkspaceDispatchPayload` command payload.
 * Invalid/missing payload is ignored safely (no-op).
 */
void movetoworkspaceid(const wm::config::parse::ActionCommand* arg);

/* Action payload entrypoint for `movetoworkspacesilent`.
 *
 * Same payload contract as `movetoworkspaceid`, but always forces silent mode.
 */
void movetoworkspacesilent(const wm::config::parse::ActionCommand* arg);

/* Action payload entrypoint for `workspace` / `view`.
 *
 * Expects `WorkspaceDispatchPayload` produced by typed parser command mapping.
 * Invalid payload is treated as no-op.
 */
void viewworkspace(const wm::config::parse::ActionCommand* arg);

/* Action payload entrypoint for `togglespecialworkspace`.
 *
 * Accepts typed `WorkspaceArgPayload` only.
 *
 * Non-special targets are ignored.
 */
void togglespecialworkspace(const wm::config::parse::ActionCommand* arg);

/* Ensure special overlay is visible on monitor `m` for `tag`.
 *
 * Behavior:
 * - Registers tag in special registry when missing.
 * - Opens overlay if closed, or switches overlay tag if different.
 * - Optionally runs overlay focus pick on open/switch (pointer, then sel, then first).
 *
 * Performance note:
 * - Uses single arrange pass strategy to avoid duplicate restack/configure work.
 */
void wm_ensure_special_overlay_visible(Monitor* m, std::string_view tag, bool focus_first_client = true);

/* Auto-hide special overlay when it has no remaining clients.
 *
 * Return:
 * - `true` if overlay transitioned from open -> closed.
 * - `false` when kept unchanged.
 */
[[nodiscard]] bool wm_special_overlay_autohide_if_empty(Monitor* m);

/* Apply focus policy after overlay hide.
 *
 * Policy:
 * - Prefer pointer-hit client on monitor when visible/focusable.
 * - Fallback to normal focus(nullptr) behavior otherwise.
 *
 * This method is `noexcept` by contract for hot action paths.
 */
void wm_focus_after_special_overlay_hidden(Monitor* m) noexcept;

/* Pick keyboard focus for the currently open special overlay tag.
 *
 * Policy (always, not gated on follow_mouse):
 * - Prefer pointer-hit overlay client when eligible.
 * - Else keep `m->sel` when still eligible.
 * - Else first eligible in the monitor client ring (active tab in a group).
 *
 * Eligibility: non-dock, focusable, on active overlay tag, visible on monitor.
 *
 * This method is `noexcept` by contract for hot action paths.
 */
void wm_focus_first_special_overlay_client(Monitor* m) noexcept;
