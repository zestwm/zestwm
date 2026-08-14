/* Connection-explicit XCB property helpers shared by WM and zestctl. */
#pragma once

#include <cstdint>
#include <string>

#include <xcb/xcb.h>

namespace wm::x11 {

    /* Flush pending requests on an open connection. */
    void flush_connection(xcb_connection_t* conn) noexcept;

    /* Intern atom name on demand; returns `XCB_ATOM_NONE` on failure. */
    [[nodiscard]] xcb_atom_t intern_atom(xcb_connection_t* conn, const char* name);

    /* Read one 32-bit CARDINAL property value; returns 1 on success. */
    [[nodiscard]] int get_cardinal32(xcb_connection_t* conn, xcb_window_t window, xcb_atom_t atom, uint32_t* out);

    /* Read one 32-bit property value with `XCB_GET_PROPERTY_TYPE_ANY`. */
    [[nodiscard]] int get_property32_any(xcb_connection_t* conn, xcb_window_t window, xcb_atom_t atom, uint32_t* out);

    /* Replace root UTF-8 string property; returns 0 on success. */
    [[nodiscard]] int set_root_utf8_string(xcb_connection_t* conn, xcb_window_t root, const char* prop_name, const std::string& value);

    /* Replace root CARDINAL(32) property with a single value; returns 0 on success. */
    [[nodiscard]] int set_root_cardinal32(xcb_connection_t* conn, xcb_window_t root, const char* prop_name, uint32_t value);

    /* Delete a root property; returns 0 on success. */
    [[nodiscard]] int delete_root_property(xcb_connection_t* conn, xcb_window_t root, const char* prop_name);

} // namespace wm::x11
