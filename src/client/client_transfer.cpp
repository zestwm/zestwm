/* Client monitor transfer and fullscreen state transitions. */
#include "intern.hpp"

#include "actions/workspace.hpp"
#include "bsp/add_flow.hpp"
#include "client/client_lifecycle.hpp"
#include "client/client_props.hpp"
#include "client/client_focus.hpp"
#include "geometry.hpp"
#include "wm_state.hpp"
#include "workspace_ref.hpp"
#include "x11/connection.hpp"
#include "x11/wm_ops.hpp"
#include "x11/wm_window.hpp"

/* Move client to another monitor: client_unlink from source BSP/stack, reattach on target active workspace. */
void sendmon(Client* c, Monitor* m) {
    if (c->mon == m)
        return;
    Monitor* const src = c->mon;
    bsp_remove_client(c);
    unfocus(c, 1);
    client_unlink(c->mon, c);
    client_unlink_stack(c->mon, c);
    c->mon       = m;
    c->workspace = WorkspaceRef::normal(m->active_workspace_id); /* assign ownership to target monitor active workspace */
    sync_client_workspace_props(c);
    apply_client_workspace_border_policy(c);
    bsp_add_client(c, m);
    client_link(c->mon, c);
    client_link_stack(c->mon, c);
    setclientworkspaceprop(c);
    if (c->isfullscreen)
        resizeclient(c, m->mx, m->my, m->mw, m->mh);
    const bool special_hid = wm_special_overlay_autohide_if_empty(src);
    focus(nullptr);
    arrange(nullptr);
    if (special_hid)
        wm_focus_after_special_overlay_hidden(src);
}

/* Enter/leave WM fullscreen: EWMH state, BSP client_unlink/reattach, and geometry restore. */
void setfullscreen(Client* c, int fullscreen) {
    const uint32_t fsatom = static_cast<uint32_t>(wm::x11::net_atom(NetWMFullscreen));

    if (fullscreen && !c->isfullscreen) {
        if (xcb_connection_t* const conn = wm::x11::connection()) {
            xcb_change_property(conn, XCB_PROP_MODE_REPLACE, static_cast<xcb_window_t>(c->win), static_cast<xcb_atom_t>(wm::x11::net_atom(NetWMState)), XCB_ATOM_ATOM, 32, 1,
                                &fsatom);
            zestwm_flush_connection();
        }
        c->fs_tile.x     = c->x;
        c->fs_tile.y     = c->y;
        c->fs_tile.w     = c->w;
        c->fs_tile.h     = c->h;
        c->fs_tile.valid = 1;
        c->isfullscreen  = 1;
        c->oldstate      = c->isfloating;
        c->oldbw         = c->bw;
        c->bw            = 0;
        /* Tiled → fullscreen via keybind/clientmessage never hit `updatewindowtype`'s treeremove; keep `leaf` set and
         * exit skips `bsp_add_client`, so tab order and tile geometry never restore. Match EWMH path: leave BSP first.
         * Set floating before `arrange` so `tree()` sole-tiled compaction does not treat this window as the only tile. */
        if (c->leaf && !c->oldstate) {
            c->isfloating = 1;
            bsp_remove_client(c);
            arrange(c->mon);
        } else
            c->isfloating = 1;
        resizeclient_fullscreen_target(c);
        setclientworkspaceprop(c);
        wm::x11::raise_window(c->win);
    } else if (!fullscreen && c->isfullscreen) {
        if (xcb_connection_t* const conn = wm::x11::connection()) {
            xcb_change_property(conn, XCB_PROP_MODE_REPLACE, static_cast<xcb_window_t>(c->win), static_cast<xcb_atom_t>(wm::x11::net_atom(NetWMState)), XCB_ATOM_ATOM, 32, 0,
                                nullptr);
            zestwm_flush_connection();
        }
        c->isfullscreen = 0;
        c->isfloating   = c->oldstate;
        c->bw           = c->oldbw;
        if (c->fs_tile.valid) {
            c->x             = c->fs_tile.x;
            c->y             = c->fs_tile.y;
            c->w             = c->fs_tile.w;
            c->h             = c->fs_tile.h;
            c->fs_tile.valid = 0;
        } else {
            c->x = c->oldx;
            c->y = c->oldy;
            c->w = c->oldw;
            c->h = c->oldh;
        }
        resizeclient(c, c->x, c->y, c->w, c->h);
        setclientworkspaceprop(c);
        /* `updatewindowtype` removes floating-ish clients from the BSP; leaving fullscreen must reattach when tiled. */
        if (!c->isfloating && !c->isdock && !c->leaf)
            bsp_add_client(c, c->mon);
        arrange(c->mon);
    }
    updatefloatingclientlist();
}
