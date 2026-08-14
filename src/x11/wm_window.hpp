/* XCB-native window geometry, attributes, and creation helpers. */
#pragma once

#include "x11/icccm_types.hpp"

#include <optional>

#include <xcb/xcb.h>
#include <xcb/xproto.h>

namespace wm::x11 {

    [[nodiscard]] std::optional<WindowInfo>   read_window_info(xcb_window_t window);
    [[nodiscard]] std::optional<xcb_window_t> read_transient_for(xcb_window_t window);

    void                                      raise_window(xcb_window_t window);
    void                                      move_window(xcb_window_t window, int x, int y);
    void                                      move_resize_window(xcb_window_t window, int x, int y, unsigned width, unsigned height);
    void                                      configure_window(xcb_window_t window, std::uint16_t mask, const WindowConfigure& changes);
    void                                      change_window_attrs(xcb_window_t window, std::uint32_t value_mask, const WindowAttrs& attrs);
    [[nodiscard]] bool                        try_select_input(xcb_window_t window, std::uint32_t event_mask);
    void                                      set_window_border(xcb_window_t window, std::uint32_t border_pixel);
    void                                      map_window(xcb_window_t window);
    void                                      unmap_window(xcb_window_t window);
    void                                      destroy_window(xcb_window_t window);
    void                                      define_cursor(xcb_window_t window, xcb_cursor_t cursor);
    void                                      map_raised(xcb_window_t window);
    void                                      set_input_focus(xcb_window_t window, std::uint8_t revert_to, xcb_timestamp_t time);
    void                                      select_input(xcb_window_t window, std::uint32_t event_mask);

    [[nodiscard]] xcb_screen_t*               screen_at(int screen_index);
    [[nodiscard]] xcb_visualtype_t*           visual_for_screen(xcb_screen_t* screen);
    [[nodiscard]] std::uint8_t                root_depth_for_screen(xcb_screen_t* screen);

    [[nodiscard]] xcb_window_t create_simple_window(xcb_window_t parent, int x, int y, unsigned width, unsigned height, unsigned border_width, std::uint32_t border_pixel,
                                                    std::uint32_t background_pixel);
    [[nodiscard]] xcb_window_t create_window(xcb_window_t parent, int x, int y, unsigned width, unsigned height, unsigned border_width, std::uint8_t depth,
                                             std::uint16_t window_class, xcb_visualid_t visual_id, std::uint32_t value_mask, const WindowAttrs& attrs);

} // namespace wm::x11
