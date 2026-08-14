/*
 * Key/button command handlers (implemented in actions.cpp).
 *
 * Scope:
 * - Dispatcher entrypoints bound from config parser/runtime binds.
 * - Workspace/group/layout/focus/mouse actions used by interactive WM paths.
 *
 * Execution API is WMState / MonitorState-first; X11 event dispatch passes WMState + X11Backend to `dispatch_event`.
 */
#pragma once

#include "layoutmsg.hpp"
#include "types.hpp"

namespace wm::state {
    struct MonitorState;
    struct WMState;
}

namespace wm::config::parse {
    struct ActionCommand;
}

void spawn(const wm::config::parse::ActionCommand* arg);
void layoutmsg(const wm::config::parse::ActionCommand* arg);
void layoutmsg(wm::state::WMState& state, const LayoutMsgPayload& payload);
void quit(const wm::config::parse::ActionCommand* arg);
/* zestctl / root property dispatch: exit without restart. */
void quit_wm_ipc_dispatch() noexcept;
void splitratio(const wm::config::parse::ActionCommand* arg);
void splitratio(wm::state::WMState& state, float payload);
void killclient(const wm::config::parse::ActionCommand* arg);
void killclient(wm::state::WMState& state);
void setlayout(const wm::config::parse::ActionCommand* arg);
void setlayout(wm::state::WMState& state, const Layout* selected);
void cyclelayout(const wm::config::parse::ActionCommand* arg);
void cyclelayout(wm::state::WMState& state, int direction);
void togglefloating(const wm::config::parse::ActionCommand* arg);
void togglefloating(wm::state::WMState& state);
void togglefullscreen(const wm::config::parse::ActionCommand* arg);
void togglefullscreen(wm::state::WMState& state);
void focusmonitor(const wm::config::parse::ActionCommand* arg);
void focusmonitor(wm::state::MonitorState& monitors, int direction);
void movetomonitor(const wm::config::parse::ActionCommand* arg);
void movetomonitor(wm::state::MonitorState& monitors, int direction);
void movegroup(const wm::config::parse::ActionCommand* arg);
void movegroup(wm::state::WMState& state, int direction);
/* client_unlink selected client from current grouped leaf. */
void moveoutofgroup(const wm::config::parse::ActionCommand* arg);
void moveoutofgroup(wm::state::WMState& state, int direction);
/* move into nearby group, else out of group, else directional window move. */
void movewindoworgroup(const wm::config::parse::ActionCommand* arg);
void movewindoworgroup(wm::state::WMState& state, int direction);
void sendtogroup(const wm::config::parse::ActionCommand* arg);
void sendtogroup(wm::state::WMState& state, int direction);
void groupmode(const wm::config::parse::ActionCommand* arg);
void groupmode(wm::state::WMState& state);
void focusurgent(const wm::config::parse::ActionCommand* arg);
void focusurgent(wm::state::WMState& state);
void focussplit(const wm::config::parse::ActionCommand* arg);
void focussplit(wm::state::WMState& state, int direction);
void cyclefocus(const wm::config::parse::ActionCommand* arg);
void cyclefocus(wm::state::WMState& state, int direction_sign_gt0_next);
void movefocus(const wm::config::parse::ActionCommand* arg);
void movefocus(wm::state::WMState& state, int direction_key_int);
void cyclenext(const wm::config::parse::ActionCommand* arg);
void cyclenext(wm::state::WMState& state);
void cycleprev(const wm::config::parse::ActionCommand* arg);
void cycleprev(wm::state::WMState& state);
void cyclegroup(const wm::config::parse::ActionCommand* arg);
void cyclegroup(wm::state::WMState& state, int direction);
void focusgroup(const wm::config::parse::ActionCommand* arg);
void focusgroup(wm::state::WMState& state, int slot);
void movemouse(const wm::config::parse::ActionCommand* arg);
void movemouse(wm::state::WMState& state);
void resizemouse(const wm::config::parse::ActionCommand* arg);
void resizemouse(wm::state::WMState& state);
void swapwindow(const wm::config::parse::ActionCommand* arg);
void swapwindow(wm::state::WMState& state, int direction);
void bringactivetotop(const wm::config::parse::ActionCommand* arg);
void bringactivetotop(wm::state::WMState& state);
void changegroupactive(const wm::config::parse::ActionCommand* arg);
void togglegroup(const wm::config::parse::ActionCommand* arg);
