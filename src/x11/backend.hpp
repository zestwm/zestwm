/* XCB connection context, atom cache, and event queue (no Xlib). */
#pragma once

#include <deque>
#include <initializer_list>
#include <string>
#include <unordered_map>
#include <cstdint>
#include <xcb/xcb.h>

#include "x11/reply_ptr.hpp"

/* Protocol constants (values match X11/X.h; no Xlib resource typedefs). */
#include "x11/constants.hpp" // IWYU pragma: export

/* ICCCM size / WM hint bits (avoid Xutil.h which pulls Xlib). */
inline constexpr long PMinSize       = (1L << 4);
inline constexpr long PMaxSize       = (1L << 5);
inline constexpr long PResizeInc     = (1L << 6);
inline constexpr long PAspect        = (1L << 7);
inline constexpr long PBaseSize      = (1L << 8);
inline constexpr long PSize          = (1L << 3);
inline constexpr long InputHint      = (1L << 0);
inline constexpr long XUrgencyHint   = (1L << 8);
inline constexpr int  WithdrawnState = 0;
inline constexpr int  NormalState    = 1;
inline constexpr int  IconicState    = 3;

using Window   = xcb_window_t;
using Atom     = xcb_atom_t;
using Time     = xcb_timestamp_t;
using KeySym   = xcb_keysym_t;
using KeyCode  = xcb_keycode_t;
using Cursor   = xcb_cursor_t;
using Pixmap   = xcb_pixmap_t;
using Colormap = xcb_colormap_t;
using XID      = std::uint32_t;

using Bool                  = int;
using Status                = int;
inline constexpr Bool True  = 1;
inline constexpr Bool False = 0;

struct X11Backend {
    using BackendEventPtr = XcbReplyPtr<xcb_generic_event_t>;

    xcb_connection_t*                           conn   = nullptr;
    xcb_screen_t*                               screen = nullptr;
    xcb_window_t                                root   = XCB_WINDOW_NONE;

    std::deque<BackendEventPtr>                 event_queue;
    std::unordered_map<std::string, xcb_atom_t> atom_cache;

    explicit X11Backend(std::initializer_list<const char*> pre_intern = {});
    ~X11Backend();
    X11Backend(const X11Backend&)              = delete;
    X11Backend& operator=(const X11Backend&)   = delete;
    X11Backend(X11Backend&&)                   = delete;
    X11Backend&        operator=(X11Backend&&) = delete;

    [[nodiscard]] bool connect(const char* display = nullptr);
    void               warmup_atoms(std::initializer_list<const char*> atoms);
    void               disconnect();
    /* Blocking wait for next queued/arriving event matching mask; ownership via unique_ptr. */
    [[nodiscard]] BackendEventPtr wait_masked_event(uint32_t mask) noexcept;
    /* Non-blocking pop of first queued event matching mask; ownership via unique_ptr. */
    [[nodiscard]] BackendEventPtr poll_masked_event(uint32_t mask) noexcept;
    /* Next event after draining the XCB read buffer into event_queue; ownership via unique_ptr. */
    [[nodiscard]] BackendEventPtr poll_event() noexcept;
    [[nodiscard]] bool            drain_latest_motion(xcb_motion_notify_event_t& out) noexcept;
    [[nodiscard]] xcb_atom_t      get_atom(const char* name, bool only_if_exists = false);
    void                          flush() noexcept;
};

void x11_backend_set_context(X11Backend& ctx);
/* Thread-local backend lookup; returns nullptr when no context is installed. */
X11Backend* x11_backend_peek_context() noexcept;
