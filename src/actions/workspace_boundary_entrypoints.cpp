/*
 * Workspace ActionCommand boundary entrypoints.
 * Keeps parser-facing wrappers out of workspace execution logic file.
 *
 * Design contract:
 * - This file is parser/runtime boundary glue only.
 * - It MUST NOT implement workspace behavior semantics (that stays in workspace.cpp).
 * - It MAY decode ActionCommand payloads and delegate to runtime dispatcher.
 *
 * Data flow:
 * 1) Parser/bind/dispatch calls ActionCommand* entrypoint declared in workspace.hpp.
 * 2) Boundary wrapper decodes and normalizes WorkspaceArgPayload.
 * 3) Wrapper delegates payload to runtime dispatcher with explicit WMState root.
 * 4) Dispatcher resolves boundary context from WMState and invokes workspace execution APIs.
 *
 * Error policy:
 * - Invalid payload => early return (no-op).
 * - Non-special targets in togglespecialworkspace => ignored by design.
 *
 * Maintenance rule:
 * - Add new Workspace ActionCommand wrappers here first.
 * - Keep workspace.cpp execution-oriented and free of ActionCommand decode logic.
 */
#include "actions/workspace.hpp"

#include "actions/workspace_boundary_dispatch.hpp"
#include "dispatch/runtime_dispatch.hpp"
#include "state/wm_state_root.hpp"

using wm::actions::workspace_boundary::workspace_payload;
using wm::config::parse::ActionCommand;

static void dispatch_runtime_workspace_command(const wm::dispatch::runtime::RuntimeCommandId id, const WorkspaceArgPayload& payload) {
    wm::state::WMState runtime_state = wm::state::build_runtime_state_root();
    wm::dispatch::runtime::dispatch_command(runtime_state, id, {.workspace_payload = payload});
}

/* Move-to-workspace boundary wrappers (normal/special payload, optional silent semantics). */
void movetoworkspaceid(const ActionCommand* arg) {
    const auto p = workspace_payload(arg);
    if (!p) [[unlikely]]
        return;
    dispatch_runtime_workspace_command(wm::dispatch::runtime::RuntimeCommandId::MoveToWorkspace, *p);
}

void movetoworkspacesilent(const ActionCommand* arg) {
    const auto p = workspace_payload(arg);
    if (!p) [[unlikely]]
        return;
    dispatch_runtime_workspace_command(wm::dispatch::runtime::RuntimeCommandId::MoveToWorkspaceSilent, *p);
}

/* Special-overlay toggle boundary wrappers. */
void togglespecialworkspace(const ActionCommand* arg) {
    const auto p = workspace_payload(arg);
    if (!p) [[unlikely]]
        return;
    dispatch_runtime_workspace_command(wm::dispatch::runtime::RuntimeCommandId::ToggleSpecialWorkspace, *p);
}

/* Workspace view boundary wrappers. */
void viewworkspace(const ActionCommand* arg) {
    const auto p = workspace_payload(arg);
    if (!p) [[unlikely]]
        return;
    dispatch_runtime_workspace_command(wm::dispatch::runtime::RuntimeCommandId::ViewWorkspace, *p);
}
