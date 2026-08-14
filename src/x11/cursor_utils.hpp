/*
 * Cursor helper API for X11/XCB integration.
 *
 * Responsibilities:
 * - Create themed cursors with graceful fallback names.
 * - Keep cursor ownership explicit through create/free boundary helpers.
 */
#pragma once

#include <xcb/xcb.h>
#include <xcb/xcb_cursor.h>

struct Cur {
    xcb_cursor_t cursor;
};

namespace wm::x11 {

    /* Build cursor from X cursor context and symbolic shape id. */
    [[nodiscard]] Cur* create_cursor(xcb_cursor_context_t* cursor_ctx, int shape);
    /* Destroy cursor handle and release wrapper allocation. */
    void free_cursor(xcb_connection_t* conn, Cur* cursor);

} // namespace wm::x11
