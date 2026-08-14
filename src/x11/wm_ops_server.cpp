/*
 * XCB server-wide grab helpers for race-sensitive client transitions.
 *
 * Implements wm::x11::* from wm_server.hpp using connection() with global fallback.
 */
#include "x11/wm_server.hpp"
#include "x11/connection.hpp"
#include "x11/wm_ops.hpp"

#include <xcb/xproto.h>

namespace wm::x11 {

    void grab_server() {
        xcb_connection_t* conn = connection();
        if (!conn)
            return;
        xcb_grab_server(conn);
        zestwm_flush_connection();
    }

    void ungrab_server() {
        xcb_connection_t* conn = connection();
        if (!conn)
            return;
        xcb_ungrab_server(conn);
        zestwm_flush_connection();
    }

} // namespace wm::x11
