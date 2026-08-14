/*
 * Runtime dispatcher command execution implementation.
 * Maps RuntimeCommandId -> action handlers with explicit WMState; routing only (queue in runtime_dispatch.cpp).
 * No `dispatch_*` passthrough layer — guard logic lives in this switch only.
 */
#include "dispatch/runtime_dispatch_commands.hpp"

#include "actions.hpp"
#include "actions/workspace.hpp"
#include "state/wm_state_root.hpp"

namespace wm::dispatch::runtime {

    void dispatch_command_execute(state::WMState& state, const RuntimeCommandId id, const RuntimeCommandPayload& payload) {
        switch (id) {
            case RuntimeCommandId::FocusMonitor: focusmonitor(state.monitors, payload.int_payload); break;
            case RuntimeCommandId::FocusSplit: focussplit(state, payload.int_payload); break;
            case RuntimeCommandId::FocusGroup: focusgroup(state, payload.int_payload); break;
            case RuntimeCommandId::CycleGroup: cyclegroup(state, payload.int_payload); break;
            case RuntimeCommandId::FocusUrgent: focusurgent(state); break;
            case RuntimeCommandId::KillClient: killclient(state); break;
            case RuntimeCommandId::SwapWindow: swapwindow(state, payload.int_payload); break;
            case RuntimeCommandId::BringActiveToTop: bringactivetotop(state); break;
            case RuntimeCommandId::CycleLayout: cyclelayout(state, payload.int_payload); break;
            case RuntimeCommandId::SetLayout: setlayout(state, payload.layout_payload); break;
            case RuntimeCommandId::SplitRatio: splitratio(state, payload.float_payload); break;
            case RuntimeCommandId::LayoutMsg:
                if (payload.layoutmsg_payload)
                    layoutmsg(state, *payload.layoutmsg_payload);
                break;
            case RuntimeCommandId::MoveToMonitor: movetomonitor(state.monitors, payload.int_payload); break;
            case RuntimeCommandId::ToggleFloating: togglefloating(state); break;
            case RuntimeCommandId::ToggleFullscreen: togglefullscreen(state); break;
            case RuntimeCommandId::GroupMode: groupmode(state); break;
            case RuntimeCommandId::MoveGroup: movegroup(state, payload.int_payload); break;
            case RuntimeCommandId::SendToGroup: sendtogroup(state, payload.int_payload); break;
            case RuntimeCommandId::MoveOutOfGroup: moveoutofgroup(state, payload.int_payload); break;
            case RuntimeCommandId::MoveWindowOrGroup: movewindoworgroup(state, payload.int_payload); break;
            case RuntimeCommandId::CycleFocus: cyclefocus(state, payload.int_payload); break;
            case RuntimeCommandId::MoveFocus: movefocus(state, payload.int_payload); break;
            case RuntimeCommandId::CycleNext: cyclenext(state); break;
            case RuntimeCommandId::CyclePrev: cycleprev(state); break;
            case RuntimeCommandId::ViewWorkspace:
                if (payload.workspace_payload.has_value() && state.workspaces.is_valid_ref(payload.workspace_payload->ref))
                    view_workspace_ref(state, payload.workspace_payload->ref);
                break;
            case RuntimeCommandId::MoveToWorkspace:
                if (payload.workspace_payload.has_value() && state.workspaces.is_valid_ref(payload.workspace_payload->ref))
                    movetoworkspace_ref(state, payload.workspace_payload->ref, payload.workspace_payload->silent);
                break;
            case RuntimeCommandId::MoveToWorkspaceSilent:
                if (payload.workspace_payload.has_value() && state.workspaces.is_valid_ref(payload.workspace_payload->ref))
                    movetoworkspace_ref(state, payload.workspace_payload->ref, true);
                break;
            case RuntimeCommandId::ToggleSpecialWorkspace:
                if (payload.workspace_payload.has_value() && payload.workspace_payload->ref.is_special() && state.workspaces.is_valid_ref(payload.workspace_payload->ref))
                    view_workspace_ref(state, payload.workspace_payload->ref);
                break;
            case RuntimeCommandId::None: break;
        }
    }

} // namespace wm::dispatch::runtime
