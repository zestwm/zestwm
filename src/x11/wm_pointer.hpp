/* XCB-native pointer query, grab, and event-queue sync helpers. */
#pragma once

#include "x11/icccm_types.hpp"

#include <optional>

#include <xcb/xcb.h>

namespace wm::x11 {

    void                                      sync(bool discard_pending_events = false);
    void                                      allow_events(int event_mode, xcb_timestamp_t time);

    [[nodiscard]] std::optional<PointerQuery> query_pointer(xcb_window_t window);
    [[nodiscard]] int  grab_pointer(xcb_window_t grab_window, bool owner_events, unsigned int event_mask, int pointer_mode, int keyboard_mode, xcb_window_t confine_to,
                                    xcb_cursor_t cursor, xcb_timestamp_t time);
    [[nodiscard]] bool ungrab_pointer(xcb_timestamp_t time);

} // namespace wm::x11
