/* Client property reads and EWMH / WM-hints sync implementation. */
#include "client/client_props.hpp"

#include "bsp/add_flow.hpp"
#include "client/client_lifecycle.hpp"
#include "config.hpp"
#include "intern.hpp"
#include "render.hpp"
#include "state/runtime_authority.hpp"
#include "state/wm_session.hpp"
#include "workspace_registry.hpp"
#include "x11/color_utils.hpp"
#include "x11/connection.hpp"
#include "x11/wm_input.hpp"
#include "x11/wm_ops.hpp"
#include "x11/wm_props.hpp"
#include "x11/wm_window.hpp"

#include <X11/Xatom.h>

#include <string>
#include <iterator>
#include <algorithm>
#include <cstring>

#ifndef VERSION
#define VERSION "dev"
#endif

namespace {
    /* Return non-zero when atom list property contains target atom. */
    int hasatomprop(Client* c, Atom prop, Atom atom) {
        if (!c || atom == None)
            return 0;
        const auto bytes = wm::x11::read_property(static_cast<xcb_window_t>(c->win), static_cast<xcb_atom_t>(prop), 0U, 128U, false, XCB_ATOM_ATOM);
        if (!bytes || bytes->format != 32 || bytes->data.empty())
            return 0;
        const auto* vals = reinterpret_cast<const std::uint32_t*>(bytes->data.data());
        const auto  n    = bytes->data.size() / sizeof(std::uint32_t);
        for (std::size_t i = 0; i < n; ++i) {
            if (static_cast<Atom>(vals[i]) == atom)
                return 1;
        }
        return 0;
    }

    /* Read a NUL-terminated UTF-8/Latin1 property into `text` (at most `size` bytes). */
    int gettextprop(Window w, Atom atom, char* text, unsigned int size) {
        if (!text || size == 0)
            return 0;
        text[0]         = '\0';
        const auto name = wm::x11::read_text_property(static_cast<xcb_window_t>(w), static_cast<xcb_atom_t>(atom));
        if (!name || name->value.empty())
            return 0;
        const std::size_t copy_len = std::min<std::size_t>(name->value.size(), static_cast<std::size_t>(size - 1U));
        std::memcpy(text, name->value.data(), copy_len);
        text[copy_len] = '\0';
        return 1;
    }

    /* Read a NUL-terminated property into `out`. */
    int gettextprop_string(Window w, Atom atom, std::string& out) {
        char buf[512];
        if (!gettextprop(w, atom, buf, sizeof buf))
            return 0;
        out = buf;
        return 1;
    }
}

/* Apply _NET_WM_WINDOW_OPACITY for a client window. */
void set_client_window_opacity(Client* c, double opv) {
    xcb_connection_t* const conn = wm::x11::connection();
    if (!conn)
        return;
    if (opv > 0.0 && opv < 1.0) {
        const uint32_t value = static_cast<uint32_t>(opv * static_cast<double>(0xffffffffu));
        xcb_change_property(conn, XCB_PROP_MODE_REPLACE, static_cast<xcb_window_t>(c->win), static_cast<xcb_atom_t>(wm::x11::net_atom(NetWMWindowsOpacity)), XCB_ATOM_CARDINAL, 32,
                            1, &value);
    } else {
        xcb_delete_property(conn, static_cast<xcb_window_t>(c->win), static_cast<xcb_atom_t>(wm::x11::net_atom(NetWMWindowsOpacity)));
    }
    zestwm_flush_connection();
}

/* Apply per-workspace border size / border:bool rules to a normal client. */
void apply_client_workspace_border_policy(Client* c) {
    if (!c || c->isdock)
        return;
    const WorkspaceMeta meta = workspace_registry_effective_meta(client_workspace_normal_id(c), c->mon);
    unsigned            bw   = g_config.borderpx;
    if (meta.rule_border_size)
        bw = *meta.rule_border_size;
    if (meta.rule_draw_border.has_value() && !meta.rule_draw_border.value())
        bw = 0U;
    if (static_cast<unsigned int>(c->bw) == bw)
        return;
    c->bw = static_cast<int>(bw);
    wm::x11::WindowConfigure wc{};
    wc.border_width = static_cast<unsigned int>(c->bw);
    wm::x11::configure_window(c->win, CWBorderWidth, wc);
}

Atom getatomprop(Client* c, Atom prop) {
    const auto bytes = wm::x11::read_property(static_cast<xcb_window_t>(c->win), static_cast<xcb_atom_t>(prop), 0U, 1U, false, XCB_ATOM_ATOM);
    if (!bytes || bytes->data.size() < sizeof(std::uint32_t) || bytes->format != 32)
        return None;
    return static_cast<Atom>(*reinterpret_cast<const std::uint32_t*>(bytes->data.data()));
}

long getstate(Window w) {
    const auto bytes =
        wm::x11::read_property(static_cast<xcb_window_t>(w), static_cast<xcb_atom_t>(wm::x11::wm_atom(WMState)), 0U, 2U, false, static_cast<xcb_atom_t>(wm::x11::wm_atom(WMState)));
    if (!bytes || bytes->format != 32 || bytes->data.size() < sizeof(std::uint32_t))
        return -1;
    return static_cast<long>(*reinterpret_cast<const std::uint32_t*>(bytes->data.data()));
}

void grabbuttons(Client* c, int focused) {
    if (!c || c->win == None)
        return;
    updatenumlockmask();
    {
        unsigned int i, j;
        unsigned int modifiers[] = {0, LockMask, numlockmask, numlockmask | LockMask};
        static_cast<void>(wm::x11::ungrab_button(AnyButton, AnyModifier, c->win));
        if (!focused)
            static_cast<void>(wm::x11::grab_button(AnyButton, AnyModifier, c->win, false, kButtonEventMask, GrabModeSync, GrabModeSync, None, None));
        for (i = 0; i < g_config.buttons.size(); i++)
            if (g_config.buttons[i].click == static_cast<unsigned>(ClickTarget::ClientWindow))
                for (j = 0; j < std::size(modifiers); j++)
                    static_cast<void>(wm::x11::grab_button(g_config.buttons[i].button, g_config.buttons[i].mask | modifiers[j], c->win, false, kButtonEventMask, GrabModeAsync,
                                                           GrabModeSync, None, None));
    }
}

void setfocus(Client* c) {
    if (!c->neverfocus)
        wm::x11::set_input_focus(c->win, RevertToPointerRoot, CurrentTime);
    if (xcb_connection_t* const conn = wm::x11::connection()) {
        xcb_change_property(conn, XCB_PROP_MODE_REPLACE, static_cast<xcb_window_t>(wm::x11::root_window()), static_cast<xcb_atom_t>(wm::x11::net_atom(NetActiveWindow)),
                            XCB_ATOM_WINDOW, 32, 1, &c->win);
        zestwm_flush_connection();
    }
    sendevent(c, wm::x11::wm_atom(WMTakeFocus));
}

void seturgent(Client* c, int urg) {
    auto wmh = wm::x11::read_wm_hints(static_cast<xcb_window_t>(c->win));
    if (!wmh)
        return;
    c->isurgent = urg;
    if (urg)
        wmh->flags |= static_cast<std::uint32_t>(XUrgencyHint);
    else
        wmh->flags &= ~static_cast<std::uint32_t>(XUrgencyHint);
    static_cast<void>(wm::x11::write_wm_hints(static_cast<xcb_window_t>(c->win), *wmh));
}

void unfocus(Client* c, int setfocus_flag) {
    if (!c)
        return;
    grabbuttons(c, 0);
    wm::x11::set_window_border(c->win,
                               wm::x11::resolve_x11_pixel(wm::x11::connection(), wm::x11::default_screen()->default_colormap,
                                                          scheme[static_cast<std::size_t>(SchemeKind::Normal)][static_cast<std::size_t>(ColorSlot::Border)]));
    /* Floating dialogs/popups must stay opaque: picom inactive-opacity + blur otherwise shows the desktop through them. */
    if (!c->isfloating)
        set_client_window_opacity(c, g_config.inactiveopacity);
    if (setfocus_flag) {
        wm::x11::set_input_focus(wm::x11::root_window(), RevertToPointerRoot, CurrentTime);
        if (xcb_connection_t* const conn = wm::x11::connection()) {
            xcb_delete_property(conn, static_cast<xcb_window_t>(wm::x11::root_window()), static_cast<xcb_atom_t>(wm::x11::net_atom(NetActiveWindow)));
            zestwm_flush_connection();
        }
    }
}

void updatefloatingclientlist(void) {
    xcb_connection_t* const conn = wm::x11::connection();
    if (!conn)
        return;
    const Window root_win = wm::x11::root_window();
    xcb_delete_property(conn, static_cast<xcb_window_t>(root_win), static_cast<xcb_atom_t>(wm::x11::net_atom(NetZestFloatingClients)));
    for (Monitor* m : wm::state::all_monitors()) {
        for (Client* list_c : m->clients) {
            if (!list_c->isfloating)
                continue;
            xcb_change_property(conn, XCB_PROP_MODE_APPEND, static_cast<xcb_window_t>(root_win), static_cast<xcb_atom_t>(wm::x11::net_atom(NetZestFloatingClients)),
                                XCB_ATOM_WINDOW, 32, 1, &list_c->win);
        }
    }
    zestwm_flush_connection();
}

void updateclientlist(void) {
    xcb_connection_t* const conn = wm::x11::connection();
    if (!conn)
        return;
    const Window root_win = wm::x11::root_window();
    xcb_delete_property(conn, static_cast<xcb_window_t>(root_win), static_cast<xcb_atom_t>(wm::x11::net_atom(NetClientList)));
    for (Monitor* m : wm::state::all_monitors())
        for (Client* list_c : m->clients)
            xcb_change_property(conn, XCB_PROP_MODE_APPEND, static_cast<xcb_window_t>(root_win), static_cast<xcb_atom_t>(wm::x11::net_atom(NetClientList)), XCB_ATOM_WINDOW, 32, 1,
                                &list_c->win);
    updatefloatingclientlist();
}

void updatesizehints(Client* c) {
    const auto size = wm::x11::read_size_hints(static_cast<xcb_window_t>(c->win));
    if (!size) {
        c->basew = c->baseh = 0;
        c->incw = c->inch = 0;
        c->maxw = c->maxh = 0;
        c->minw = c->minh = 0;
        c->maxa = c->mina = 0.0;
        c->isfixed        = 0;
        c->hintsvalid     = 1;
        return;
    }

    if (size->flags & PBaseSize) {
        c->basew = size->base_width;
        c->baseh = size->base_height;
    } else if (size->flags & PMinSize) {
        c->basew = size->min_width;
        c->baseh = size->min_height;
    } else
        c->basew = c->baseh = 0;
    if (size->flags & PResizeInc) {
        c->incw = size->width_inc;
        c->inch = size->height_inc;
    } else
        c->incw = c->inch = 0;
    if (size->flags & PMaxSize) {
        c->maxw = size->max_width;
        c->maxh = size->max_height;
    } else
        c->maxw = c->maxh = 0;
    if (size->flags & PMinSize) {
        c->minw = size->min_width;
        c->minh = size->min_height;
    } else if (size->flags & PBaseSize) {
        c->minw = size->base_width;
        c->minh = size->base_height;
    } else
        c->minw = c->minh = 0;
    if (size->flags & PAspect) {
        c->mina = static_cast<float>(size->min_aspect.y) / static_cast<float>(size->min_aspect.x);
        c->maxa = static_cast<float>(size->max_aspect.x) / static_cast<float>(size->max_aspect.y);
    } else
        c->maxa = c->mina = 0.0;
    c->isfixed    = (c->maxw && c->maxh && c->maxw == c->minw && c->maxh == c->minh);
    c->hintsvalid = 1;
}

void updatestatus(void) {
    if (!gettextprop_string(wm::x11::root_window(), XA_WM_NAME, wm::state::session().status_bar_text))
        wm::state::session().status_bar_text = std::string("zestwm-") + VERSION;
}

void updatetitle(Client* c) {
    if (!gettextprop_string(c->win, wm::x11::net_atom(NetWMName), c->name))
        gettextprop_string(c->win, XA_WM_NAME, c->name);
    if (c->name.empty())
        c->name = std::string(wm::state::WmSession::kBrokenClientLabel);
}

void updatewindowtype(Client* c) {
    int       isfs            = hasatomprop(c, wm::x11::net_atom(NetWMState), wm::x11::net_atom(NetWMFullscreen));
    Atom      wtype           = getatomprop(c, wm::x11::net_atom(NetWMWindowType));
    int       wasdock         = c->isdock;
    const int suppress_app_fs = client_wm_should_suppress_application_fullscreen(c);
    const int modal_ewmh =
        (wtype == wm::x11::net_atom(NetWMWindowTypeDialog) || wtype == wm::x11::net_atom(NetWMWindowTypeSplash) || wtype == wm::x11::net_atom(NetWMWindowTypeUtility)) ? 1 : 0;

    /* Splash/dialog/utility (and transients) often carry _NET_WM_STATE_FULLSCREEN; keep float overlay, not WM fullscreen. */
    if (isfs && !suppress_app_fs)
        setfullscreen(c, 1);
    else if (c->isfullscreen)
        setfullscreen(c, 0);
    if (wtype == wm::x11::net_atom(NetWMWindowTypeDock)) {
        c->isfloating = 1;
        c->isdock     = 1;
        c->neverfocus = 1;
    } else if (wtype != None) {
        c->isdock = 0;
    }
    /* EWMH modal-ish types (GTK/Qt splash and tool windows often use SPLASH/UTILITY, not DIALOG). */
    if (modal_ewmh)
        c->isfloating = 1;
    if (!wasdock && c->isdock && c->leaf) {
        bsp_remove_client(c);
        arrange(c->mon);
    }
    if (c->isfloating && c->leaf && !c->isdock) {
        bsp_remove_client(c);
        arrange(c->mon);
    }
    if (suppress_app_fs && c->isfullscreen)
        setfullscreen(c, 0);
    updatefloatingclientlist();
}

void updatewmhints(Client* c) {
    Monitor*   current = wm::state::runtime_authority().ref_current_monitor();
    const auto wmh     = wm::x11::read_wm_hints(static_cast<xcb_window_t>(c->win));
    if (!wmh)
        return;
    if (current && c == current->sel && (wmh->flags & static_cast<std::uint32_t>(XUrgencyHint)) != 0U) {
        wm::x11::WmHints cleared = *wmh;
        cleared.flags &= ~static_cast<std::uint32_t>(XUrgencyHint);
        static_cast<void>(wm::x11::write_wm_hints(static_cast<xcb_window_t>(c->win), cleared));
    } else
        c->isurgent = (wmh->flags & static_cast<std::uint32_t>(XUrgencyHint)) != 0U ? 1 : 0;
    if (wmh->flags & static_cast<std::uint32_t>(InputHint))
        c->neverfocus = wmh->input ? 0 : 1;
    else
        c->neverfocus = 0;
}

Client* wintoclient(Window w) {
    return wm::state::find_client(w);
}
