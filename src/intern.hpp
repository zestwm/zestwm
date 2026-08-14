/* Internal entry points shared between zestwm.cpp and actions.cpp. */
#pragma once

#include "types.hpp"
#include "wm_state.hpp"
#include "workspace_ref.hpp"
#include "x11/constants.hpp"

#include <optional>

void resize(Client* c, int x, int y, int w, int h, int interact);
void setclientworkspaceprop(Client* c);
void arrange(Monitor* m, bool restack_after = true);
void monitor_set_active_workspace_id(Monitor* m, WorkspaceId id);
int  monitor_workspace_contains_id(const Monitor* m, WorkspaceId id);
int  count_clients_on_workspace(WorkspaceId id);
int  client_is_visible_on_monitor(const Client* c, const Monitor* m);

/* Shared non-owning visibility helper for client call sites across modules. */
[[nodiscard]] constexpr inline bool client_is_visible(const Client* c) noexcept {
    return c && (c->isdock || client_is_visible_on_monitor(c, c->mon));
}

/* Full outer width/height of a client including its border. */
[[nodiscard]] constexpr inline int client_outer_width(const Client* c) noexcept {
    return c->w + 2 * c->bw;
}
[[nodiscard]] constexpr inline int client_outer_height(const Client* c) noexcept {
    return c->h + 2 * c->bw;
}

/* Strip caps/numlock from a modifier mask and keep only the meaningful modifier bits. */
[[nodiscard]] inline unsigned int cleanmask(unsigned int mask) noexcept {
    return mask & ~(numlockmask | LockMask) & (ShiftMask | ControlMask | Mod1Mask | Mod2Mask | Mod3Mask | Mod4Mask | Mod5Mask);
}

/* X11 event mask constants for pointer/button grabbing. */
inline constexpr unsigned int kButtonEventMask = ButtonPressMask | ButtonReleaseMask;
inline constexpr unsigned int kMouseEventMask  = kButtonEventMask | PointerMotionMask;

int                           sendevent(Client* c, Atom proto);
void                          setfullscreen(Client* c, int fullscreen);
Monitor*                      recttomon_from_fallback(int x, int y, int w, int h, Monitor* fallback);
Monitor*                      wintomon_from_fallback(Window w, Monitor* fallback);
void                          sendmon(Client* c, Monitor* m);
int                           getrootptr(int* x, int* y);
extern int                    startup_restore_pending;
void                          ensure_workspace_registry_for_id(WorkspaceId id);
void                          configure(Client* c);
void                          clamp_client_to_monitor_area(Client* c, bool use_workarea);
void                          update_special_dimwin(Monitor* m);
void                          resizeclient_fullscreen_target(Client* c);
void                          updatenumlockmask(void);
Monitor*                      wintomon(Window w);
std::optional<WorkspaceRef>   consume_special_dispatch_workspace_ref();
