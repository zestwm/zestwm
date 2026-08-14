/* Client geometry, arrange, and monitor hit-test implementation. */
#include <cmath>

#include "config.hpp"
#include "geometry.hpp"

#include "bsp/add_flow.hpp"
#include "client/client_focus.hpp"
#include "client/client_props.hpp"
#include "draw/bar.hpp"
#include "intern.hpp"
#include "state/runtime_authority.hpp"
#include "state/wm_state_root.hpp"
#include "workspace_registry.hpp"
#include "monitor/monitor_model.hpp"
#include "x11/connection.hpp"
#include "x11/wm_props.hpp"
#include "x11/wm_window.hpp"

#include <algorithm>

/* Clamp client geometry inside either monitor workarea or full monitor rectangle. */
void clamp_client_to_monitor_area(Client* c, bool use_workarea) {
    if (!c || !c->mon)
        return;
    const int bx = use_workarea ? c->mon->wx : c->mon->mx;
    const int by = use_workarea ? c->mon->wy : c->mon->my;
    const int bw = use_workarea ? static_cast<int>(c->mon->ww) : static_cast<int>(c->mon->mw);
    const int bh = use_workarea ? static_cast<int>(c->mon->wh) : static_cast<int>(c->mon->mh);
    if (c->x + client_outer_width(c) > bx + bw)
        c->x = bx + bw - client_outer_width(c);
    if (c->y + client_outer_height(c) > by + bh)
        c->y = by + bh - client_outer_height(c);
    c->x = std::max(c->x, bx);
    c->y = std::max(c->y, by);
}

/* Outer / inner tiling gaps for the monitor's active workspace (per-workspace rules override globals). */
void tiling_gaps_for_monitor_workspace(Monitor* m, unsigned* outer, unsigned* inner_split) {
    const WorkspaceId   ws   = m ? m->active_workspace_id : kWorkspaceIdMin;
    const WorkspaceMeta meta = workspace_registry_effective_meta(ws, m);
    unsigned            in   = g_config.gaps_in;
    unsigned            out  = g_config.gaps_in;
    if (meta.rule_gaps_in)
        in = *meta.rule_gaps_in;
    if (meta.rule_gaps_out)
        out = *meta.rule_gaps_out;
    else if (meta.rule_gaps_in)
        out = *meta.rule_gaps_in;
    *inner_split = in;
    *outer       = out;
}

int applysizehints(Client* c, int* x, int* y, int* w, int* h, int interact) {
    int      baseismin;
    Monitor* m = c->mon;

    /* set minimum possible */
    *w = std::max(1, *w);
    *h = std::max(1, *h);
    if (interact) {
        /* Clamp interactive moves to the client's monitor screen, not global root sw/sh. */
        const int bx = m ? m->mx : 0;
        const int by = m ? m->my : 0;
        const int bw = m ? m->mw : sw;
        const int bh = m ? m->mh : sh;
        if (*x >= bx + bw)
            *x = bx + bw - client_outer_width(c);
        if (*y >= by + bh)
            *y = by + bh - client_outer_height(c);
        if (*x + *w + 2 * c->bw <= bx)
            *x = bx;
        if (*y + *h + 2 * c->bw <= by)
            *y = by;
    } else {
        /* Tiled work area; docks live in the strut strip outside wx/wy and must not be
		 * snapped into wy or each arrange grows reserved space (feedback with updatebarpos). */
        int bx = m->wx, by = m->wy, bw = m->ww, bh = m->wh;
        if (c->isdock) {
            bx = m->mx;
            by = m->my;
            bw = m->mw;
            bh = m->mh;
        }
        if (*x >= bx + bw)
            *x = bx + bw - client_outer_width(c);
        if (*y >= by + bh)
            *y = by + bh - client_outer_height(c);
        if (*x + *w + 2 * c->bw <= bx)
            *x = bx;
        if (*y + *h + 2 * c->bw <= by)
            *y = by;
    }
    if (*h < bh)
        *h = bh;
    if (*w < bh)
        *w = bh;
    if (!interact && (g_config.resizehints || c->isfloating || !monitor_arrange_fn(c->mon))) {
        if (!c->hintsvalid)
            updatesizehints(c);
        /* see last two sentences in ICCCM 4.1.2.3 */
        baseismin = c->basew == c->minw && c->baseh == c->minh;
        if (!baseismin) { /* temporarily remove base dimensions */
            *w -= c->basew;
            *h -= c->baseh;
        }
        /* adjust for aspect limits */
        if (c->mina > 0 && c->maxa > 0) {
            if (*h > 0 && c->maxa < (static_cast<float>(*w) / static_cast<float>(*h)))
                *w = static_cast<int>(std::lround(static_cast<float>(*h) * c->maxa));
            else if (*w > 0 && c->mina < (static_cast<float>(*h) / static_cast<float>(*w)))
                *h = static_cast<int>(std::lround(static_cast<float>(*w) * c->mina));
        }
        if (baseismin) { /* increment calculation requires this */
            *w -= c->basew;
            *h -= c->baseh;
        }
        /* adjust for increment value */
        if (c->incw)
            *w -= *w % c->incw;
        if (c->inch)
            *h -= *h % c->inch;
        /* restore base dimensions */
        *w = std::max(*w + c->basew, c->minw);
        *h = std::max(*h + c->baseh, c->minh);
        if (c->maxw)
            *w = std::min(*w, c->maxw);
        if (c->maxh)
            *h = std::min(*h, c->maxh);
    }
    return *x != c->x || *y != c->y || *w != c->w || *h != c->h;
}

void arrange(Monitor* m, bool restack_after) {
    if (m) {
        /* Layout before showhide: resizeclient runs first so X windows are not moved to stale (x,y)
         * for one frame when client visibility flips (e.g. clients on a special tag when the overlay opens). */
        arrangemon(m);
        showhide(m->stack.empty() ? nullptr : m->stack.front());
        if (restack_after)
            restack(m);
    } else {
        for (Monitor* im : wm::state::all_monitors()) {
            if (!im->stack.empty())
                showhide(im->stack.front());
        }
        for (Monitor* im : wm::state::all_monitors())
            arrangemon(im);
    }
    savezestwmstate();
    savezesttreestate();
    savezestspecialoverlaystate();
    savezestspecialhiddenidstate();
    savezestselectionstate();
    savezestlayoutstate();
}

void arrangemon(Monitor* m) {
    Monitor* current = wm::state::runtime_authority().ref_current_monitor();
    updatebarpos(m, current);
    monitor_sync_layout_label(m);
    if (void (*const arrange_fn)(Monitor*) = monitor_arrange_fn(m))
        arrange_fn(m);
    drawconfigbanner(m, current);
    update_special_dimwin(m);
}

void configure(Client* c) {
    static_cast<void>(wm::x11::send_configure_notify(c->win, c->x, c->y, static_cast<unsigned>(c->w), static_cast<unsigned>(c->h), static_cast<unsigned>(c->bw), None, false));
}

void arrange_docks(Monitor* m) {
    long l, r, t, b;
    int  nx, ny, nw, nh;

    if (!m)
        return;
    for (Client* dock_c : m->clients) {
        if (!dock_c->isdock || !client_is_visible(dock_c))
            continue;
        nx = dock_c->x;
        ny = dock_c->y;
        nw = dock_c->w;
        nh = dock_c->h;
        l = r = t = b = 0;
        if (client_strut_partial(dock_c, &l, &r, &t, &b) && (l != 0 || r != 0 || t != 0 || b != 0)) {
            if (t > 0 && b == 0)
                ny = m->my;
            else if (b > 0 && t == 0)
                ny = m->my + static_cast<int>(m->mh) - nh;
            if (l > 0 && r == 0)
                nx = m->mx;
            else if (r > 0 && l == 0)
                nx = m->mx + static_cast<int>(m->mw) - nw;
        }
        resize(dock_c, nx, ny, nw, nh, 0);
    }
}

Monitor* recttomon_from_fallback(const int x, const int y, const int w, const int h, Monitor* fallback) {
    Monitor* r = fallback ? fallback : wm::state::mons_slot();
    int      a, area = 0;

    for (Monitor* m : wm::state::all_monitors()) {
        /* Overlap area between (x,y,w,h) and monitor rect (m->wx,wy,ww,wh). */
        const int ox = std::max(0, std::min(x + w, m->wx + static_cast<int>(m->ww)) - std::max(x, m->wx));
        const int oy = std::max(0, std::min(y + h, m->wy + static_cast<int>(m->wh)) - std::max(y, m->wy));
        a            = ox * oy;
        if (a > area) {
            area = a;
            r    = m;
        }
    }
    return r;
}

void resize(Client* c, int x, int y, int w, int h, int interact) {
    if (applysizehints(c, &x, &y, &w, &h, interact))
        resizeclient(c, x, y, w, h);
}

void resizeclient(Client* c, int x, int y, int w, int h) {
    wm::x11::WindowConfigure wc;

    c->oldx = c->x;
    c->x = wc.x = x;
    c->oldy     = c->y;
    c->y = wc.y = y;
    c->oldw     = c->w;
    c->w = wc.width = w;
    c->oldh         = c->h;
    c->h = wc.height = h;
    wc.border_width  = c->bw;
    wm::x11::configure_window(c->win, CWX | CWY | CWWidth | CWHeight | CWBorderWidth, wc);
    configure(c);
}

/* Fullscreen geometry: special clients use the tiled workarea (matches scratch overlay); others use full monitor. */
void resizeclient_fullscreen_target(Client* c) {
    if (!c || !c->mon)
        return;
    Monitor* m = c->mon;
    if (c->workspace.is_special() && !c->isdock) {
        unsigned outer_g     = g_config.gaps_in;
        unsigned inner_dummy = g_config.gaps_in;
        tiling_gaps_for_monitor_workspace(m, &outer_g, &inner_dummy);
        const int ox = m->wx + static_cast<int>(outer_g);
        const int oy = m->wy + static_cast<int>(outer_g);
        int       rw = static_cast<int>(m->ww) - 2 * static_cast<int>(outer_g);
        int       rh = static_cast<int>(m->wh) - 2 * static_cast<int>(outer_g);
        if (rw < 1)
            rw = 1;
        if (rh < 1)
            rh = 1;
        resizeclient(c, ox, oy, rw, rh);
    } else
        resizeclient(c, m->mx, m->my, m->mw, m->mh);
}

void showhide(Client* c) {
    if (!c)
        return;
    /* While a special overlay is open, park normal clients off-screen. Leaving them mapped under a
     * translucent dim showed fragments of the desktop around/under the scratchpad. */
    const bool overlay_parks_normal = c->mon && c->mon->special_overlay_open && !c->isdock && c->workspace.is_normal();
    if (client_is_visible(c) && !overlay_parks_normal) {
        /* show clients top down */
        /* Special/floating clients (Qt/wx/Electron) often paint while parked or pre-mapped off-screen;
         * Move alone leaves a stale frame when geometry already matches. Force full configure. */
        const bool reveal_configure = c->needresize || c->workspace.is_special() || c->isfloating;
        if (reveal_configure) {
            c->needresize = 0;
            wm::x11::move_resize_window(c->win, c->x, c->y, static_cast<unsigned>(c->w), static_cast<unsigned>(c->h));
            configure(c);
        } else {
            wm::x11::move_window(c->win, c->x, c->y);
        }

        if ((!monitor_arrange_fn(c->mon) || c->isfloating) && !c->isfullscreen)
            resize(c, c->x, c->y, c->w, c->h, 0);
        showhide(monitor_stack_older(c->mon, c));
    } else {
        /* hide clients bottom up */
        showhide(monitor_stack_older(c->mon, c));
        wm::x11::move_window(c->win, client_outer_width(c) * -2, c->y);
        /* Next visibility flip must reconfigure: X window may have mapped/painted off-screen. */
        c->needresize = 1;
    }
}

namespace {
    /* First tiled visible client at or after `c` in monitor client order. */
    Client* next_tiled_client(Monitor* m, Client* c) {
        if (!m)
            return nullptr;
        bool after = (c == nullptr);
        for (Client* x : m->clients) {
            if (after && !x->isfloating && client_is_visible(x))
                return x;
            if (x == c)
                after = true;
        }
        return nullptr;
    }
} // namespace

/* Monocle layout: stack all visible tiled clients to fill the monitor work area. */
void monocle(Monitor* m) {
    unsigned int n = 0;

    for (Client* count_c : m->clients) {
        if (client_is_visible(count_c))
            ++n;
    }
    if (n > 0)
        m->layout_label = "[" + std::to_string(n) + "]";
    for (Client* tile_c = next_tiled_client(m, nullptr); tile_c; tile_c = next_tiled_client(m, tile_c))
        resize(tile_c, m->wx, m->wy, m->ww - 2 * tile_c->bw, m->wh - 2 * tile_c->bw, 0);
    arrange_docks(m);
}

Monitor* wintomon_from_fallback(Window w, Monitor* fallback) {
    int     x, y;
    Client* c;

    if (w == wm::x11::root_window() && getrootptr(&x, &y))
        return recttomon_from_fallback(x, y, 1, 1, fallback);
    for (Monitor* m : wm::state::all_monitors()) {
        if (w == m->barwin || w == m->confwin)
            return m;
        if (monitor_groupbar_slot_for_window(m, w))
            return m;
    }
    c = wintoclient(w);
    if (c)
        return c->mon;
    return fallback;
}

Monitor* wintomon(Window w) {
    return wintomon_from_fallback(w, wm::state::monitor_or_fallback(wm::state::runtime_authority().ref_current_monitor(), wm::state::mons_slot()));
}
