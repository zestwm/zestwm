/* Active X11 connection accessors (backend context with global fallback). */
#pragma once

#include "x11/backend.hpp"

#include <xcb/xcb.h>

extern xcb_connection_t* xcb;
extern xcb_screen_t*     xscreen;
extern Window            root;

namespace wm::x11 {

    /* Return backend connection when set, otherwise the process-global fallback. */
    [[nodiscard]] inline xcb_connection_t* connection() noexcept {
        if (X11Backend* backend = x11_backend_peek_context(); backend && backend->conn)
            return backend->conn;
        return xcb;
    }

    /* Return backend screen when set, otherwise the process-global fallback. */
    [[nodiscard]] inline xcb_screen_t* default_screen() noexcept {
        if (X11Backend* backend = x11_backend_peek_context(); backend && backend->screen)
            return backend->screen;
        return xscreen;
    }

    /* Return backend root window when set, otherwise the process-global fallback. */
    [[nodiscard]] inline Window root_window() noexcept {
        if (X11Backend* backend = x11_backend_peek_context(); backend && backend->root != XCB_WINDOW_NONE)
            return backend->root;
        return root;
    }

} // namespace wm::x11
