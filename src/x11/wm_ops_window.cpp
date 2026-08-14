/*
 * XCB-native window geometry, attributes, and creation helpers.
 *
 * Implements wm::x11::* from wm_window.hpp using connection() with global fallback.
 */
#include "x11/wm_window.hpp"
#include "x11/connection.hpp"
#include "x11/reply_ptr.hpp"

#include <cstdint>

#include <X11/Xatom.h>
#include <xcb/xproto.h>

namespace wm::x11 {

    namespace {

        /* Pack WindowAttrs fields in X11 wire order for valuemask bits. */
        int pack_window_attributes_values(std::uint32_t valuemask, const WindowAttrs& wa, std::uint32_t* values) {
            int idx = 0;
            if (valuemask & CWBackPixmap)
                values[idx++] = static_cast<std::uint32_t>(wa.background_pixmap);
            if (valuemask & CWBackPixel)
                values[idx++] = wa.background_pixel;
            if (valuemask & CWBorderPixmap)
                values[idx++] = static_cast<std::uint32_t>(wa.border_pixmap);
            if (valuemask & CWBorderPixel)
                values[idx++] = wa.border_pixel;
            if (valuemask & CWBitGravity)
                values[idx++] = static_cast<std::uint32_t>(wa.bit_gravity);
            if (valuemask & CWWinGravity)
                values[idx++] = static_cast<std::uint32_t>(wa.win_gravity);
            if (valuemask & CWBackingStore)
                values[idx++] = static_cast<std::uint32_t>(wa.backing_store);
            if (valuemask & CWBackingPlanes)
                values[idx++] = wa.backing_planes;
            if (valuemask & CWBackingPixel)
                values[idx++] = wa.backing_pixel;
            if (valuemask & CWOverrideRedirect)
                values[idx++] = wa.override_redirect ? 1U : 0U;
            if (valuemask & CWSaveUnder)
                values[idx++] = wa.save_under ? 1U : 0U;
            if (valuemask & CWEventMask)
                values[idx++] = wa.event_mask;
            if (valuemask & CWDontPropagate)
                values[idx++] = wa.do_not_propagate_mask;
            if (valuemask & CWColormap)
                values[idx++] = static_cast<std::uint32_t>(wa.colormap);
            if (valuemask & CWCursor)
                values[idx++] = static_cast<std::uint32_t>(wa.cursor);
            return idx;
        }

    } // namespace

    std::optional<WindowInfo> read_window_info(xcb_window_t window) {
        xcb_connection_t* conn = connection();
        if (!conn)
            return std::nullopt;

        const xcb_get_window_attributes_cookie_t acookie = xcb_get_window_attributes(conn, window);
        const xcb_get_geometry_cookie_t          gcookie = xcb_get_geometry(conn, window);
        auto                                     areply  = make_xcb_reply_ptr(xcb_get_window_attributes_reply(conn, acookie, nullptr));
        auto                                     greply  = make_xcb_reply_ptr(xcb_get_geometry_reply(conn, gcookie, nullptr));
        if (!areply || !greply)
            return std::nullopt;

        WindowInfo info{};
        info.x                 = greply->x;
        info.y                 = greply->y;
        info.width             = greply->width;
        info.height            = greply->height;
        info.border_width      = greply->border_width;
        info.depth             = greply->depth;
        info.root              = greply->root;
        info.map_state         = areply->map_state;
        info.override_redirect = areply->override_redirect != 0;
        info.window_class      = areply->_class;
        return info;
    }

    std::optional<xcb_window_t> read_transient_for(xcb_window_t window) {
        xcb_connection_t* conn = connection();
        if (!conn)
            return std::nullopt;

        const xcb_get_property_cookie_t cookie = xcb_get_property(conn, 0, window, XCB_ATOM_WM_TRANSIENT_FOR, XCB_ATOM_WINDOW, 0, 1);
        auto                            reply  = make_xcb_reply_ptr(xcb_get_property_reply(conn, cookie, nullptr));
        if (!reply || reply->format != 32 || xcb_get_property_value_length(reply.get()) < static_cast<int>(sizeof(xcb_window_t)))
            return std::nullopt;

        const auto* value = static_cast<const xcb_window_t*>(xcb_get_property_value(reply.get()));
        return value[0];
    }

    void raise_window(xcb_window_t window) {
        xcb_connection_t* conn = connection();
        if (!conn)
            return;
        const std::uint32_t values[] = {XCB_STACK_MODE_ABOVE};
        xcb_configure_window(conn, window, XCB_CONFIG_WINDOW_STACK_MODE, values);
    }

    void move_window(xcb_window_t window, int x, int y) {
        xcb_connection_t* conn = connection();
        if (!conn)
            return;
        const std::uint32_t values[] = {static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)};
        xcb_configure_window(conn, window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, values);
    }

    void move_resize_window(xcb_window_t window, int x, int y, unsigned width, unsigned height) {
        xcb_connection_t* conn = connection();
        if (!conn)
            return;
        const std::uint32_t values[] = {static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), width, height};
        xcb_configure_window(conn, window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
    }

    void configure_window(xcb_window_t window, std::uint16_t mask, const WindowConfigure& changes) {
        xcb_connection_t* conn = connection();
        if (!conn || mask == 0U)
            return;

        std::uint32_t values[7];
        int           idx = 0;
        if (mask & CWX)
            values[idx++] = static_cast<std::uint32_t>(changes.x);
        if (mask & CWY)
            values[idx++] = static_cast<std::uint32_t>(changes.y);
        if (mask & CWWidth)
            values[idx++] = static_cast<std::uint32_t>(changes.width);
        if (mask & CWHeight)
            values[idx++] = static_cast<std::uint32_t>(changes.height);
        if (mask & CWBorderWidth)
            values[idx++] = static_cast<std::uint32_t>(changes.border_width);
        if (mask & CWSibling)
            values[idx++] = static_cast<std::uint32_t>(changes.sibling);
        if (mask & CWStackMode)
            values[idx++] = static_cast<std::uint32_t>(changes.stack_mode);
        if (idx == 0)
            return;
        xcb_configure_window(conn, window, mask, values);
    }

    void change_window_attrs(xcb_window_t window, std::uint32_t value_mask, const WindowAttrs& attrs) {
        xcb_connection_t* conn = connection();
        if (!conn || value_mask == 0U)
            return;

        std::uint32_t values[15];
        const int     idx = pack_window_attributes_values(value_mask, attrs, values);
        if (idx == 0)
            return;
        xcb_change_window_attributes(conn, window, value_mask, values);
    }

    bool try_select_input(xcb_window_t window, std::uint32_t event_mask) {
        xcb_connection_t* conn = connection();
        if (!conn)
            return false;

        const std::uint32_t     values[] = {event_mask};
        const xcb_void_cookie_t cookie   = xcb_change_window_attributes_checked(conn, window, XCB_CW_EVENT_MASK, values);
        auto                    error    = make_xcb_reply_ptr(xcb_request_check(conn, cookie));
        return !error;
    }

    void set_window_border(xcb_window_t window, std::uint32_t border_pixel) {
        xcb_connection_t* conn = connection();
        if (!conn)
            return;
        const std::uint32_t values[] = {border_pixel};
        xcb_change_window_attributes(conn, window, XCB_CW_BORDER_PIXEL, values);
    }

    void map_window(xcb_window_t window) {
        xcb_connection_t* conn = connection();
        if (!conn)
            return;
        xcb_map_window(conn, window);
    }

    void unmap_window(xcb_window_t window) {
        xcb_connection_t* conn = connection();
        if (!conn)
            return;
        xcb_unmap_window(conn, window);
    }

    void destroy_window(xcb_window_t window) {
        xcb_connection_t* conn = connection();
        if (!conn)
            return;
        xcb_destroy_window(conn, window);
    }

    xcb_screen_t* screen_at(int screen_index) {
        xcb_connection_t* conn = connection();
        if (!conn)
            return nullptr;

        const xcb_setup_t* setup = xcb_get_setup(conn);
        if (!setup)
            return nullptr;

        xcb_screen_iterator_t it = xcb_setup_roots_iterator(setup);
        for (int i = 0; it.rem && i < screen_index; i++)
            xcb_screen_next(&it);
        return it.data;
    }

    xcb_visualtype_t* visual_for_screen(xcb_screen_t* screen) {
        if (!screen)
            return nullptr;

        xcb_depth_iterator_t dit = xcb_screen_allowed_depths_iterator(screen);
        for (; dit.rem; xcb_depth_next(&dit)) {
            xcb_visualtype_iterator_t vit = xcb_depth_visuals_iterator(dit.data);
            for (; vit.rem; xcb_visualtype_next(&vit)) {
                if (vit.data->visual_id == screen->root_visual)
                    return vit.data;
            }
        }
        return nullptr;
    }

    std::uint8_t root_depth_for_screen(xcb_screen_t* screen) {
        return screen ? screen->root_depth : 0;
    }

    xcb_window_t create_simple_window(xcb_window_t parent, int x, int y, unsigned width, unsigned height, unsigned border_width, std::uint32_t border_pixel,
                                      std::uint32_t background_pixel) {
        xcb_connection_t* conn = connection();
        if (!conn)
            return XCB_WINDOW_NONE;

        const xcb_window_t  wid      = xcb_generate_id(conn);
        const std::uint32_t values[] = {background_pixel, border_pixel};
        xcb_create_window(conn, XCB_COPY_FROM_PARENT, wid, parent, static_cast<std::int16_t>(x), static_cast<std::int16_t>(y), static_cast<std::uint16_t>(width),
                          static_cast<std::uint16_t>(height), static_cast<std::uint16_t>(border_width), XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT,
                          XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL, values);
        return wid;
    }

    xcb_window_t create_window(xcb_window_t parent, int x, int y, unsigned width, unsigned height, unsigned border_width, std::uint8_t depth, std::uint16_t window_class,
                               xcb_visualid_t visual_id, std::uint32_t value_mask, const WindowAttrs& attrs) {
        xcb_connection_t* conn = connection();
        if (!conn)
            return XCB_WINDOW_NONE;

        std::uint32_t values[15]{};
        if (value_mask != 0U)
            static_cast<void>(pack_window_attributes_values(value_mask, attrs, values));

        const xcb_window_t wid = xcb_generate_id(conn);
        xcb_create_window(conn, depth, wid, parent, static_cast<std::int16_t>(x), static_cast<std::int16_t>(y), static_cast<std::uint16_t>(width),
                          static_cast<std::uint16_t>(height), static_cast<std::uint16_t>(border_width), window_class, visual_id, value_mask, values);
        return wid;
    }

    void define_cursor(xcb_window_t window, xcb_cursor_t cursor) {
        xcb_connection_t* conn = connection();
        if (!conn)
            return;
        const std::uint32_t values[] = {static_cast<std::uint32_t>(cursor)};
        xcb_change_window_attributes(conn, window, XCB_CW_CURSOR, values);
    }

    void map_raised(xcb_window_t window) {
        map_window(window);
        raise_window(window);
    }

    void set_input_focus(xcb_window_t window, std::uint8_t revert_to, xcb_timestamp_t time) {
        xcb_connection_t* conn = connection();
        if (!conn)
            return;
        xcb_set_input_focus(conn, revert_to, window, time);
    }

    void select_input(xcb_window_t window, std::uint32_t event_mask) {
        xcb_connection_t* conn = connection();
        if (!conn)
            return;
        const std::uint32_t values[] = {event_mask};
        xcb_change_window_attributes(conn, window, XCB_CW_EVENT_MASK, values);
    }

} // namespace wm::x11
