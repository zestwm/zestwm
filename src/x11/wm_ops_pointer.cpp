/*
 * XCB-native pointer query, grab, and event-queue sync helpers.
 *
 * Implements wm::x11::* from wm_pointer.hpp using connection() with global fallback.
 */
#include "x11/wm_pointer.hpp"
#include "x11/connection.hpp"
#include "x11/reply_ptr.hpp"
#include "x11/wm_ops.hpp"

#include <cstdint>

#include <xcb/xproto.h>

namespace wm::x11 {

    void sync(bool discard_pending_events) {
        xcb_connection_t* conn = connection();
        if (!conn)
            return;

        if (discard_pending_events) {
            for (;;) {
                auto event = make_xcb_reply_ptr(xcb_poll_for_event(conn));
                if (!event)
                    break;
            }
            zestwm_flush_connection();
            return;
        }

        zestwm_flush_connection();
        const xcb_get_input_focus_cookie_t cookie = xcb_get_input_focus(conn);
        [[maybe_unused]] auto              reply  = make_xcb_reply_ptr(xcb_get_input_focus_reply(conn, cookie, nullptr));
    }

    void allow_events(int event_mode, xcb_timestamp_t time) {
        xcb_connection_t* conn = connection();
        if (!conn)
            return;
        xcb_allow_events(conn, static_cast<std::uint8_t>(event_mode), time);
        zestwm_flush_connection();
    }

    std::optional<PointerQuery> query_pointer(xcb_window_t window) {
        xcb_connection_t* conn = connection();
        if (!conn)
            return std::nullopt;

        const xcb_query_pointer_cookie_t cookie = xcb_query_pointer(conn, window);
        auto                             reply  = make_xcb_reply_ptr(xcb_query_pointer_reply(conn, cookie, nullptr));
        if (!reply)
            return std::nullopt;

        PointerQuery out{};
        out.root        = reply->root;
        out.child       = reply->child;
        out.root_x      = reply->root_x;
        out.root_y      = reply->root_y;
        out.win_x       = reply->win_x;
        out.win_y       = reply->win_y;
        out.mask        = reply->mask;
        out.same_screen = reply->same_screen != 0;
        return out;
    }

    int grab_pointer(xcb_window_t grab_window, bool owner_events, unsigned int event_mask, int pointer_mode, int keyboard_mode, xcb_window_t confine_to, xcb_cursor_t cursor,
                     xcb_timestamp_t time) {
        xcb_connection_t* conn = connection();
        if (!conn)
            return GrabNotViewable;

        const xcb_grab_pointer_cookie_t cookie = xcb_grab_pointer(conn, owner_events ? 1 : 0, grab_window, static_cast<std::uint16_t>(event_mask),
                                                                  static_cast<std::uint8_t>(pointer_mode), static_cast<std::uint8_t>(keyboard_mode), confine_to, cursor, time);
        auto                            reply  = make_xcb_reply_ptr(xcb_grab_pointer_reply(conn, cookie, nullptr));
        if (!reply)
            return GrabFrozen;

        const int status = reply->status;
        zestwm_flush_connection();
        return status;
    }

    bool ungrab_pointer(xcb_timestamp_t time) {
        xcb_connection_t* conn = connection();
        if (!conn)
            return false;
        xcb_ungrab_pointer(conn, time);
        zestwm_flush_connection();
        return true;
    }

} // namespace wm::x11
