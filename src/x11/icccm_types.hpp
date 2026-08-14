/* ICCCM/EWMH property payloads modeled with XCB-native types and STL ownership. */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <xcb/xcb.h>

namespace wm::x11 {

    struct ClassHint {
        std::string instance;
        std::string res_class;
    };

    struct WmHints {
        std::uint32_t flags         = 0U;
        bool          input         = false;
        int           initial_state = 0;
        xcb_pixmap_t  icon_pixmap   = XCB_PIXMAP_NONE;
        xcb_window_t  icon_window   = XCB_WINDOW_NONE;
        int           icon_x        = 0;
        int           icon_y        = 0;
        xcb_pixmap_t  icon_mask     = XCB_PIXMAP_NONE;
        std::uint32_t window_group  = 0U;
    };

    struct AspectPair {
        int x = 0;
        int y = 0;
    };

    struct SizeHints {
        std::uint32_t flags      = 0U;
        int           x          = 0;
        int           y          = 0;
        int           width      = 0;
        int           height     = 0;
        int           min_width  = 0;
        int           min_height = 0;
        int           max_width  = 0;
        int           max_height = 0;
        int           width_inc  = 0;
        int           height_inc = 0;
        AspectPair    min_aspect{};
        AspectPair    max_aspect{};
        int           base_width  = 0;
        int           base_height = 0;
        int           win_gravity = 0;
    };

    struct PropertyBytes {
        xcb_atom_t                type   = XCB_ATOM_NONE;
        std::uint8_t              format = 0U;
        std::vector<std::uint8_t> data;
    };

    struct TextProperty {
        xcb_atom_t                encoding = XCB_ATOM_NONE;
        std::uint8_t              format   = 0U;
        std::vector<std::uint8_t> value;
    };

    struct QueryTree {
        xcb_window_t              root   = XCB_WINDOW_NONE;
        xcb_window_t              parent = XCB_WINDOW_NONE;
        std::vector<xcb_window_t> children;
    };

    /* Geometry + map state from get_geometry/get_window_attributes. */
    struct WindowInfo {
        int           x                 = 0;
        int           y                 = 0;
        int           width             = 0;
        int           height            = 0;
        int           border_width      = 0;
        int           depth             = 0;
        xcb_window_t  root              = XCB_WINDOW_NONE;
        std::uint8_t  map_state         = 0U;
        bool          override_redirect = false;
        std::uint16_t window_class      = 0U;
    };

    /* Fields for configure_window value_mask updates. */
    struct WindowConfigure {
        int          x            = 0;
        int          y            = 0;
        int          width        = 0;
        int          height       = 0;
        int          border_width = 0;
        xcb_window_t sibling      = XCB_WINDOW_NONE;
        int          stack_mode   = 0;
    };

    /* create_window / change_window_attributes payload. */
    struct WindowAttrs {
        xcb_pixmap_t   background_pixmap     = XCB_PIXMAP_NONE;
        std::uint32_t  background_pixel      = 0U;
        xcb_pixmap_t   border_pixmap         = XCB_PIXMAP_NONE;
        std::uint32_t  border_pixel          = 0U;
        int            bit_gravity           = 0;
        int            win_gravity           = 0;
        int            backing_store         = 0;
        std::uint32_t  backing_planes        = 0U;
        std::uint32_t  backing_pixel         = 0U;
        bool           save_under            = false;
        std::uint32_t  event_mask            = 0U;
        std::uint32_t  do_not_propagate_mask = 0U;
        bool           override_redirect     = false;
        xcb_colormap_t colormap              = XCB_COLORMAP_NONE;
        xcb_cursor_t   cursor                = XCB_CURSOR_NONE;
    };

    struct ModifierMap {
        int                        max_keypermod = 0;
        std::vector<xcb_keycode_t> keycodes;

        /* Lookup keycode at modifier slot `mod` and index `slot`; returns 0 when out of range. */
        [[nodiscard]] xcb_keycode_t at(int mod, int slot) const noexcept {
            if (max_keypermod <= 0 || mod < 0 || slot < 0 || slot >= max_keypermod)
                return 0;
            const std::size_t idx = static_cast<std::size_t>(mod) * static_cast<std::size_t>(max_keypermod) + static_cast<std::size_t>(slot);
            if (idx >= keycodes.size())
                return 0;
            return keycodes[idx];
        }
    };

    struct KeyboardMapping {
        int                       keysyms_per_keycode = 0;
        std::vector<xcb_keysym_t> keysyms;
    };

    /* Result of xcb_query_pointer for hit-testing and mouse routing. */
    struct PointerQuery {
        xcb_window_t  root        = XCB_WINDOW_NONE;
        xcb_window_t  child       = XCB_WINDOW_NONE;
        int           root_x      = 0;
        int           root_y      = 0;
        int           win_x       = 0;
        int           win_y       = 0;
        std::uint16_t mask        = 0;
        bool          same_screen = false;
    };

} // namespace wm::x11
