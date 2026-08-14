/* zestwm entry point: main, event loop, cleanup, and client helpers. */
#include <cerrno>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <cstdint>
#include <xcb/randr.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>

#include "x11/backend.hpp"
#include "x11/connection.hpp"
#include "x11/xcb_props.hpp"
#include "x11/cursor_utils.hpp"
#include "workspace_id.hpp"
#include "workspace_ref.hpp"
#include "workspace_registry.hpp"
#include "util.hpp"
#include "config.hpp"
#include "log.hpp"

#include <cassert>
#include <exception>
#include <memory>
#include <vector>
#include <string>
#include <string_view>

#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xproto.h>
#include <X11/XF86keysym.h>

#include "x11/atoms.hpp"
#include "client/client_focus.hpp"
#include "client/client_lifecycle.hpp"
#include "draw/bar.hpp"
#include "dispatch/xcb_handlers.hpp"
#include "intern.hpp"
#include "monitor/monitor_lifecycle.hpp"
#include "dispatch/runtime_dispatch.hpp"
#include "state/wm_state_root.hpp"
#include "wm_state.hpp"
#include "x11/wm_input.hpp"
#include "x11/wm_ops.hpp"
#include "x11/wm_pointer.hpp"
#include "x11/wm_window.hpp"
#include "x11/wm_props.hpp"
#include "actions.hpp"
#include "actions/workspace.hpp"
#include "wm_startup.hpp"

#ifndef VERSION
#define VERSION "dev"
#endif

[[nodiscard]] std::optional<WorkspaceRef> consume_special_dispatch_workspace_ref() {
    xcb_connection_t* const conn     = wm::x11::connection();
    const Window            root_win = wm::x11::root_window();
    if (!conn || root_win == XCB_WINDOW_NONE)
        return std::nullopt;
    uint32_t   hidden_id      = 0U;
    const Atom special_hidden = wm::x11::net_atom(NetZestSpecialDispatchHiddenId);
    const int  got_hidden     = wm::x11::get_cardinal32(conn, static_cast<xcb_window_t>(root_win), static_cast<xcb_atom_t>(special_hidden), &hidden_id);
    xcb_delete_property(conn, static_cast<xcb_window_t>(root_win), static_cast<xcb_atom_t>(special_hidden));
    zestwm_flush_connection();
    if (!got_hidden)
        return std::nullopt;
    const auto target = workspace_special_ref_from_hidden_id(static_cast<WorkspaceId>(hidden_id));
    if (!target.has_value())
        return std::nullopt;
    return workspace_normalize_special_ref_with_hidden_id(*target);
}

/* function declarations (file-local) */
static void cleanup(void);
static void run(void);
static int  xerror(xcb_connection_t* xc, xcb_generic_error_t* ee);
/* Track currently held physical keys for repeat-gating of non-repeat binds. */

/* Flush pending X requests on the active backend connection. */
void zestwm_flush_connection(X11Backend& backend) {
    backend.flush();
}

/* Flush via TLS backend when set; otherwise fall back to connection() accessor. */
void zestwm_flush_connection(void) {
    if (X11Backend* backend_ctx = x11_backend_peek_context()) {
        backend_ctx->flush();
        return;
    }
    if (xcb_connection_t* conn = wm::x11::connection())
        xcb_flush(conn);
}

void cleanup(void) {
    Layout             foo = {"", nullptr};
    size_t             i;
    wm::state::WMState cu_rt    = wm::state::build_runtime_state_root();
    Monitor*           current  = cu_rt.monitors.current;
    xcb_connection_t*  conn     = wm::x11::connection();
    const Window       root_win = wm::x11::root_window();

    /* Reload/shutdown path: avoid focus/view transitions while tearing down state. */
    if (current)
        current->layout_slots[current->active_layout_slot & 1U] = &foo;
    for (Monitor* m : wm::state::all_monitors())
        while (!m->stack.empty())
            release_client(m->stack.front(), 0);
    static_cast<void>(wm::x11::ungrab_key(AnyKey, AnyModifier, root_win));
    while (wm::state::first_monitor())
        monitor_cleanup(wm::state::first_monitor());
    for (i = 0; i < kCursorSlotCount; i++)
        wm::x11::free_cursor(conn, cursor[i]);
    scheme.clear();
    wm::x11::destroy_window(wmcheckwin);
    draw_bar_shutdown();
    canvas.reset();
    canvas_font_height = 0U;
    if (cursor_ctx) {
        xcb_cursor_context_free(cursor_ctx);
        cursor_ctx = nullptr;
    }
    wm::x11::sync(false);
    wm::x11::set_input_focus(PointerRoot, RevertToPointerRoot, CurrentTime);
    if (conn) {
        xcb_delete_property(conn, static_cast<xcb_window_t>(root_win), static_cast<xcb_atom_t>(wm::x11::net_atom(NetActiveWindow)));
        zestwm_flush_connection();
    }
    wmconf_watch_shutdown();
    wmconf_free();
}

void ensure_workspace_registry_for_id(WorkspaceId id) {
    if (id < kWorkspaceIdMin)
        return;
    const std::size_t old_count = workspace_registry_count();
    workspace_registry_ensure_id(id);
    if (workspace_registry_count() <= old_count)
        return;
    grabkeys();
    update_net_desktop_props();
}

int getrootptr(int* x, int* y) {
    const auto ptr = wm::x11::query_pointer(wm::x11::root_window());
    if (!ptr || !ptr->same_screen)
        return 0;
    if (x)
        *x = ptr->root_x;
    if (y)
        *y = ptr->root_y;
    return 1;
}

static void run(void) {
    struct pollfd      pfd[2];
    int                nfds;
    int                rc;
    X11Backend*        backend_ctx;
    wm::state::WMState runtime_state = wm::state::build_runtime_state_root();

    wm::x11::sync(false);
    backend_ctx = x11_backend_peek_context();
    if (!backend_ctx || !backend_ctx->conn)
        die("zestwm: backend context not initialized");
    xcb_connection_t* const conn = wm::x11::connection();
    pfd[0].fd                    = xcb_get_file_descriptor(conn);
    pfd[0].events                = POLLIN;
    const int watch_fd           = wmconf_watch_inotify_fd();
    pfd[1].fd                    = watch_fd;
    pfd[1].events                = POLLIN;
    nfds                         = (watch_fd >= 0) ? 2 : 1;
    while (running) {
        wmconf_input_kb_reapply_poll();
        pfd[0].revents = 0;
        if (nfds > 1)
            pfd[1].revents = 0;
        rc = poll(pfd, nfds, wmconf_input_kb_reapply_poll_ms_cap(wmconf_watch_poll_timeout_ms()));
        wmconf_input_kb_reapply_poll();
        if (rc == 0) {
            wmconf_watch_maybe_reload();
            continue;
        }
        if (rc < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        wmconf_watch_maybe_reload();
        while (auto ev = backend_ctx->poll_event()) {
            uint8_t t = ev->response_type & 0x7f;
            if (t == 0) {
                xerror(conn, reinterpret_cast<xcb_generic_error_t*>(ev.get()));
                continue;
            }
            /* Route event mutation through centralized runtime dispatcher boundary. */
            wm::dispatch::runtime::dispatch_event(runtime_state, *backend_ctx, ev.get());
        }
    }
}

int sendevent(Client* c, Atom proto) {
    int exists = 0;

    for (const xcb_atom_t atom : wm::x11::read_wm_protocols(static_cast<xcb_window_t>(c->win))) {
        if (atom == static_cast<xcb_atom_t>(proto)) {
            exists = 1;
            break;
        }
    }
    if (exists)
        wm::x11::send_client_message(c->win, wm::x11::wm_atom(WMProtocols), 32, static_cast<long>(proto), static_cast<long>(CurrentTime), 0);
    return exists;
}

void updatenumlockmask(void) {
    unsigned int i, j;

    numlockmask = 0;
    if (!wm::x11::connection())
        return;
    const auto modmap = wm::x11::read_modifier_map();
    if (!modmap)
        return;
    if (modmap->max_keypermod <= 0 || modmap->keycodes.empty())
        return;
    const xcb_keycode_t numlock = wm::x11::keycode_for_keysym(XK_Num_Lock);
    for (i = 0; i < 8; i++)
        for (j = 0; j < static_cast<unsigned int>(modmap->max_keypermod); j++)
            if (modmap->at(static_cast<int>(i), static_cast<int>(j)) == numlock)
                numlockmask = (1 << i);
}

/* Ignore errors on destroyed windows; other protocol errors are logged. */
int xerror(xcb_connection_t* xc, xcb_generic_error_t* ee) {
    ignore_result(xc);
    if (ee->error_code == XCB_WINDOW /* BadWindow */
        || (ee->major_code == XCB_SET_INPUT_FOCUS && ee->error_code == XCB_MATCH) || (ee->major_code == XCB_POLY_TEXT_8 && ee->error_code == XCB_DRAWABLE) ||
        (ee->major_code == XCB_POLY_FILL_RECTANGLE && ee->error_code == XCB_DRAWABLE) || (ee->major_code == XCB_POLY_SEGMENT && ee->error_code == XCB_DRAWABLE) ||
        (ee->major_code == XCB_CONFIGURE_WINDOW && (ee->error_code == XCB_MATCH || ee->error_code == XCB_VALUE)) ||
        (ee->major_code == XCB_GRAB_BUTTON && ee->error_code == XCB_ACCESS) || (ee->major_code == XCB_GRAB_KEY && ee->error_code == XCB_ACCESS) ||
        (ee->major_code == XCB_COPY_AREA && ee->error_code == XCB_DRAWABLE))
        return 0;
    const std::string msg = std::string("zestwm: fatal error: request code=") + std::to_string(ee->major_code) + ", error code=" + std::to_string(ee->error_code);
    wm::log::warn_and_log(msg);
    exit(1);
}

/* Install session-owned runtime authority for the process lifetime; resets on exit. */
namespace {
    struct RuntimeAuthorityScope {
        explicit RuntimeAuthorityScope(wm::state::WMRuntimeAuthority& storage) noexcept {
            wm::state::install_runtime_authority(&storage);
        }
        RuntimeAuthorityScope(const RuntimeAuthorityScope&)            = delete;
        RuntimeAuthorityScope& operator=(const RuntimeAuthorityScope&) = delete;
        ~RuntimeAuthorityScope() noexcept {
            wm::state::install_runtime_authority(nullptr);
        }
    };
} // namespace

int main(int argc, char* argv[]) {
    try {
        const char* conffile       = nullptr;
        bool        skip_autostart = false;
        X11Backend  backend_ctx;

        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "-v")) {
                if (argc != 2)
                    die("usage: zestwm [-v] [-c path] [--reload]");
                die("zestwm-%s", VERSION);
            } else if (!strcmp(argv[i], "-c")) {
                if (++i >= argc)
                    die("usage: zestwm [-v] [-c path] [--reload]");
                conffile = argv[i];
            } else if (!strcmp(argv[i], "--reload")) {
                skip_autostart = true;
            } else
                die("usage: zestwm [-v] [-c path] [--reload]");
        }
        wm::state::WMRuntimeAuthority session_runtime_authority{};
        const RuntimeAuthorityScope   session_runtime_owner(session_runtime_authority);
        wmconf_load(conffile);
        if (!setlocale(LC_CTYPE, ""))
            wm::log::warn_and_log("warning: no locale support");
        xcb = xcb_connect(nullptr, &screen);
        if (xcb_connection_has_error(xcb))
            die("zestwm: cannot open display");
        backend_ctx.conn   = xcb;
        backend_ctx.screen = wm::x11::screen_at(screen);
        backend_ctx.root   = backend_ctx.screen ? backend_ctx.screen->root : static_cast<Window>(XCB_WINDOW_NONE);
        x11_backend_set_context(backend_ctx);
        wmconf_watch_init();
        startup_restore_pending = 1;
        setup();
        showconfigerrorbanner();
        if (!skip_autostart)
            wmconf_autostart(xcb);
        restorezestselectionstate();
        scan();
        restorezesttreestate();
        restorezestselectionstate();
        restorezestspecialoverlaystate();
        startup_restore_pending = 0;
        arrange(nullptr);
        {
            Client* focused_after = nullptr;
            for (Monitor* m : wm::state::all_monitors()) {
                if (m->special_overlay_open)
                    wm_focus_first_special_overlay_client(m);
                if (m->sel)
                    focused_after = m->sel;
            }
            if (!focused_after)
                focus(nullptr);
        }
        update_net_desktop_props();
        run();
        if (restart) {
            xcb_disconnect(xcb);
            constexpr int kMaxReloadArgv = 64;
            char*         reload_argv[kMaxReloadArgv];
            int           reload_argc = 0;
            bool          has_reload  = false;
            for (int i = 0; i < argc && reload_argc < kMaxReloadArgv - 2; ++i) {
                reload_argv[reload_argc++] = argv[i];
                if (!strcmp(argv[i], "--reload"))
                    has_reload = true;
            }
            static char reload_flag[] = "--reload";
            if (!has_reload)
                reload_argv[reload_argc++] = reload_flag;
            reload_argv[reload_argc] = nullptr;
            execvp(argv[0], reload_argv);
            return EXIT_FAILURE;
        }
        cleanup();
        xcb_disconnect(xcb);
        return EXIT_SUCCESS;
    } catch (const std::exception& e) { die("zestwm: fatal: %s", e.what()); } catch (...) {
        die("zestwm: fatal error");
    }
}
