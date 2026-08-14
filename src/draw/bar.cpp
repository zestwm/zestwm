/* Monitor bar, group tab, and config-error banner drawing implementation. */
#include "draw/bar.hpp"

#include "config.hpp"
#include "draw.hpp"
#include "intern.hpp"
#include "log.hpp"
#include "render.hpp"
#include "state/runtime_authority.hpp"
#include "util.hpp"
#include "wm_state.hpp"
#include "x11/color_utils.hpp"
#include "x11/connection.hpp"
#include "x11/wm_window.hpp"
#include "x11/wm_props.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <string>

static int                               conferr_show = 0;
static char                              conferr_msg[1024];
static Clr                               conferr_scheme[3];
static Clr                               groupbar_scheme_norm[3];
static Clr                               groupbar_scheme_sel[3];
static Clr                               groupbar_bg_clr;
static Clr                               groupbar_indicator_norm;
static Clr                               groupbar_indicator_sel;
static std::unique_ptr<wm::draw::Canvas> groupbar_canvas{};
static unsigned int                      groupbar_font_height = 0U;

static int                               is_hex_n(const std::string& s, size_t n) {
    if (s.size() != n)
        return 0;
    for (char c : s)
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return 0;
    return 1;
}

static std::string trim_copy(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.pop_back();
    return s;
}

struct ParsedColor {
    std::string rgb;
    double      alpha = 1.0;
    bool        ok    = false;
};

static ParsedColor parse_group_color(std::string in) {
    ParsedColor pc;
    in = trim_copy(std::move(in));
    if (in.empty())
        return pc;
    if (in.rfind("rgba(", 0) == 0) {
        size_t l = in.find('('), r = in.find(')', l + 1);
        if (l != std::string::npos && r != std::string::npos) {
            std::string body = in.substr(l + 1, r - l - 1);
            if (is_hex_n(body, 8)) {
                pc.rgb   = "#" + body.substr(0, 6);
                pc.alpha = static_cast<double>(strtoul(body.substr(6, 2).c_str(), nullptr, 16)) / 255.0;
                pc.ok    = true;
            }
        }
        return pc;
    }
    if (in.rfind("rgb(", 0) == 0) {
        size_t l = in.find('('), r = in.find(')', l + 1);
        if (l != std::string::npos && r != std::string::npos) {
            std::string body = in.substr(l + 1, r - l - 1);
            if (is_hex_n(body, 6)) {
                pc.rgb = "#" + body;
                pc.ok  = true;
            }
        }
        return pc;
    }
    if (in[0] == '#') {
        if (in.size() == 7 && is_hex_n(in.substr(1), 6)) {
            pc.rgb = in;
            pc.ok  = true;
            return pc;
        }
        if (in.size() == 9 && is_hex_n(in.substr(1), 8)) {
            pc.rgb   = "#" + in.substr(1, 6);
            pc.alpha = static_cast<double>(strtoul(in.substr(7, 2).c_str(), nullptr, 16)) / 255.0;
            pc.ok    = true;
            return pc;
        }
        return pc;
    }
    if (in.find(' ') != std::string::npos || in.find("deg") != std::string::npos)
        return pc;
    pc.rgb = in;
    pc.ok  = true;
    return pc;
}

static void apply_group_color(Clr* dst, const std::string& raw, const char* what) {
    ParsedColor pc = parse_group_color(raw);
    if (!pc.ok) {
        char buf[512];
        snprintf(buf, sizeof(buf), "zestwm: invalid %s color '%s' ignored", what, raw.c_str());
        wm::log::warn_and_log(buf);
        return;
    }
    if (!wm::x11::connection() || !wm::x11::default_screen() ||
        !wm::x11::parse_color_rgba(wm::x11::connection(), wm::x11::default_screen()->default_colormap, pc.rgb.c_str(), *dst))
        return;
    dst->a = pc.alpha;
}
void showconfigerrorbanner(void) {
    const char* msg     = wmconf_last_error();
    Monitor*    current = wm::state::runtime_authority().ref_current_monitor();

    if (!msg || !*msg)
        return;
    conferr_show = 1;
    snprintf(conferr_msg, sizeof(conferr_msg), "Config error: %s", msg);
    for (Monitor* m : wm::state::all_monitors())
        drawconfigbanner(m, current);
}

static int confbannerheight(void) {
    return th + 8;
}

int groupbar_thickness(void) {
    int base = th;

    if (groupbar_font_height > 0U)
        base = static_cast<int>(groupbar_font_height) + 2;
    if (g_config.groupbar_render_titles && g_config.groupbar_indicator_height > 0)
        base += g_config.groupbar_indicator_height + std::max(g_config.groupbar_indicator_gap, 0);
    if (base < 1)
        base = 1;
    return base;
}

GroupbarSlot* monitor_groupbar_slot_for_window(Monitor* m, Window w) {
    if (!m || !w)
        return nullptr;
    for (GroupbarSlot& slot : m->groupbars) {
        if (slot.win == w)
            return &slot;
    }
    return nullptr;
}
void drawtab(Monitor* m) {
    wm::draw::Canvas* gcanvas;
    Client*           c;
    int               i;
    int               vertical;
    int               step, off;
    int               is_active;
    int               tabw;
    int               tabh;
    int               ih, ig, ipos, base_ipos;
    int               tx, ty, tw, thh;
    Clr               bg_scheme[3];
    Clr               tab_scheme[3];
    Clr               ind_scheme[3];
    unsigned          group_lpad;

    gcanvas    = groupbar_canvas ? groupbar_canvas.get() : canvas.get();
    group_lpad = (groupbar_font_height > 0U) ? groupbar_font_height / 2U : static_cast<unsigned>(lrpad / 2);
    if (!gcanvas)
        return;

    auto draw_filled = [&](int x, int y, unsigned int w, unsigned int h, const Clr& color) { ignore_result(gcanvas->fill_rect(x, y, w, h, color)); };

    auto draw_frame_or_fill = [&](int x, int y, unsigned int w, unsigned int h, int filled, const Clr& color) {
        if (filled) {
            draw_filled(x, y, w, h, color);
            return;
        }
        if (w == 0U || h == 0U)
            return;
        draw_filled(x, y, w, 1U, color);
        if (h > 1U)
            draw_filled(x, y + static_cast<int>(h - 1U), w, 1U, color);
        if (h > 2U) {
            draw_filled(x, y + 1, 1U, h - 2U, color);
            if (w > 1U)
                draw_filled(x + static_cast<int>(w - 1U), y + 1, 1U, h - 2U, color);
        }
    };
    if (!g_config.groupbar_enabled) {
        for (const GroupbarSlot& slot : m->groupbars) {
            if (slot.win)
                wm::x11::move_window(slot.win, -m->ww, -th);
        }
        return;
    }
    for (GroupbarSlot& slot : m->groupbars) {
        LayoutNode* leaf = slot.anchor;
        if (!slot.win || !leaf || leaf->type != NODE_GROUPED || slot.ntabs <= 0 || slot.w <= 0 || slot.h <= 0) {
            if (slot.win)
                wm::x11::move_window(slot.win, -m->ww, -th);
            continue;
        }
        vertical  = (g_config.groupbar_position == 1 || g_config.groupbar_position == 2);
        step      = vertical ? (slot.h / slot.ntabs) : (slot.w / slot.ntabs);
        off       = 0;
        ih        = g_config.groupbar_indicator_height;
        ig        = g_config.groupbar_indicator_gap;
        base_ipos = g_config.groupbar_indicator_position;
        if (g_config.groupbar_render_titles) {
            if (vertical)
                base_ipos = (g_config.groupbar_position == 1) ? 2 : 1; /* left bar => indicator right, right bar => indicator left */
            else
                base_ipos = 3; /* top/bottom bars: indicator below title */
        } else {
            if (vertical && (base_ipos == 0 || base_ipos == 3))
                base_ipos = (g_config.groupbar_position == 1) ? 1 : 2;
            if (!vertical && (base_ipos == 1 || base_ipos == 2))
                base_ipos = (g_config.groupbar_position == 3) ? 3 : 0;
        }
        if (ig < 0)
            ig = 0;
        if (ih < 0)
            ih = 0;

        bg_scheme[static_cast<std::size_t>(ColorSlot::Foreground)] = groupbar_bg_clr;
        bg_scheme[static_cast<std::size_t>(ColorSlot::Background)] = groupbar_bg_clr;
        bg_scheme[static_cast<std::size_t>(ColorSlot::Border)]     = groupbar_bg_clr;
        draw_frame_or_fill(0, 0, static_cast<unsigned>(slot.w), static_cast<unsigned>(slot.h), 1, bg_scheme[static_cast<std::size_t>(ColorSlot::Foreground)]);

        for (i = 0; i < static_cast<int>(leaf->grouped.clients.size()); i++) {
            c = leaf->grouped.clients[i];
            if (!c || !client_is_visible(c))
                continue;
            is_active = (c == m->sel || (m->sel && m->sel->leaf != leaf && i == static_cast<int>(leaf->grouped.active)));
            tabw      = vertical ? slot.w : (off + step >= slot.w ? slot.w - off : step);
            tabh      = vertical ? (off + step >= slot.h ? slot.h - off : step) : slot.h;
            ipos      = base_ipos;
            tx        = vertical ? 0 : off;
            ty        = vertical ? off : 0;
            tw        = tabw;
            thh       = tabh;
            if (g_config.groupbar_render_titles && ih > 0) {
                if (ipos == 0) {
                    ty += ih + ig;
                    thh -= ih + ig;
                } else if (ipos == 3) {
                    thh -= ih + ig;
                } else if (ipos == 1) {
                    tx += ih + ig;
                    tw -= ih + ig;
                } else if (ipos == 2) {
                    tw -= ih + ig;
                }
                if (tw < 0)
                    tw = 0;
                if (thh < 0)
                    thh = 0;
            }
            tab_scheme[static_cast<std::size_t>(ColorSlot::Foreground)] =
                is_active ? groupbar_scheme_sel[static_cast<std::size_t>(ColorSlot::Foreground)] : groupbar_scheme_norm[static_cast<std::size_t>(ColorSlot::Foreground)];
            tab_scheme[static_cast<std::size_t>(ColorSlot::Background)] = groupbar_bg_clr;
            tab_scheme[static_cast<std::size_t>(ColorSlot::Border)]     = groupbar_bg_clr;
            if (vertical && g_config.groupbar_render_titles)
                ignore_result(gcanvas->draw_text_rotate_90(tx, ty, static_cast<unsigned>(tw), static_cast<unsigned>(thh), group_lpad, c->name,
                                                           tab_scheme[static_cast<std::size_t>(ColorSlot::Foreground)], tab_scheme[static_cast<std::size_t>(ColorSlot::Background)],
                                                           g_config.groupbar_position == 1));
            else
                ignore_result(gcanvas->draw_text(tx, ty, static_cast<unsigned>(tw), static_cast<unsigned>(thh), group_lpad, g_config.groupbar_render_titles ? c->name : "",
                                                 tab_scheme[static_cast<std::size_t>(ColorSlot::Foreground)], tab_scheme[static_cast<std::size_t>(ColorSlot::Background)]));

            if (ih > 0 && tabw > 2 && tabh > 2) {
                ind_scheme[static_cast<std::size_t>(ColorSlot::Foreground)] = is_active ? groupbar_indicator_sel : groupbar_indicator_norm;
                ind_scheme[static_cast<std::size_t>(ColorSlot::Background)] = groupbar_bg_clr;
                ind_scheme[static_cast<std::size_t>(ColorSlot::Border)]     = groupbar_bg_clr;
                if (ipos == 0 || ipos == 3) {
                    int rw = tabw - 2;
                    int rh = ih;
                    if (rh > tabh - ig)
                        rh = tabh - ig;
                    if (rh > 0)
                        draw_frame_or_fill((vertical ? 0 : off) + 1, (vertical ? off : 0) + (ipos == 0 ? ig : tabh - ig - rh), static_cast<unsigned>(rw), static_cast<unsigned>(rh),
                                           1, ind_scheme[static_cast<std::size_t>(ColorSlot::Foreground)]);
                } else {
                    int rw = ih;
                    int rh = tabh - 2;
                    if (rw > tabw - ig)
                        rw = tabw - ig;
                    if (rw > 0)
                        draw_frame_or_fill((vertical ? 0 : off) + (ipos == 1 ? ig : tabw - ig - rw), (vertical ? off : 0) + 1, static_cast<unsigned>(rw), static_cast<unsigned>(rh),
                                           1, ind_scheme[static_cast<std::size_t>(ColorSlot::Foreground)]);
                }
            }
            off += step;
        }
        wm::x11::move_resize_window(slot.win, slot.x, slot.y, slot.w, slot.h);
        ignore_result(gcanvas->set_target(slot.win));
        ignore_result(gcanvas->present(0, 0, static_cast<unsigned>(slot.w), static_cast<unsigned>(slot.h)));
    }
}

void updategroupbarwin(void) {
    wm::x11::WindowAttrs wa;

    wa                   = {};
    wa.override_redirect = true;
    wa.background_pixmap = ParentRelative;
    wa.event_mask        = ButtonPressMask | ExposureMask;

    for (Monitor* m : wm::state::all_monitors()) {
        for (std::size_t i = 0; i < m->groupbars.size(); ++i) {
            if (m->groupbars[i].win)
                continue;
            m->groupbars[i].win = wm::x11::create_window(wm::x11::root_window(), m->wx, m->wy, 1, 1, 0, root_depth, CopyFromParent, root_visual->visual_id,
                                                         CWOverrideRedirect | CWBackPixmap | CWEventMask, wa);
            wm::x11::define_cursor(m->groupbars[i].win, cursor[static_cast<std::size_t>(CursorKind::Normal)]->cursor);
            wm::x11::map_raised(m->groupbars[i].win);
            ignore_result(wm::x11::write_class_hint(static_cast<xcb_window_t>(m->groupbars[i].win), "zestwm", "zestwm-tab"));
        }
    }
}

void updateconfigbannerwin(void) {
    wm::x11::WindowAttrs wa;
    int                  chh = confbannerheight();

    wa                   = {};
    wa.override_redirect = true;
    wa.background_pixmap = ParentRelative;
    wa.event_mask        = ExposureMask | ButtonPressMask;

    for (Monitor* m : wm::state::all_monitors()) {
        if (m->confwin)
            continue;
        m->confwin = wm::x11::create_window(wm::x11::root_window(), m->wx, m->wy, m->ww, chh, 0, root_depth, CopyFromParent, root_visual->visual_id,
                                            CWOverrideRedirect | CWBackPixmap | CWEventMask, wa);
        wm::x11::define_cursor(m->confwin, cursor[static_cast<std::size_t>(CursorKind::Normal)]->cursor);
        wm::x11::map_raised(m->confwin);
        ignore_result(wm::x11::write_class_hint(static_cast<xcb_window_t>(m->confwin), "zestwm", "zestwm-config-error"));
    }
}

void drawconfigbanner(Monitor* m, Monitor* current) {
    int               chh = m ? m->confh : confbannerheight();
    int               show_here;
    wm::draw::Canvas* active_canvas = canvas.get();

    if (!m || !m->confwin)
        return;
    if (!active_canvas)
        return;
    show_here = conferr_show && conferr_msg[0] != '\0' && ((current && m == current) || (!current && m == wm::state::mons_slot()));
    if (!show_here) {
        wm::x11::move_window(m->confwin, -m->ww, -chh);
        return;
    }
    ignore_result(active_canvas->fill_rect(0, 0, static_cast<unsigned>(m->ww), static_cast<unsigned>(chh), conferr_scheme[static_cast<std::size_t>(ColorSlot::Background)]));
    ignore_result(active_canvas->draw_text(0, 4, static_cast<unsigned>(m->ww), static_cast<unsigned>(th), 12U, conferr_msg,
                                           conferr_scheme[static_cast<std::size_t>(ColorSlot::Foreground)], conferr_scheme[static_cast<std::size_t>(ColorSlot::Background)]));
    wm::x11::move_resize_window(m->confwin, m->wx, m->confy, m->ww, chh);
    ignore_result(active_canvas->set_target(m->confwin));
    ignore_result(active_canvas->present(0, 0, static_cast<unsigned>(m->ww), static_cast<unsigned>(chh)));
}

/* Read _NET_WM_STRUT_PARTIAL (first four fields: left, right, top, bottom). */
int client_strut_partial(Client* c, long* left, long* right, long* top, long* bottom) {
    *left = *right = *top = *bottom = 0;
    if (!wm::x11::connection() || !c)
        return 0;
    const auto prop =
        wm::x11::read_property(static_cast<xcb_window_t>(c->win), static_cast<xcb_atom_t>(wm::x11::net_atom(NetWMStrutPartial)), 0U, 12U, false, XCB_GET_PROPERTY_TYPE_ANY);
    if (!prop || prop->format != 32 || prop->data.size() < 4U * sizeof(std::uint32_t))
        return 0;
    const auto* v = reinterpret_cast<const std::uint32_t*>(prop->data.data());
    *left         = static_cast<long>(v[0]);
    *right        = static_cast<long>(v[1]);
    *top          = static_cast<long>(v[2]);
    *bottom       = static_cast<long>(v[3]);
    return 1;
}

/* Grow strut hints so reserved area at least covers the dock window on this monitor
 * (reported _NET_WM_STRUT_PARTIAL can be a few pixels short of the real window). */
void dock_merge_geometry_reserve(Client* c, Monitor* m, long* l, long* r, long* t, long* b) {
    int  x0, y0, x1, y1;
    long top_use, bot_use, left_use, right_use;

    if (!c || !m)
        return;
    x0 = c->x;
    y0 = c->y;
    x1 = c->x + static_cast<int>(client_outer_width(c));
    y1 = c->y + static_cast<int>(client_outer_height(c));
    if (x1 <= m->mx || x0 >= m->mx + static_cast<int>(m->mw) || y1 <= m->my || y0 >= m->my + static_cast<int>(m->mh))
        return;
    top_use = static_cast<long>(y1) - static_cast<long>(m->my);
    if (top_use > 0 && y0 < m->my + static_cast<int>(m->mh) / 2 && top_use < static_cast<long>(m->mh) / 2 && top_use > *t)
        *t = top_use;
    bot_use = static_cast<long>(m->my) + static_cast<long>(m->mh) - static_cast<long>(y0);
    if (bot_use > 0 && y1 > m->my + static_cast<int>(m->mh) / 2 && bot_use < static_cast<long>(m->mh) / 2 && bot_use > *b)
        *b = bot_use;
    left_use = static_cast<long>(x1) - static_cast<long>(m->mx);
    if (left_use > 0 && x0 < m->mx + static_cast<int>(m->mw) / 2 && left_use < static_cast<long>(m->mw) / 2 && left_use > *l)
        *l = left_use;
    right_use = static_cast<long>(m->mx) + static_cast<long>(m->mw) - static_cast<long>(x0);
    if (right_use > 0 && x1 > m->mx + static_cast<int>(m->mw) / 2 && right_use < static_cast<long>(m->mw) / 2 && right_use > *r)
        *r = right_use;
}

void updatebarpos(Monitor* m, Monitor* current) {
    long st = 0, sb = 0, sl = 0, sr = 0;
    long l, r, t, b;
    long gl, gr, gt, gb;
    bool has_strut;
    int  chh;
    int  show_here;

    m->wx = m->mx;
    m->wy = m->my;
    m->ww = m->mw;
    m->wh = m->mh;
    for (Client* bar_c : m->clients) {
        if (!bar_c->isdock || !client_is_visible(bar_c))
            continue;
        l = r = t = b = 0;
        has_strut     = client_strut_partial(bar_c, &l, &r, &t, &b) && (l != 0 || r != 0 || t != 0 || b != 0);
        if (!has_strut) {
            /* No strut property yet: reserve strip from dock window geometry */
            if (bar_c->h > 0 && bar_c->h < static_cast<int>(m->mh) / 3 && bar_c->y <= m->my + 4)
                t = static_cast<long>(bar_c->h) + 2 * static_cast<long>(bar_c->bw);
            else if (bar_c->h > 0 && bar_c->h < static_cast<int>(m->mh) / 3 && bar_c->y + static_cast<int>(client_outer_height(bar_c)) >= static_cast<int>(m->my + m->mh) - 4)
                b = static_cast<long>(m->my + m->mh) - static_cast<long>(bar_c->y);
            dock_merge_geometry_reserve(bar_c, m, &l, &r, &t, &b);
        } else {
            /* Some docks publish partial/odd struts; fill only missing axes from geometry. */
            gl = gr = gt = gb = 0;
            dock_merge_geometry_reserve(bar_c, m, &gl, &gr, &gt, &gb);
            if (t == 0 && gt > 0)
                t = gt;
            if (b == 0 && gb > 0)
                b = gb;
            if (l == 0 && gl > 0)
                l = gl;
            if (r == 0 && gr > 0)
                r = gr;
        }
        if (t > st)
            st = t;
        if (b > sb)
            sb = b;
        if (l > sl)
            sl = l;
        if (r > sr)
            sr = r;
    }
    m->wx += static_cast<int>(sl);
    m->wy += static_cast<int>(st);
    m->ww -= static_cast<int>(sl + sr);
    m->wh -= static_cast<int>(st + sb);
    if (static_cast<int>(m->ww) < 1)
        m->ww = 1;
    if (static_cast<int>(m->wh) < 1)
        m->wh = 1;
    m->confh  = 0;
    m->confy  = -th;
    show_here = conferr_show && ((current && m == current) || (!current && m == wm::state::mons_slot()));
    if (show_here) {
        chh      = confbannerheight();
        m->confy = m->wy;
        m->confh = chh;
        m->wy += chh;
        m->wh -= chh;
        if (m->wh < 1)
            m->wh = 1;
    }
    m->by = -bh;
    m->ty = -th;
    m->tx = m->tw = 0;
    m->th         = 0;
}

void draw_bar_init_canvas(std::unique_ptr<wm::draw::Canvas> canvas, unsigned font_height) {
    groupbar_canvas      = std::move(canvas);
    groupbar_font_height = font_height;
}

void draw_bar_resize_canvas(unsigned width, unsigned height) {
    if (groupbar_canvas)
        ignore_result(groupbar_canvas->resize(width, height));
}

void draw_bar_shutdown() {
    groupbar_canvas.reset();
    groupbar_font_height = 0U;
}

void draw_init_groupbar_chrome() {
    groupbar_scheme_norm[static_cast<std::size_t>(ColorSlot::Foreground)] = scheme[static_cast<std::size_t>(SchemeKind::Normal)][static_cast<std::size_t>(ColorSlot::Foreground)];
    groupbar_scheme_norm[static_cast<std::size_t>(ColorSlot::Background)] = scheme[static_cast<std::size_t>(SchemeKind::Normal)][static_cast<std::size_t>(ColorSlot::Background)];
    groupbar_scheme_norm[static_cast<std::size_t>(ColorSlot::Border)]     = scheme[static_cast<std::size_t>(SchemeKind::Normal)][static_cast<std::size_t>(ColorSlot::Border)];
    groupbar_scheme_sel[static_cast<std::size_t>(ColorSlot::Foreground)]  = scheme[static_cast<std::size_t>(SchemeKind::Selected)][static_cast<std::size_t>(ColorSlot::Foreground)];
    groupbar_scheme_sel[static_cast<std::size_t>(ColorSlot::Background)]  = scheme[static_cast<std::size_t>(SchemeKind::Selected)][static_cast<std::size_t>(ColorSlot::Background)];
    groupbar_scheme_sel[static_cast<std::size_t>(ColorSlot::Border)]      = scheme[static_cast<std::size_t>(SchemeKind::Selected)][static_cast<std::size_t>(ColorSlot::Border)];
    groupbar_bg_clr                                                       = groupbar_scheme_norm[static_cast<std::size_t>(ColorSlot::Background)];
    groupbar_indicator_norm                                               = groupbar_scheme_norm[static_cast<std::size_t>(ColorSlot::Background)];
    groupbar_indicator_sel                                                = groupbar_scheme_sel[static_cast<std::size_t>(ColorSlot::Background)];
    if (!g_config.groupbar_col_background.empty()) {
        apply_group_color(&groupbar_bg_clr, g_config.groupbar_col_background, "groupbar.col.background");
    }
    if (!g_config.groupbar_col_active.empty())
        apply_group_color(&groupbar_indicator_sel, g_config.groupbar_col_active, "groupbar.col.active");
    if (!g_config.groupbar_col_inactive.empty())
        apply_group_color(&groupbar_indicator_norm, g_config.groupbar_col_inactive, "groupbar.col.inactive");
    if (!g_config.active_border_color.empty())
        apply_group_color(&scheme[static_cast<std::size_t>(SchemeKind::Selected)][static_cast<std::size_t>(ColorSlot::Border)], g_config.active_border_color,
                          "general.col.active_border");
    if (!g_config.inactive_border_color.empty())
        apply_group_color(&scheme[static_cast<std::size_t>(SchemeKind::Normal)][static_cast<std::size_t>(ColorSlot::Border)], g_config.inactive_border_color,
                          "general.col.inactive_border");
    if (!g_config.group_border_active_color.empty())
        apply_group_color(&scheme[static_cast<std::size_t>(SchemeKind::Selected)][static_cast<std::size_t>(ColorSlot::Border)], g_config.group_border_active_color,
                          "group.col.border_active");
    if (!g_config.group_border_inactive_color.empty())
        apply_group_color(&scheme[static_cast<std::size_t>(SchemeKind::Normal)][static_cast<std::size_t>(ColorSlot::Border)], g_config.group_border_inactive_color,
                          "group.col.border_inactive");
    wm::x11::parse_color_rgba(wm::x11::connection(), wm::x11::default_screen()->default_colormap, "#ffffff", conferr_scheme[static_cast<std::size_t>(ColorSlot::Foreground)]);
    wm::x11::parse_color_rgba(wm::x11::connection(), wm::x11::default_screen()->default_colormap, "#a40000", conferr_scheme[static_cast<std::size_t>(ColorSlot::Background)]);
    wm::x11::parse_color_rgba(wm::x11::connection(), wm::x11::default_screen()->default_colormap, "#a40000", conferr_scheme[static_cast<std::size_t>(ColorSlot::Border)]);
}
