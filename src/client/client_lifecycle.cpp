/* Client adoption and teardown lifecycle implementation. */
#include "client/client_lifecycle.hpp"

#include "actions/workspace.hpp"
#include "bsp/add_flow.hpp"
#include "client/client_focus.hpp"
#include "client/client_props.hpp"
#include "client/group_focus.hpp"
#include "bsp/workspace_store.hpp"
#include "config/parse/window_rule.hpp"
#include "geometry.hpp"
#include "intern.hpp"
#include "render.hpp"
#include "special_workspace_registry.hpp"
#include "monitor/monitor_model.hpp"
#include "monitor/monitor_model.hpp"
#include "state/wm_session.hpp"
#include "state/wm_state_root.hpp"
#include "wm_state.hpp"
#include "workspace_registry.hpp"
#include "x11/color_utils.hpp"
#include "x11/connection.hpp"
#include "x11/wm_input.hpp"
#include "x11/wm_ops.hpp"
#include "x11/wm_pointer.hpp"
#include "x11/wm_props.hpp"
#include "x11/wm_server.hpp"
#include "x11/wm_window.hpp"

#include <memory>
#include <optional>
#include <string>

/* True for DIALOG or WM_TRANSIENT_FOR; splash/utility are excluded (noise). */
static bool client_is_modal_like(Client* c) {
    if (!c)
        return false;
    const Atom wtype = getatomprop(c, wm::x11::net_atom(NetWMWindowType));
    if (wtype == wm::x11::net_atom(NetWMWindowTypeSplash) || wtype == wm::x11::net_atom(NetWMWindowTypeUtility))
        return false;
    if (wtype == wm::x11::net_atom(NetWMWindowTypeDialog))
        return true;
    return wm::x11::read_transient_for(c->win).has_value();
}

static void maybe_select_new_client(Client* c, Monitor* current) {
    if (!c || !client_is_visible(c) || c->workspace_special_silent)
        return;
    /* Open special overlay keeps keyboard focus; ignore maps on the normal desktop underneath. */
    if (c->mon && c->mon->special_overlay_open && !client_on_open_special_overlay_tag(c, c->mon))
        return;
    if (current && c->mon == current && !c->neverfocus)
        unfocus(current->sel, 0);
    if (!c->neverfocus)
        c->mon->sel = c;
}
void sync_client_workspace_props(Client* c) {
    if (!c)
        return;
    setclientworkspaceprop(c);
}

/* function implementations */
void applyrules(Client* c) {
    std::string class_name_storage;
    const char* class_name = wm::state::WmSession::kBrokenClientLabel.data();

    /* rule matching */
    c->isfloating = 0;
    /* Keep workspace if adopt_client() preset it (e.g. WM_TRANSIENT_FOR); do not wipe before rules. */
    c->workspace_set_by_rule    = 0;
    c->workspace_rule_silent    = 0;
    c->workspace_special_silent = 0;
    c->rule_fullscreen_pending  = 0;
    c->rule_center_pending      = 0;
    if (const auto hint = wm::x11::read_class_hint(static_cast<xcb_window_t>(c->win)); hint && !hint->res_class.empty()) {
        class_name_storage = hint->res_class;
        class_name         = class_name_storage.c_str();
    }

    for (const auto& wr : wm::config::parse::window_rules) {
        if (!wm::config::parse::window_rule_entry_matches(wr, c, class_name))
            continue;
        wm::config::parse::window_rule_apply_prestack(wr, c);
    }

    /* With an open special overlay, unmatched clients inherit that tag (terminals, dialogs,
     * splash, …). Window rules and managed transient parents still win when preset above.
     * Skip inherit during startup restore: overlay open/closed comes from persisted state after scan. */
    if (c->workspace.is_unset()) {
        if (!startup_restore_pending && c->mon && c->mon->special_overlay_open) {
            const std::string tag = c->mon->special_overlay_tag;
            if (special_workspace_registry_ensure_tag(tag))
                c->workspace = workspace_normalize_special_ref_with_hidden_id(WorkspaceRef::special(tag));
            else
                c->workspace = WorkspaceRef::normal(c->mon->active_workspace_id);
        } else {
            c->workspace = WorkspaceRef::normal(c->mon ? c->mon->active_workspace_id : kWorkspaceIdMin);
        }
    }
    sync_client_workspace_props(c);
}

void client_link(Monitor* m, Client* c) {
    monitor_prepend_client(m, c);
}

void client_link_stack(Monitor* m, Client* c) {
    monitor_prepend_stack(m, c);
}
int client_wm_should_suppress_application_fullscreen(Client* c) {
    const Atom wtype = getatomprop(c, wm::x11::net_atom(NetWMWindowType));

    if (wtype == wm::x11::net_atom(NetWMWindowTypeDialog) || wtype == wm::x11::net_atom(NetWMWindowTypeSplash) || wtype == wm::x11::net_atom(NetWMWindowTypeUtility))
        return 1;
    if (wm::x11::read_transient_for(c->win))
        return 1;
    return 0;
}
void client_unlink(Monitor* m, Client* c) {
    monitor_remove_client(m, c);
}

void client_unlink_stack(Monitor* m, Client* c) {
    monitor_remove_stack(m, c);

    if (c == m->sel) {
        Client* t = nullptr;
        for (Client* entry : m->stack) {
            if (client_is_visible(entry)) {
                t = entry;
                break;
            }
        }
        m->sel = t;
    }
}

void adopt_client(wm::state::WMState& state, Window w, const wm::x11::WindowInfo& wa) {
    Client*                  t       = nullptr;
    Monitor*                 current = state.monitors.current;
    Window                   trans   = None;
    wm::x11::WindowConfigure wc;

    /* Value-initialize Client, then register as sole owner in the Window-keyed registry. */
    auto owned = std::make_unique<Client>();
    owned->win = w;
    Client* c  = wm::state::runtime_authority().register_client(std::move(owned));
    if (!c)
        return;
    /* geometry */
    c->x = c->oldx = wa.x;
    c->y = c->oldy = wa.y;
    c->w = c->oldw = wa.width;
    c->h = c->oldh = wa.height;
    c->oldbw       = wa.border_width;

    updatetitle(c);
    if (const auto transient = wm::x11::read_transient_for(w)) {
        trans = *transient;
        t     = wintoclient(trans);
        if (t) {
            c->mon       = t->mon;
            c->workspace = t->workspace;
        } else {
            c->mon = current ? current : state.monitors.first();
        }
    } else {
        c->mon = current ? current : state.monitors.first();
    }
    /* Window rules must run for transients too (e.g. float/center on portal dialogs); `applyrules` keeps preset workspace. */
    applyrules(c);
    if (const std::optional<WorkspaceRef> persisted_ws = apply_workspace_from_persistence(c, c->workspace_set_by_rule); persisted_ws) {
        c->workspace = *persisted_ws;
        if (persisted_ws->is_normal())
            c->workspace_set_by_rule = 0;
    }
    /* Modal on normal while overlay open: close scratchpad so it is not parked. */
    if (c->mon && c->mon->special_overlay_open && c->workspace.is_normal() && !c->isdock && client_is_modal_like(c)) {
        c->mon->special_overlay_open = false;
        c->mon->special_overlay_tag.clear();
    }
    apply_client_workspace_border_policy(c);

    wc.border_width = c->bw;
    wm::x11::configure_window(w, CWBorderWidth, wc);
    wm::x11::set_window_border(w,
                               wm::x11::resolve_x11_pixel(wm::x11::connection(), wm::x11::default_screen()->default_colormap,
                                                          scheme[static_cast<std::size_t>(SchemeKind::Normal)][static_cast<std::size_t>(ColorSlot::Border)]));
    configure(c); /* propagates border_width, if size doesn't change */
    updatewindowtype(c);
    /* Default-center transient/dialog-like windows unless rules or restore override later behavior. */
    if (c->rule_center_pending == 0U) {
        const Atom wtype = getatomprop(c, wm::x11::net_atom(NetWMWindowType));
        const int  dialog_like =
            (wtype == wm::x11::net_atom(NetWMWindowTypeDialog) || wtype == wm::x11::net_atom(NetWMWindowTypeSplash) || wtype == wm::x11::net_atom(NetWMWindowTypeUtility)) ? 1 : 0;
        const int transient_like = (trans != None) ? 1 : 0;
        if (dialog_like || transient_like)
            c->rule_center_pending = 1U;
    }
    if (c->isdock)
        c->bw = 0;
    updatesizehints(c);
    updatewmhints(c);
    if (c->isdock) {
        wc.border_width = 0;
        wm::x11::configure_window(w, CWBorderWidth, wc);
        wm::x11::set_window_border(w,
                                   wm::x11::resolve_x11_pixel(wm::x11::connection(), wm::x11::default_screen()->default_colormap,
                                                              scheme[static_cast<std::size_t>(SchemeKind::Normal)][static_cast<std::size_t>(ColorSlot::Border)]));
    }
    /* Clamp to work area only for normal clients; docks may sit in EWMH strut strips. */
    clamp_client_to_monitor_area(c, !c->isdock);
    sync_client_workspace_props(c);
    wm::x11::select_input(w, EnterWindowMask | FocusChangeMask | PropertyChangeMask | StructureNotifyMask);
    grabbuttons(c, 0);
    if (!c->isfloating) {
        const Atom wtype  = getatomprop(c, wm::x11::net_atom(NetWMWindowType));
        const bool dialog = (wtype == wm::x11::net_atom(NetWMWindowTypeDialog));
        c->isfloating = c->oldstate = (trans != None || c->isfixed || dialog) ? 1 : 0;
    }
    if (!c->isdock) {
        bsp_add_client(c, c->mon);
        workspace_registry_note_client_joined_workspace(client_workspace_normal_id(c));
    }
    /* Floating state + saved geometry for pre-existing windows is restored from tree-state
     * (`|F(...)` suffix) by restorezesttreestate() after scan; for a fresh map here, only the
     * transient/fixed heuristic sets isfloating. Pull out of the tree if now floating. */
    if (c->isfloating)
        bsp_remove_client(c);
    const int should_center_now = (c->rule_center_pending != 0U);
    if (c->isfloating && should_center_now && c->mon) {
        c->x = c->mon->wx + (c->mon->ww - client_outer_width(c)) / 2;
        c->y = c->mon->wy + (c->mon->wh - client_outer_height(c)) / 2;
    }
    c->rule_center_pending = 0;
    if (c->isfloating)
        wm::x11::raise_window(c->win);
    setclientworkspaceprop(c);
    client_link(c->mon, c);
    client_link_stack(c->mon, c);
    if (xcb_connection_t* const conn = wm::x11::connection()) {
        xcb_change_property(conn, XCB_PROP_MODE_APPEND, static_cast<xcb_window_t>(wm::x11::root_window()), static_cast<xcb_atom_t>(wm::x11::net_atom(NetClientList)),
                            XCB_ATOM_WINDOW, 32, 1, &c->win);
        zestwm_flush_connection();
    }
    /* Refresh desktop exports after mapping new client so bars see occupancy on never-viewed workspaces. */
    update_net_desktop_props();
    /* Tiled clients: legacy off-screen pre-map place (some toolkits expect a Configure before Map).
     * Floating/dialog: keep final on-screen geometry so Qt/wx do not paint a stale first frame off-screen. */
    if (c->isfloating)
        wm::x11::move_resize_window(c->win, c->x, c->y, c->w, c->h);
    else
        wm::x11::move_resize_window(c->win, c->x + 2 * sw, c->y, c->w, c->h);
    setclientstate(c, NormalState);
    state.monitors.current  = current;
    const int special_spawn = (!c->isdock && c->workspace.is_special()) ? 1 : 0;
    if (special_spawn) {
        /* Non-silent special maps open the overlay at runtime. During startup restore, overlay
         * visibility comes from `_NET_ZESTWM_SPECIAL_OVERLAY` after scan (do not force-open). */
        if (!startup_restore_pending && !c->workspace_special_silent && c->mon && special_workspace_registry_ensure_tag(std::string(c->workspace.special_tag)))
            wm_ensure_special_overlay_visible(c->mon, std::string(c->workspace.special_tag), true);
        else
            arrange(c->mon);
        if (!startup_restore_pending)
            maybe_select_new_client(c, wm::state::monitor_or_fallback(state));
    } else if (!startup_restore_pending) {
        if (c->workspace_set_by_rule && c->workspace.is_normal() && !c->workspace_rule_silent) {
            const bool viewed = view_workspace_id(state, c->workspace.normal_id);
            if (!viewed) {
                maybe_select_new_client(c, wm::state::monitor_or_fallback(state));
                arrange(c->mon);
            }
        } else {
            /* Do not steal focus or clobber `sel` with an off-workspace client (`workspace … silent`). */
            maybe_select_new_client(c, wm::state::monitor_or_fallback(state));
            arrange(c->mon);
        }
    }
    if (!startup_restore_pending) {
        if (c->rule_fullscreen_pending == 2U && !client_wm_should_suppress_application_fullscreen(c))
            setfullscreen(c, 1);
        else if (c->rule_fullscreen_pending == 1U)
            setfullscreen(c, 0);
        c->rule_fullscreen_pending = 0;
        if (c->isfloating) {
            clamp_client_to_monitor_area(c, true);
            resize(c, c->x, c->y, c->w, c->h, 0);
        }
    }
    wm::x11::map_window(c->win);
    /* Floating: force opaque (_NET_WM_WINDOW_OPACITY cleared). Compositors with inactive-opacity/blur
     * otherwise show the desktop through dialogs that miss focus on map. */
    if (!startup_restore_pending && c->isfloating && !c->isdock)
        set_client_window_opacity(c, 1.0);
    /* Runtime non-silent special maps reveal the overlay (startup restore uses persisted overlay state). */
    if (!startup_restore_pending && c->workspace.is_special() && c->mon && !c->workspace_special_silent)
        wm_ensure_special_overlay_visible(c->mon, c->workspace.special_tag, true);
    if (!startup_restore_pending && !c->workspace_special_silent) {
        /* `workspace … silent` bypasses apply_normal_workspace_view in the block above. First arrange+showhide can run
         * before the window is mapped, leaving the tiled client unstaged until something triggers another arrange
         * (e.g. switching workspaces). Re-arrange once the window exists when the silent rule targeted the
         * already-viewed desktop. */
        if (!c->isdock && c->workspace_rule_silent && c->workspace.is_normal() && c->mon && c->workspace.normal_id == c->mon->active_workspace_id)
            arrange(c->mon);
        /* focus(nullptr) walks only selected monitor's stack. A new client may live on another monitor
         * or lose the race to stack order, so X focus + WM_TAKE_FOCUS never reach the app. Browsers often keep a black
         * buffer until focused. Route input to the managed client when policy allows. */
        /* Maps onto the normal desktop under an open overlay must not steal scratchpad focus. */
        const bool overlay_blocks_normal = c->mon && c->mon->special_overlay_open && !client_on_open_special_overlay_tag(c, c->mon);
        if (overlay_blocks_normal) {
            /* Keep overlay focus. */
        } else if (!c->isdock && !c->neverfocus && client_is_visible(c)) {
            focus(c);
            /* Grouped/tab leaf: `arrange` above ran before `focus()`/`bsp_focus_client()` bumped this client to active;
             * otherwise inactive tab slots stay at `hide_x` until another workspace switch. Silent rules are unaffected
             * by _NET_WM activation (see NetActiveWindow) — same ordering bug applies. */
            if (c->leaf && c->leaf->type == NODE_GROUPED)
                arrange(c->mon);
        } else
            focus(nullptr);
        /* Floating dialogs often stay black until a later configure (e.g. toggling special). Pre-map arrange runs
         * while unmapped; repeat a real Configure + arrange after Map/focus so the toolkit paints. */
        if (c->isfloating && !c->isdock && c->mon && client_is_visible(c)) {
            clamp_client_to_monitor_area(c, true);
            resizeclient(c, c->x, c->y, c->w, c->h);
            configure(c);
            wm::x11::raise_window(c->win);
            c->needresize = 1;
            arrange(c->mon);
            if (!c->neverfocus && !overlay_blocks_normal)
                focus(c);
        }
    }
    updatefloatingclientlist();
}

void release_client(wm::state::WMState& state, Client* c, int destroyed) {
    Monitor*                 m = c->mon;
    Client*                  fallback_focus;
    wm::x11::WindowConfigure wc;
    const WorkspaceRef       wid_copy = c->workspace;

    fallback_focus = (m && m->sel == c) ? group_focus_client_after_remove(c) : nullptr;
    bsp_remove_client(c);
    client_unlink(m, c);
    client_unlink_stack(m, c);
    const bool special_hid = wm_special_overlay_autohide_if_empty(m);
    if (wid_copy.is_normal()) {
        const WorkspaceId wid = wid_copy.normal_id;
        if (count_clients_on_workspace(wid) == 0) {
            workspace_registry_note_workspace_became_empty(wid);
            if (!workspace_registry_is_persistent(wid))
                bsp_clear_workspace_tree_state(wid);
        }
    }
    if (!destroyed) {
        wc.border_width = c->oldbw;
        wm::x11::grab_server(); /* avoid race conditions */
        wm::x11::select_input(c->win, NoEventMask);
        wm::x11::configure_window(c->win, CWBorderWidth, wc); /* restore border */
        static_cast<void>(wm::x11::ungrab_button(AnyButton, AnyModifier, c->win));
        setclientstate(c, WithdrawnState);
        wm::x11::sync(false);
        wm::x11::ungrab_server();
    }
    if (state.focus.lastfocused == c)
        state.focus.lastfocused = nullptr;
    /* All aliases unlinked: erase sole owner (WorkspaceRef dtor runs via unique_ptr). */
    const Window win = c->win;
    c                = nullptr;
    wm::state::runtime_authority().erase_client(win);
    if (fallback_focus)
        focus(fallback_focus);
    else
        focus(nullptr);
    updateclientlist();
    arrange(m);
    if (special_hid)
        wm_focus_after_special_overlay_hidden(m);
}

void adopt_client(Window w, const wm::x11::WindowInfo& wa) {
    wm::state::WMState rt = wm::state::build_runtime_state_root();
    adopt_client(rt, w, wa);
}

void release_client(Client* c, int destroyed) {
    wm::state::WMState rt = wm::state::build_runtime_state_root();
    release_client(rt, c, destroyed);
}
