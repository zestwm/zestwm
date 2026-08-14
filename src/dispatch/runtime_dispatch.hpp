/*
 * Runtime dispatcher boundary declarations.
 * Centralizes event/command entrypoints that mutate runtime state through explicit WMState
 * and X11Backend references.
 */
#pragma once

#include "layoutmsg.hpp"
#include "state/wm_state_root.hpp"
#include "types.hpp"
#include "x11/backend.hpp"

#include <cstdint>
#include <optional>

#include <xcb/xcb.h>

namespace wm::dispatch::runtime {

    /* Runtime command id placeholder for incremental command-boundary migration. */
    enum class RuntimeCommandId : std::uint8_t {
        None = 0U,
        FocusMonitor,
        FocusSplit,
        FocusGroup,
        CycleGroup,
        FocusUrgent,
        KillClient,
        SwapWindow,
        BringActiveToTop,
        CycleLayout,
        SetLayout,
        SplitRatio,
        LayoutMsg,
        MoveToMonitor,
        ToggleFloating,
        ToggleFullscreen,
        GroupMode,
        MoveGroup,
        SendToGroup,
        MoveOutOfGroup,
        MoveWindowOrGroup,
        CycleFocus,
        MoveFocus,
        CycleNext,
        CyclePrev,
        ViewWorkspace,
        MoveToWorkspace,
        MoveToWorkspaceSilent,
        ToggleSpecialWorkspace,
    };

    /* Runtime command payload placeholder for incremental typed command migration. */
    struct RuntimeCommandPayload {
        int                                int_payload       = 0;
        float                              float_payload     = 0.0F;
        const Layout*                      layout_payload    = nullptr;
        const LayoutMsgPayload*            layoutmsg_payload = nullptr;
        std::optional<WorkspaceArgPayload> workspace_payload = std::nullopt;
    };

    /* Dispatch one X11 runtime event through centralized boundary (state + backend). */
    void dispatch_event(state::WMState& state, X11Backend& backend, xcb_generic_event_t* ev);

    /* Dispatch one runtime command through centralized boundary. */
    void dispatch_command(state::WMState& state, RuntimeCommandId id, const RuntimeCommandPayload& payload);

} // namespace wm::dispatch::runtime
