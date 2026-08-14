/*
 * X11 backend runtime: connection ownership, atom cache, and event queue.
 *
 * Design notes:
 * - This translation unit is intentionally XCB-first and exception-free.
 * - Event helpers return raw pointers only at C boundary; ownership is
 *   transferred to caller, matching existing project conventions.
 * - Queue stores RAII wrappers to keep early-return/error paths leak-free.
 */
#include "x11/backend.hpp"
#include "x11/reply_ptr.hpp"

#include <cstring>
#include <string>
#include <vector>

#include <xcb/xcb.h>

static thread_local X11Backend* g_backend_ctx = nullptr;
static uint32_t                 xcb_event_type_to_mask(uint8_t type) noexcept;
static constexpr std::uint8_t   kXcbResponseTypeMask = 0x7f;

X11Backend*                     x11_backend_peek_context() noexcept {
    return g_backend_ctx;
}

void x11_backend_set_context(X11Backend& ctx) {
    g_backend_ctx = &ctx;
}

/* Build backend state and optionally pre-intern startup atoms. */
X11Backend::X11Backend(std::initializer_list<const char*> pre_intern) {
    warmup_atoms(pre_intern);
}

/* Release connection-owned resources and close the XCB connection. */
X11Backend::~X11Backend() {
    disconnect();
}

/*
 * Map XCB response type to X11 mask bitfield used by wait/poll selectors.
 * Unknown events map to zero mask and are ignored by masked consumers.
 */
static uint32_t xcb_event_type_to_mask(uint8_t type) noexcept {
    switch (type & kXcbResponseTypeMask) {
        case XCB_KEY_PRESS: return KeyPressMask;
        case XCB_KEY_RELEASE: return KeyReleaseMask;
        case XCB_BUTTON_PRESS: return ButtonPressMask;
        case XCB_BUTTON_RELEASE: return ButtonReleaseMask;
        case XCB_MOTION_NOTIFY: return PointerMotionMask;
        case XCB_ENTER_NOTIFY: return EnterWindowMask;
        case XCB_LEAVE_NOTIFY: return LeaveWindowMask;
        case XCB_FOCUS_IN:
        case XCB_FOCUS_OUT: return FocusChangeMask;
        case XCB_KEYMAP_NOTIFY: return KeymapStateMask;
        case XCB_EXPOSE:
        case XCB_GRAPHICS_EXPOSURE:
        case XCB_NO_EXPOSURE: return ExposureMask;
        case XCB_VISIBILITY_NOTIFY: return VisibilityChangeMask;
        case XCB_CREATE_NOTIFY:
        case XCB_DESTROY_NOTIFY:
        case XCB_UNMAP_NOTIFY:
        case XCB_MAP_NOTIFY:
        case XCB_REPARENT_NOTIFY:
        case XCB_CONFIGURE_NOTIFY:
        case XCB_GRAVITY_NOTIFY:
        case XCB_CIRCULATE_NOTIFY: return SubstructureNotifyMask | StructureNotifyMask;
        case XCB_MAP_REQUEST:
        case XCB_CONFIGURE_REQUEST:
        case XCB_CIRCULATE_REQUEST: return SubstructureRedirectMask;
        case XCB_RESIZE_REQUEST: return ResizeRedirectMask;
        case XCB_PROPERTY_NOTIFY: return PropertyChangeMask;
        case XCB_COLORMAP_NOTIFY: return ColormapChangeMask;
        case XCB_CLIENT_MESSAGE: return SubstructureNotifyMask | SubstructureRedirectMask;
        default: return 0;
    }
}

/*
 * Drain currently pending events from XCB socket into backend queue.
 * Non-blocking: stops as soon as poll reports no more events.
 */
static void backend_drain_socket_buffer(X11Backend& ctx) noexcept {
    if (!ctx.conn)
        return;
    for (;;) {
        auto ev = make_xcb_reply_ptr(xcb_poll_for_event(ctx.conn));
        if (!ev)
            break;
        ctx.event_queue.push_back(std::move(ev));
    }
}

/*
 * Resolve screen descriptor by screen index from setup roots iterator.
 * Returns nullptr on invalid connection/setup/index exhaustion.
 */
static xcb_screen_t* backend_screen_of_display(xcb_connection_t* conn, int screen) {
    const xcb_setup_t*    setup;
    xcb_screen_iterator_t it;
    int                   i;

    if (!conn)
        return nullptr;
    setup = xcb_get_setup(conn);
    if (!setup)
        return nullptr;
    it = xcb_setup_roots_iterator(setup);
    for (i = 0; it.rem && i < screen; i++)
        xcb_screen_next(&it);
    return it.data;
}

/*
 * Open XCB connection and cache active screen/root handles.
 * On any failure path it leaves backend in fully disconnected state.
 */
bool X11Backend::connect(const char* display) {
    disconnect();
    int screen_num = 0;
    conn           = xcb_connect(display, &screen_num);
    if (!conn || xcb_connection_has_error(conn)) {
        disconnect();
        return false;
    }
    screen = backend_screen_of_display(conn, screen_num);
    root   = screen ? screen->root : static_cast<Window>(XCB_WINDOW_NONE);
    return screen != nullptr;
}

/*
 * Batch-intern startup atom list and store resolved IDs in atom_cache.
 * Cache key is atom string; duplicate names are skipped in-place.
 */
void X11Backend::warmup_atoms(std::initializer_list<const char*> atoms) {
    if (!conn || atoms.size() == 0)
        return;
    std::vector<std::pair<std::string, xcb_intern_atom_cookie_t>> cookies;
    cookies.reserve(atoms.size());
    for (const char* atom_name : atoms) {
        if (!atom_name || atom_cache.find(atom_name) != atom_cache.end())
            continue;
        const auto len = static_cast<uint16_t>(strlen(atom_name));
        cookies.emplace_back(atom_name, xcb_intern_atom(conn, 0, len, atom_name));
    }
    for (auto& [name, cookie] : cookies) {
        auto reply = make_xcb_reply_ptr(xcb_intern_atom_reply(conn, cookie, nullptr));
        if (reply)
            atom_cache[name] = reply->atom;
    }
}

/*
 * Clear transient state and close active connection if present.
 * Safe to call repeatedly; acts as idempotent backend reset.
 */
void X11Backend::disconnect() {
    event_queue.clear();
    atom_cache.clear();
    screen = nullptr;
    root   = XCB_WINDOW_NONE;
    if (!conn)
        return;
    xcb_disconnect(conn);
    conn = nullptr;
}

/*
 * Blocking wait for next queued/arriving event matching mask.
 * Drains socket first, then waits for fresh event only when needed.
 */
X11Backend::BackendEventPtr X11Backend::wait_masked_event(uint32_t mask) noexcept {
    if (!conn)
        return nullptr;
    for (;;) {
        backend_drain_socket_buffer(*this);
        for (auto it = event_queue.begin(); it != event_queue.end();) {
            auto* ev = it->get();
            if (!ev) {
                it = event_queue.erase(it);
                continue;
            }
            uint8_t t = ev->response_type & kXcbResponseTypeMask;
            if (t == 0) {
                it = event_queue.erase(it);
                continue;
            }
            const auto have = xcb_event_type_to_mask(t);
            if (have && (have & mask)) {
                auto out = std::move(*it);
                event_queue.erase(it);
                return out;
            }
            ++it;
        }
        auto ev = make_xcb_reply_ptr(xcb_wait_for_event(conn));
        if (!ev)
            return nullptr;
        event_queue.push_back(std::move(ev));
    }
}

/*
 * Non-blocking pop of next event regardless of mask.
 * Returns nullptr when queue is empty after a socket drain pass.
 */
X11Backend::BackendEventPtr X11Backend::poll_event() noexcept {
    if (!conn)
        return nullptr;
    backend_drain_socket_buffer(*this);
    while (!event_queue.empty()) {
        auto ev = std::move(event_queue.front());
        event_queue.pop_front();
        if (!ev)
            continue;
        return ev;
    }
    return nullptr;
}

/*
 * Non-blocking pop of first event matching mask.
 * Keeps non-matching events queued to preserve ordering for other consumers.
 */
X11Backend::BackendEventPtr X11Backend::poll_masked_event(uint32_t mask) noexcept {
    if (!conn)
        return nullptr;
    backend_drain_socket_buffer(*this);
    for (auto it = event_queue.begin(); it != event_queue.end();) {
        auto* ev = it->get();
        if (!ev) {
            it = event_queue.erase(it);
            continue;
        }
        uint8_t t = ev->response_type & kXcbResponseTypeMask;
        if (t == 0) {
            it = event_queue.erase(it);
            continue;
        }
        const auto have = xcb_event_type_to_mask(t);
        if (have && (have & mask)) {
            auto out = std::move(*it);
            event_queue.erase(it);
            return out;
        }
        ++it;
    }
    return nullptr;
}

/*
 * Collapse queued motion events to latest sample.
 * Useful for drag/move flows where stale motion events are redundant noise.
 */
bool X11Backend::drain_latest_motion(xcb_motion_notify_event_t& out) noexcept {
    bool drained = false;
    backend_drain_socket_buffer(*this);
    for (auto it = event_queue.begin(); it != event_queue.end();) {
        auto* ev = it->get();
        if (!ev) {
            it = event_queue.erase(it);
            continue;
        }
        if ((ev->response_type & kXcbResponseTypeMask) == XCB_MOTION_NOTIFY) {
            const auto* motion = reinterpret_cast<const xcb_motion_notify_event_t*>(ev);
            out                = *motion;
            drained            = true;
            it                 = event_queue.erase(it);
            continue;
        }
        ++it;
    }
    return drained;
}

/*
 * Resolve atom by name with cache fast-path.
 * Returns XCB_ATOM_NONE when connection missing or intern reply fails.
 */
xcb_atom_t X11Backend::get_atom(const char* name, bool only_if_exists) {
    if (!conn || !name)
        return XCB_ATOM_NONE;
    auto cached = atom_cache.find(name);
    if (cached != atom_cache.end())
        return cached->second;
    const auto len   = static_cast<uint16_t>(strlen(name));
    const auto ck    = xcb_intern_atom(conn, only_if_exists ? 1 : 0, len, name);
    auto       reply = make_xcb_reply_ptr(xcb_intern_atom_reply(conn, ck, nullptr));
    if (!reply)
        return XCB_ATOM_NONE;
    atom_cache.emplace(name, reply->atom);
    return reply->atom;
}

/* Flush pending outbound XCB requests on active connection. */
void X11Backend::flush() noexcept {
    if (conn)
        xcb_flush(conn);
}
