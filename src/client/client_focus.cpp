/* Client focus selection and monitor restack (EWMH active window + overlay stacking). */
#include "client/client_focus.hpp"

#include "bsp/add_flow.hpp"
#include "monitor/monitor_model.hpp"
#include "client/client_lifecycle.hpp"
#include "client/client_props.hpp"
#include "config.hpp"
#include "draw/bar.hpp"
#include "intern.hpp"
#include "render.hpp"
#include "state/runtime_authority.hpp"
#include "wm_state.hpp"
#include "x11/backend.hpp"
#include "x11/color_utils.hpp"
#include "x11/connection.hpp"
#include "x11/wm_ops.hpp"
#include "x11/wm_pointer.hpp"
#include "x11/wm_window.hpp"

/* Select focused client on the current monitor; updates borders, stack, tree focus, and _NET_ACTIVE_WINDOW. */
void focus(Client* c) {
    Monitor*& current_ref = wm::state::runtime_authority().ref_current_monitor();
    Client*&  lastfocused = wm::state::runtime_authority().ref_last_focused();
    Monitor*  current     = current_ref;
    if (!current)
        return;
    if (!c || !client_is_visible(c) || c->neverfocus) {
        c = nullptr;
        for (Client* candidate : current->stack) {
            if (client_is_visible(candidate) && !candidate->neverfocus) {
                c = candidate;
                break;
            }
        }
    }
    if (current->sel && current->sel != c) {
        unfocus(current->sel, 0);
        lastfocused = current->sel;
    }
    if (c) {
        if (c->mon != current) {
            current_ref = c->mon;
            current     = c->mon;
        }
        if (c->isurgent)
            seturgent(c, 0);
        client_unlink_stack(c->mon, c);
        client_link_stack(c->mon, c);
        grabbuttons(c, 1);
        wm::x11::set_window_border(c->win,
                                   wm::x11::resolve_x11_pixel(wm::x11::connection(), wm::x11::default_screen()->default_colormap,
                                                              scheme[static_cast<std::size_t>(SchemeKind::Selected)][static_cast<std::size_t>(ColorSlot::Border)]));
        if (lastfocused && lastfocused != c)
            wm::x11::set_window_border(lastfocused->win,
                                       wm::x11::resolve_x11_pixel(wm::x11::connection(), wm::x11::default_screen()->default_colormap,
                                                                  scheme[static_cast<std::size_t>(SchemeKind::Normal)][static_cast<std::size_t>(ColorSlot::Border)]));
        lastfocused = nullptr;
        setfocus(c);
        bsp_focus_client(c);
        set_client_window_opacity(c, g_config.activeopacity);
    } else {
        if (lastfocused)
            wm::x11::set_window_border(lastfocused->win,
                                       wm::x11::resolve_x11_pixel(wm::x11::connection(), wm::x11::default_screen()->default_colormap,
                                                                  scheme[static_cast<std::size_t>(SchemeKind::Normal)][static_cast<std::size_t>(ColorSlot::Border)]));
        lastfocused = nullptr;
        wm::x11::set_input_focus(wm::x11::root_window(), RevertToPointerRoot, CurrentTime);
        {
            if (xcb_connection_t* const conn = wm::x11::connection()) {
                xcb_delete_property(conn, static_cast<xcb_window_t>(wm::x11::root_window()), static_cast<xcb_atom_t>(wm::x11::net_atom(NetActiveWindow)));
                zestwm_flush_connection();
            }
        }
    }
    current->sel = c;
    drawtab(current);
}

/* Re-stack tiled/floating/dock/special-overlay windows after focus or layout changes. */
void restack(Monitor* m, X11Backend& backend) {
    wm::x11::WindowConfigure wc;
    int                      has_visible_fullscreen = 0;

    if (m->sel && !m->sel->neverfocus && (m->sel->isfloating || !monitor_arrange_fn(m)))
        wm::x11::raise_window(m->sel->win);
    if (monitor_arrange_fn(m)) {
        wc.stack_mode = Below;
        for (Client* stack_c : m->stack) {
            if (!stack_c->isfloating && !stack_c->isdock && client_is_visible(stack_c))
                wm::x11::configure_window(stack_c->win, CWStackMode, wc);
        }
    }
    for (Client* stack_c : m->stack) {
        if (client_is_visible(stack_c) && stack_c->isfullscreen) {
            has_visible_fullscreen = 1;
            break;
        }
    }
    if (!has_visible_fullscreen) {
        wc.stack_mode = Above;
        for (Client* stack_c : m->stack) {
            if (stack_c->isdock && client_is_visible(stack_c))
                wm::x11::configure_window(stack_c->win, CWStackMode, wc);
        }
    }
    /* Paint first, then stack overlay so a lone special client is not raised above the bar before `drawtab` runs. */
    drawtab(m);
    if (m->special_overlay_open) {
        if (m->special_dimwin)
            wm::x11::raise_window(m->special_dimwin);
        std::vector<Client*> overlay_clients;
        overlay_clients.reserve(16U);
        for (Client* stack_c : m->stack) {
            if (!client_is_visible(stack_c) || stack_c->isdock)
                continue;
            if (client_on_open_special_overlay_tag(stack_c, m))
                overlay_clients.push_back(stack_c);
        }
        /* `m->stack` is newest-first; raise oldest->newest so fresh dialogs/popups stay top-most. */
        for (auto it = overlay_clients.rbegin(); it != overlay_clients.rend(); ++it)
            wm::x11::raise_window((*it)->win);
        /* Tab bar last so it stays above dim + overlay clients for paint and clicks. */
        for (const GroupbarSlot& slot : m->groupbars) {
            if (slot.win)
                wm::x11::raise_window(slot.win);
        }
    }
    wm::x11::sync(false);
    for (;;) {
        auto d = backend.poll_masked_event(static_cast<uint32_t>(EnterWindowMask));
        if (!d)
            break;
    }
}

/* TLS fallback for call sites without an explicit backend reference. */
void restack(Monitor* m) {
    if (X11Backend* backend = x11_backend_peek_context())
        restack(m, *backend);
}
