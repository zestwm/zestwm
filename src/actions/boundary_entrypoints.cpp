/*
 * ActionCommand boundary entrypoints.
 * Keeps parser-facing wrappers out of actions.cpp so action files focus on execution logic.
 *
 * Design contract:
 * - This file is a strict boundary layer between parsed config/runtime commands and typed action execution.
 * - It MUST NOT contain window-management logic (focus/layout/tree mutations belong to actions.cpp).
 * - It MAY decode ActionCommand payload variants and delegate to runtime dispatcher.
 *
 * Data-flow:
 * 1) Config/bind/IPC code calls one ActionCommand* entrypoint declared in actions.hpp.
 * 2) Entry wrapper decodes payload using actions/boundary_dispatch helpers.
 * 3) Wrapper builds payload and calls runtime dispatcher with explicit WMState root.
 * 4) Dispatcher resolves boundary context from WMState and invokes execution handlers.
 *
 * Error-handling policy:
 * - Invalid/missing payloads use safe fallbacks from boundary_dispatch helpers.
 * - layoutmsg requires a non-null decoded payload and returns early otherwise.
 * - No exceptions are thrown; wrappers are thin and side-effect free except delegation.
 *
 * Maintenance rules:
 * - Keep wrappers one-way (ActionCommand -> runtime_dispatch only).
 * - Do not call parser APIs from actions.cpp for these commands.
 * - Add new ActionCommand entrypoints here first, then keep actions.cpp execution-only.
 */
#include "actions.hpp"

#include "actions/boundary_dispatch.hpp"
#include "dispatch/runtime_dispatch.hpp"
#include "state/wm_state_root.hpp"

using wm::actions::boundary::command_float;
using wm::actions::boundary::command_int;
using wm::actions::boundary::command_layout;
using wm::actions::boundary::command_layoutmsg_payload;
using wm::config::parse::ActionCommand;

static void dispatch_runtime_command(const wm::dispatch::runtime::RuntimeCommandId id, const wm::dispatch::runtime::RuntimeCommandPayload& payload = {}) {
    wm::state::WMState runtime_state = wm::state::build_runtime_state_root();
    wm::dispatch::runtime::dispatch_command(runtime_state, id, payload);
}

/* Focus and selection boundary wrappers (payload: int where required). */
void focusmonitor(const ActionCommand* arg) {
    dispatch_runtime_command(wm::dispatch::runtime::RuntimeCommandId::FocusMonitor, {.int_payload = command_int(arg)});
}

void focussplit(const ActionCommand* arg) {
    dispatch_runtime_command(wm::dispatch::runtime::RuntimeCommandId::FocusSplit, {.int_payload = command_int(arg)});
}

void cyclefocus(const ActionCommand* arg) {
    dispatch_runtime_command(wm::dispatch::runtime::RuntimeCommandId::CycleFocus, {.int_payload = command_int(arg)});
}

void movefocus(const ActionCommand* arg) {
    dispatch_runtime_command(wm::dispatch::runtime::RuntimeCommandId::MoveFocus, {.int_payload = command_int(arg)});
}

void cyclenext(const ActionCommand* arg) {
    static_cast<void>(arg);
    dispatch_runtime_command(wm::dispatch::runtime::RuntimeCommandId::CycleNext);
}

void cycleprev(const ActionCommand* arg) {
    static_cast<void>(arg);
    dispatch_runtime_command(wm::dispatch::runtime::RuntimeCommandId::CyclePrev);
}

void focusgroup(const ActionCommand* arg) {
    dispatch_runtime_command(wm::dispatch::runtime::RuntimeCommandId::FocusGroup, {.int_payload = command_int(arg, -1)});
}

void cyclegroup(const ActionCommand* arg) {
    dispatch_runtime_command(wm::dispatch::runtime::RuntimeCommandId::CycleGroup, {.int_payload = command_int(arg)});
}

void focusurgent(const ActionCommand* arg) {
    static_cast<void>(arg);
    dispatch_runtime_command(wm::dispatch::runtime::RuntimeCommandId::FocusUrgent);
}

void killclient(const ActionCommand* arg) {
    static_cast<void>(arg);
    dispatch_runtime_command(wm::dispatch::runtime::RuntimeCommandId::KillClient);
}

/* Group/window transform boundary wrappers. */
void swapwindow(const ActionCommand* arg) {
    dispatch_runtime_command(wm::dispatch::runtime::RuntimeCommandId::SwapWindow, {.int_payload = command_int(arg)});
}

void bringactivetotop(const ActionCommand* arg) {
    static_cast<void>(arg);
    dispatch_runtime_command(wm::dispatch::runtime::RuntimeCommandId::BringActiveToTop);
}

void changegroupactive(const ActionCommand* arg) {
    dispatch_runtime_command(wm::dispatch::runtime::RuntimeCommandId::CycleGroup, {.int_payload = command_int(arg)});
}

void togglegroup(const ActionCommand* arg) {
    static_cast<void>(arg);
    dispatch_runtime_command(wm::dispatch::runtime::RuntimeCommandId::GroupMode);
}

/* Layout boundary wrappers. */
void cyclelayout(const ActionCommand* arg) {
    dispatch_runtime_command(wm::dispatch::runtime::RuntimeCommandId::CycleLayout, {.int_payload = command_int(arg)});
}

void setlayout(const ActionCommand* arg) {
    dispatch_runtime_command(wm::dispatch::runtime::RuntimeCommandId::SetLayout, {.layout_payload = command_layout(arg)});
}

void splitratio(const ActionCommand* arg) {
    dispatch_runtime_command(wm::dispatch::runtime::RuntimeCommandId::SplitRatio, {.float_payload = command_float(arg)});
}

void layoutmsg(const ActionCommand* arg) {
    const auto* payload = command_layoutmsg_payload(arg);
    if (!payload)
        return;
    dispatch_runtime_command(wm::dispatch::runtime::RuntimeCommandId::LayoutMsg, {.layoutmsg_payload = payload});
}

/* Monitor routing + floating/fullscreen boundary wrappers. */
void movetomonitor(const ActionCommand* arg) {
    dispatch_runtime_command(wm::dispatch::runtime::RuntimeCommandId::MoveToMonitor, {.int_payload = command_int(arg)});
}

void togglefloating(const ActionCommand* arg) {
    static_cast<void>(arg);
    dispatch_runtime_command(wm::dispatch::runtime::RuntimeCommandId::ToggleFloating);
}

void togglefullscreen(const ActionCommand* arg) {
    static_cast<void>(arg);
    dispatch_runtime_command(wm::dispatch::runtime::RuntimeCommandId::ToggleFullscreen);
}

/* Group movement boundary wrappers. */
void groupmode(const ActionCommand* arg) {
    static_cast<void>(arg);
    dispatch_runtime_command(wm::dispatch::runtime::RuntimeCommandId::GroupMode);
}

void movegroup(const ActionCommand* arg) {
    dispatch_runtime_command(wm::dispatch::runtime::RuntimeCommandId::MoveGroup, {.int_payload = command_int(arg)});
}

void sendtogroup(const ActionCommand* arg) {
    dispatch_runtime_command(wm::dispatch::runtime::RuntimeCommandId::SendToGroup, {.int_payload = command_int(arg)});
}

void moveoutofgroup(const ActionCommand* arg) {
    dispatch_runtime_command(wm::dispatch::runtime::RuntimeCommandId::MoveOutOfGroup, {.int_payload = command_int(arg)});
}

void movewindoworgroup(const ActionCommand* arg) {
    dispatch_runtime_command(wm::dispatch::runtime::RuntimeCommandId::MoveWindowOrGroup, {.int_payload = command_int(arg)});
}

/* Pointer-grab drag loops: build WMState once at the bind boundary; nested dispatch_event gets backend from the drag loop. */
void movemouse(const ActionCommand* arg) {
    static_cast<void>(arg);
    wm::state::WMState runtime_state = wm::state::build_runtime_state_root();
    movemouse(runtime_state);
}

void resizemouse(const ActionCommand* arg) {
    static_cast<void>(arg);
    wm::state::WMState runtime_state = wm::state::build_runtime_state_root();
    resizemouse(runtime_state);
}
