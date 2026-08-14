/* WM startup: X11 screen setup, EWMH atoms, and initial client scan. */
#include "wm_startup.hpp"

#include "client/client_focus.hpp"
#include "client/client_lifecycle.hpp"
#include "client/client_props.hpp"
#include "config.hpp"
#include "dispatch/xcb_handlers.hpp"
#include "draw/bar.hpp"
#include "intern.hpp"
#include "monitor/monitor_lifecycle.hpp"
#include "state/wm_state_root.hpp"
#include "util.hpp"
#include "wm_state.hpp"
#include "x11/backend.hpp"
#include "x11/color_utils.hpp"
#include "x11/connection.hpp"
#include "x11/cursor_utils.hpp"
#include "x11/wm_ops.hpp"
#include "x11/wm_pointer.hpp"
#include "x11/wm_window.hpp"
#include "x11/wm_props.hpp"

#include <cctype>
#include <csignal>
#include <cstdlib>
#include <sys/wait.h>
#include <xcb/randr.h>

#include <algorithm>
#include <string>

#include <X11/cursorfont.h>

int startup_restore_pending = 0;

namespace {
    /* Trim ASCII whitespace from both ends of a config/font string. */
    std::string trim_copy(std::string s) {
        while (!s.empty() && isspace(static_cast<unsigned char>(s.front())))
            s.erase(s.begin());
        while (!s.empty() && isspace(static_cast<unsigned char>(s.back())))
            s.pop_back();
        return s;
    }

    /* Build Pango font description from config `family:size=N` with optional size override. */
    std::string pango_font_desc(std::string raw, int size_override) {
        int    parsed_size = 0;
        size_t pos, i;

        raw = trim_copy(std::move(raw));
        pos = raw.find(":size=");
        if (pos != std::string::npos) {
            i = pos + 6;
            while (i < raw.size() && isdigit(static_cast<unsigned char>(raw[i]))) {
                parsed_size = parsed_size * 10 + (raw[i] - '0');
                i++;
            }
            raw = trim_copy(raw.substr(0, pos));
        }
        if (size_override > 0)
            parsed_size = size_override;
        if (raw.empty())
            raw = "monospace";
        if (parsed_size > 0)
            raw += " " + std::to_string(parsed_size);
        return raw;
    }

    /* Fails with BadAccess if another client already selected SubstructureRedirect on root. */
    void checkotherwm(void) {
        if (!wm::x11::try_select_input(root, SubstructureRedirectMask))
            die("zestwm: another window manager is already running");
        wm::x11::sync(false);
    }

    /* Persist state and request in-process restart on SIGHUP (config watch / reload). */
    void sighup(int unused) {
        ignore_result(unused);
        savezestwmstate();
        savezesttreestate();
        savezestspecialoverlaystate();
        savezestspecialhiddenidstate();
        savezestselectionstate();
        restart = 1;
        running = 0;
    }
} // namespace

/* Adopt existing top-level windows before tree-state restore runs in main. */
void scan(void) {
    unsigned int i;

    const auto   tree = wm::x11::query_tree(static_cast<xcb_window_t>(root));
    if (!tree)
        return;
    for (i = 0; i < tree->children.size(); i++) {
        const Window win  = static_cast<Window>(tree->children[i]);
        const auto   info = wm::x11::read_window_info(win);
        if (!info || info->override_redirect || wm::x11::read_transient_for(win))
            continue;
        if (info->map_state == IsViewable || getstate(win) == IconicState)
            adopt_client(win, *info);
    }
    for (i = 0; i < tree->children.size(); i++) {
        const Window win  = static_cast<Window>(tree->children[i]);
        const auto   info = wm::x11::read_window_info(win);
        if (!info)
            continue;
        if (wm::x11::read_transient_for(win) && (info->map_state == IsViewable || getstate(win) == IconicState))
            adopt_client(win, *info);
    }
}

/* Initialize X11 screen, atoms, cursors, draw contexts, and root event mask. */
void setup(void) {
    int                  i;
    wm::x11::WindowAttrs wa;
    uint32_t             wmcheck;
    uint32_t             supported[NetLast];
    struct sigaction     sa;
    struct sigaction     hupsa;
    X11Backend*          backend_ctx;

    /* do not transform children into zombies when they terminate */
    sigemptyset(&sa.sa_mask);
    sa.sa_flags   = SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESTART;
    sa.sa_handler = SIG_IGN;
    sigaction(SIGCHLD, &sa, nullptr);
    sigemptyset(&hupsa.sa_mask);
    hupsa.sa_flags   = 0;
    hupsa.sa_handler = sighup;
    sigaction(SIGHUP, &hupsa, nullptr);

    /* clean up any zombies (inherited from .xinitrc etc) immediately */
    while (waitpid(-1, nullptr, WNOHANG) > 0)
        ;

    backend_ctx = x11_backend_peek_context();
    if (!backend_ctx || !backend_ctx->conn)
        die("zestwm: backend context not initialized");
    xcb = backend_ctx->conn;

    /* init screen */
    xscreen = backend_ctx->screen;
    if (!xscreen) {
        xscreen             = wm::x11::screen_at(screen);
        backend_ctx->screen = xscreen;
    }
    if (!xscreen)
        die("zestwm: cannot get screen");
    root              = xscreen->root;
    backend_ctx->root = root;
    /* must run after root is set (was incorrectly called from main with root == 0) */
    checkotherwm();
    wmconf_apply_input_settings();
    root_visual = wm::x11::visual_for_screen(xscreen);
    if (!root_visual)
        die("zestwm: cannot resolve root visual");
    root_depth = wm::x11::root_depth_for_screen(xscreen);
    sw         = xscreen->width_in_pixels;
    sh         = xscreen->height_in_pixels;
    /* Globals filled above; remaining setup uses accessors so later threading can swap backends. */
    xcb_connection_t* const conn       = wm::x11::connection();
    xcb_screen_t* const     scr        = wm::x11::default_screen();
    const Window            root_win   = wm::x11::root_window();
    auto                    canvas_res = wm::draw::Canvas::create(*conn, *scr, root_win, static_cast<unsigned>(sw), static_cast<unsigned>(sh));
    if (!canvas_res.has_value())
        die("zestwm: cannot create draw context");
    canvas = std::move(canvas_res.value());
    if (xcb_cursor_context_new(conn, scr, &cursor_ctx) != 0)
        die("zestwm: cannot create cursor context");
    {
        std::string main_font = pango_font_desc(g_config.wm_misc.font_family, 0);
        if (auto set_font_res = canvas->set_font(main_font); !set_font_res.has_value())
            die("no fonts could be loaded.");
        if (auto font_h = canvas->text_height(); font_h.has_value())
            canvas_font_height = font_h.value();
    }
    auto group_canvas_res = wm::draw::Canvas::create(*conn, *scr, root_win, static_cast<unsigned>(sw), static_cast<unsigned>(sh));
    if (!group_canvas_res.has_value())
        die("zestwm: cannot create groupbar draw context");
    unsigned groupbar_font_height = 0U;
    {
        std::string gb_font = g_config.wm_misc.font_family;
        if (!g_config.groupbar_font_family.empty())
            gb_font = g_config.groupbar_font_family;
        gb_font        = pango_font_desc(gb_font, g_config.groupbar_font_size);
        auto gb_canvas = std::move(group_canvas_res.value());
        if (auto gb_set_font_res = gb_canvas->set_font(gb_font); !gb_set_font_res.has_value())
            die("zestwm: no groupbar fonts could be loaded.");
        if (auto gb_font_h = gb_canvas->text_height(); gb_font_h.has_value())
            groupbar_font_height = gb_font_h.value();
        draw_bar_init_canvas(std::move(gb_canvas), groupbar_font_height);
    }
    if (!g_config.wm_misc.background_color.empty()) {
        Clr bg;
        wm::x11::parse_color_rgba(conn, scr->default_colormap, g_config.wm_misc.background_color.c_str(), bg);
        uint32_t pix = wm::x11::resolve_x11_pixel(conn, scr->default_colormap, bg);
        xcb_change_window_attributes(conn, root_win, XCB_CW_BACK_PIXEL, &pix);
        xcb_clear_area(conn, 0, root_win, 0, 0, 0, 0);
        zestwm_flush_connection();
    }
    lrpad = static_cast<int>(canvas_font_height);
    bh    = static_cast<int>(canvas_font_height) + 2;
    th    = bh;
    if (groupbar_font_height > 0U)
        th = std::max(th, static_cast<int>(groupbar_font_height) + 2);
    {
        wm::state::WMState rt = wm::state::build_runtime_state_root();
        updategeom(rt.monitors);
    }
    /* init atoms */
    backend_ctx->warmup_atoms({"UTF8_STRING",
                               "WM_PROTOCOLS",
                               "WM_DELETE_WINDOW",
                               "WM_STATE",
                               "WM_TAKE_FOCUS",
                               "_NET_ACTIVE_WINDOW",
                               "_NET_SUPPORTED",
                               "_NET_WM_NAME",
                               "_NET_WM_STATE",
                               "_NET_SUPPORTING_WM_CHECK",
                               "_NET_WM_STATE_FULLSCREEN",
                               "_NET_WM_WINDOW_TYPE",
                               "_NET_WM_WINDOW_TYPE_DIALOG",
                               "_NET_WM_WINDOW_TYPE_DOCK",
                               "_NET_WM_WINDOW_TYPE_SPLASH",
                               "_NET_WM_WINDOW_TYPE_UTILITY",
                               "_NET_WM_STRUT_PARTIAL",
                               "_NET_WM_DESKTOP",
                               "_NET_CLIENT_LIST",
                               "_NET_WM_WINDOW_OPACITY",
                               "_NET_ZESTWM_STATE",
                               "_NET_ZEST_LAYOUTS",
                               "_NET_ZEST_LAYOUT_LIST",
                               "_NET_ZEST_TREE_STATE",
                               "_NET_ZEST_SELECTION_STATE",
                               "_NET_ZEST_DISPATCH",
                               "_NET_ZESTWM_SPECIAL_HIDDEN_ID",
                               "_NET_ZESTWM_SPECIAL_OVERLAY",
                               "_NET_ZESTWM_SPECIAL_HIDDEN_IDS",
                               "_NET_NUMBER_OF_DESKTOPS",
                               "_NET_CURRENT_DESKTOP",
                               "_NET_DESKTOP_NAMES"});
    utf8_atom                               = static_cast<Atom>(backend_ctx->get_atom("UTF8_STRING"));
    wmatom[WMProtocols]                     = static_cast<Atom>(backend_ctx->get_atom("WM_PROTOCOLS"));
    wmatom[WMDelete]                        = static_cast<Atom>(backend_ctx->get_atom("WM_DELETE_WINDOW"));
    wmatom[WMState]                         = static_cast<Atom>(backend_ctx->get_atom("WM_STATE"));
    wmatom[WMTakeFocus]                     = static_cast<Atom>(backend_ctx->get_atom("WM_TAKE_FOCUS"));
    netatom[NetActiveWindow]                = static_cast<Atom>(backend_ctx->get_atom("_NET_ACTIVE_WINDOW"));
    netatom[NetSupported]                   = static_cast<Atom>(backend_ctx->get_atom("_NET_SUPPORTED"));
    netatom[NetWMName]                      = static_cast<Atom>(backend_ctx->get_atom("_NET_WM_NAME"));
    netatom[NetWMState]                     = static_cast<Atom>(backend_ctx->get_atom("_NET_WM_STATE"));
    netatom[NetWMCheck]                     = static_cast<Atom>(backend_ctx->get_atom("_NET_SUPPORTING_WM_CHECK"));
    netatom[NetWMFullscreen]                = static_cast<Atom>(backend_ctx->get_atom("_NET_WM_STATE_FULLSCREEN"));
    netatom[NetWMWindowType]                = static_cast<Atom>(backend_ctx->get_atom("_NET_WM_WINDOW_TYPE"));
    netatom[NetWMWindowTypeDialog]          = static_cast<Atom>(backend_ctx->get_atom("_NET_WM_WINDOW_TYPE_DIALOG"));
    netatom[NetWMWindowTypeDock]            = static_cast<Atom>(backend_ctx->get_atom("_NET_WM_WINDOW_TYPE_DOCK"));
    netatom[NetWMWindowTypeSplash]          = static_cast<Atom>(backend_ctx->get_atom("_NET_WM_WINDOW_TYPE_SPLASH"));
    netatom[NetWMWindowTypeUtility]         = static_cast<Atom>(backend_ctx->get_atom("_NET_WM_WINDOW_TYPE_UTILITY"));
    netatom[NetWMStrutPartial]              = static_cast<Atom>(backend_ctx->get_atom("_NET_WM_STRUT_PARTIAL"));
    netatom[NetWMDesktop]                   = static_cast<Atom>(backend_ctx->get_atom("_NET_WM_DESKTOP"));
    netatom[NetClientList]                  = static_cast<Atom>(backend_ctx->get_atom("_NET_CLIENT_LIST"));
    netatom[NetWMWindowsOpacity]            = static_cast<Atom>(backend_ctx->get_atom("_NET_WM_WINDOW_OPACITY"));
    netatom[NetZestwmState]                 = static_cast<Atom>(backend_ctx->get_atom("_NET_ZESTWM_STATE"));
    netatom[NetZestLayouts]                 = static_cast<Atom>(backend_ctx->get_atom("_NET_ZEST_LAYOUTS"));
    netatom[NetZestLayoutList]              = static_cast<Atom>(backend_ctx->get_atom("_NET_ZEST_LAYOUT_LIST"));
    netatom[NetZestTreeState]               = static_cast<Atom>(backend_ctx->get_atom("_NET_ZEST_TREE_STATE"));
    netatom[NetZestSelectionState]          = static_cast<Atom>(backend_ctx->get_atom("_NET_ZEST_SELECTION_STATE"));
    netatom[NetZestDispatch]                = static_cast<Atom>(backend_ctx->get_atom("_NET_ZEST_DISPATCH"));
    netatom[NetZestSpecialDispatchHiddenId] = static_cast<Atom>(backend_ctx->get_atom("_NET_ZESTWM_SPECIAL_HIDDEN_ID"));
    netatom[NetZestSpecialOverlayState]     = static_cast<Atom>(backend_ctx->get_atom("_NET_ZESTWM_SPECIAL_OVERLAY"));
    netatom[NetZestSpecialHiddenIdState]    = static_cast<Atom>(backend_ctx->get_atom("_NET_ZESTWM_SPECIAL_HIDDEN_IDS"));
    netatom[NetZestFloatingClients]         = static_cast<Atom>(backend_ctx->get_atom("_NET_ZEST_FLOATING_CLIENTS"));
    netatom[NetNumberOfDesktops]            = static_cast<Atom>(backend_ctx->get_atom("_NET_NUMBER_OF_DESKTOPS"));
    netatom[NetCurrentDesktop]              = static_cast<Atom>(backend_ctx->get_atom("_NET_CURRENT_DESKTOP"));
    netatom[NetDesktopNames]                = static_cast<Atom>(backend_ctx->get_atom("_NET_DESKTOP_NAMES"));
    /* init cursors */
    cursor[static_cast<std::size_t>(CursorKind::Normal)]  = wm::x11::create_cursor(cursor_ctx, XC_left_ptr);
    cursor[static_cast<std::size_t>(CursorKind::Resize)]  = wm::x11::create_cursor(cursor_ctx, XC_sizing);
    cursor[static_cast<std::size_t>(CursorKind::ResizeH)] = wm::x11::create_cursor(cursor_ctx, XC_sb_h_double_arrow);
    cursor[static_cast<std::size_t>(CursorKind::ResizeV)] = wm::x11::create_cursor(cursor_ctx, XC_sb_v_double_arrow);
    cursor[static_cast<std::size_t>(CursorKind::Move)]    = wm::x11::create_cursor(cursor_ctx, XC_fleur);
    /* init appearance */
    scheme.resize(g_config.colors.size());
    for (i = 0; i < static_cast<int>(g_config.colors.size()); i++) {
        const char* row[3] = {g_config.colors[i][0].c_str(), g_config.colors[i][1].c_str(), g_config.colors[i][2].c_str()};
        for (int cidx = 0; cidx < 3; ++cidx)
            wm::x11::parse_color_rgba(wm::x11::connection(), wm::x11::default_screen()->default_colormap, row[cidx], scheme[static_cast<size_t>(i)][static_cast<size_t>(cidx)]);
    }
    draw_init_groupbar_chrome();
    updategroupbarwin();
    updateconfigbannerwin();
    updatestatus();
    /* supporting window for NetWMCheck */
    wmcheckwin = wm::x11::create_simple_window(wm::x11::root_window(), 0, 0, 1, 1, 0, 0, 0);
    wmcheck    = static_cast<uint32_t>(wmcheckwin);
    if (xcb_connection_t* const pub_conn = wm::x11::connection()) {
        const Window pub_root = wm::x11::root_window();
        xcb_change_property(pub_conn, XCB_PROP_MODE_REPLACE, static_cast<xcb_window_t>(wmcheckwin), static_cast<xcb_atom_t>(wm::x11::net_atom(NetWMCheck)), XCB_ATOM_WINDOW, 32, 1,
                            &wmcheck);
        xcb_change_property(pub_conn, XCB_PROP_MODE_REPLACE, static_cast<xcb_window_t>(wmcheckwin), static_cast<xcb_atom_t>(wm::x11::net_atom(NetWMName)),
                            static_cast<xcb_atom_t>(wm::x11::utf8_string_atom()), 8, 6, "zestwm");
        xcb_change_property(pub_conn, XCB_PROP_MODE_REPLACE, static_cast<xcb_window_t>(pub_root), static_cast<xcb_atom_t>(wm::x11::net_atom(NetWMCheck)), XCB_ATOM_WINDOW, 32, 1,
                            &wmcheck);
    }
    /* EWMH support per view */
    for (i = 0; i < NetLast; i++)
        supported[i] = static_cast<uint32_t>(wm::x11::net_atom(static_cast<AtomIndex>(i)));
    if (xcb_connection_t* const pub_conn = wm::x11::connection()) {
        const Window pub_root = wm::x11::root_window();
        xcb_change_property(pub_conn, XCB_PROP_MODE_REPLACE, static_cast<xcb_window_t>(pub_root), static_cast<xcb_atom_t>(wm::x11::net_atom(NetSupported)), XCB_ATOM_ATOM, 32,
                            NetLast, supported);
        xcb_delete_property(pub_conn, static_cast<xcb_window_t>(pub_root), static_cast<xcb_atom_t>(wm::x11::net_atom(NetClientList)));
        xcb_delete_property(pub_conn, static_cast<xcb_window_t>(pub_root), static_cast<xcb_atom_t>(wm::x11::net_atom(NetZestFloatingClients)));
        zestwm_flush_connection();
    }
    savezestlayoutliststate();
    /* Subscribe to RandR events so monitor hotplug reaches the WM at runtime. */
    if (const xcb_query_extension_reply_t* rr = xcb_get_extension_data(wm::x11::connection(), &xcb_randr_id)) {
        if (rr->present) {
            xcb_handlers_set_randr_event_base(rr->first_event);
            xcb_randr_select_input(wm::x11::connection(), static_cast<xcb_window_t>(wm::x11::root_window()), XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE);
            zestwm_flush_connection();
        }
    }
    /* select events */
    wa.cursor = cursor[static_cast<std::size_t>(CursorKind::Normal)]->cursor;
    wa.event_mask =
        SubstructureRedirectMask | SubstructureNotifyMask | ButtonPressMask | PointerMotionMask | EnterWindowMask | LeaveWindowMask | StructureNotifyMask | PropertyChangeMask;
    wm::x11::change_window_attrs(wm::x11::root_window(), CWEventMask | CWCursor, wa);
    wm::x11::select_input(wm::x11::root_window(), wa.event_mask);
    grabkeys();
    if (!startup_restore_pending) {
        focus(nullptr);
        update_net_desktop_props();
    }
}
