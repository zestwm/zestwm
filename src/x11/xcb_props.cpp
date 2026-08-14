/* Connection-explicit XCB property helpers shared by WM and zestctl. */
#include "x11/xcb_props.hpp"

#include "x11/reply_ptr.hpp"

#include <cstring>
#include <string>
#include <unordered_map>

namespace wm::x11 {
    namespace {
        /* Per-connection atom cache to avoid repeated intern roundtrips. */
        std::unordered_map<xcb_connection_t*, std::unordered_map<std::string, xcb_atom_t>> g_atom_cache_by_conn;
    } // namespace

    void flush_connection(xcb_connection_t* conn) noexcept {
        if (conn)
            xcb_flush(conn);
    }

    xcb_atom_t intern_atom(xcb_connection_t* conn, const char* name) {
        if (!conn || !name || !*name)
            return XCB_ATOM_NONE;
        auto& cache = g_atom_cache_by_conn[conn];
        if (const auto it = cache.find(name); it != cache.end())
            return it->second;

        const xcb_intern_atom_cookie_t cookie = xcb_intern_atom(conn, 0, static_cast<uint16_t>(std::strlen(name)), name);
        auto                           reply  = make_xcb_reply_ptr(xcb_intern_atom_reply(conn, cookie, nullptr));
        if (!reply)
            return XCB_ATOM_NONE;
        const xcb_atom_t atom    = reply->atom;
        cache[std::string(name)] = atom;
        return atom;
    }

    int get_cardinal32(xcb_connection_t* conn, xcb_window_t window, xcb_atom_t atom, uint32_t* out) {
        if (!conn || !out)
            return 0;
        const xcb_get_property_cookie_t cookie = xcb_get_property(conn, 0, window, atom, XCB_ATOM_CARDINAL, 0, 1);
        auto                            reply  = make_xcb_reply_ptr(xcb_get_property_reply(conn, cookie, nullptr));
        if (!reply)
            return 0;
        if (reply->format != 32 || xcb_get_property_value_length(reply.get()) < static_cast<int>(sizeof(uint32_t)))
            return 0;
        const auto* data = static_cast<const uint32_t*>(xcb_get_property_value(reply.get()));
        *out             = data[0];
        return 1;
    }

    int get_property32_any(xcb_connection_t* conn, xcb_window_t window, xcb_atom_t atom, uint32_t* out) {
        if (!conn || !out)
            return 0;
        const xcb_get_property_cookie_t cookie = xcb_get_property(conn, 0, window, atom, XCB_GET_PROPERTY_TYPE_ANY, 0, 1);
        auto                            reply  = make_xcb_reply_ptr(xcb_get_property_reply(conn, cookie, nullptr));
        if (!reply)
            return 0;
        if (reply->format != 32 || xcb_get_property_value_length(reply.get()) < static_cast<int>(sizeof(uint32_t)))
            return 0;
        const auto* data = static_cast<const uint32_t*>(xcb_get_property_value(reply.get()));
        *out             = data[0];
        return 1;
    }

    int set_root_utf8_string(xcb_connection_t* conn, xcb_window_t root, const char* prop_name, const std::string& value) {
        const xcb_atom_t prop = intern_atom(conn, prop_name);
        const xcb_atom_t utf8 = intern_atom(conn, "UTF8_STRING");
        if (!conn || prop == XCB_ATOM_NONE || utf8 == XCB_ATOM_NONE)
            return 1;
        const xcb_void_cookie_t ck  = xcb_change_property_checked(conn, XCB_PROP_MODE_REPLACE, root, prop, utf8, 8, static_cast<uint32_t>(value.size()), value.data());
        auto                    err = make_xcb_reply_ptr(xcb_request_check(conn, ck));
        if (err)
            return 1;
        flush_connection(conn);
        return 0;
    }

    int set_root_cardinal32(xcb_connection_t* conn, xcb_window_t root, const char* prop_name, uint32_t value) {
        const xcb_atom_t prop = intern_atom(conn, prop_name);
        if (!conn || prop == XCB_ATOM_NONE)
            return 1;
        const xcb_void_cookie_t ck  = xcb_change_property_checked(conn, XCB_PROP_MODE_REPLACE, root, prop, XCB_ATOM_CARDINAL, 32, 1, &value);
        auto                    err = make_xcb_reply_ptr(xcb_request_check(conn, ck));
        if (err)
            return 1;
        flush_connection(conn);
        return 0;
    }

    int delete_root_property(xcb_connection_t* conn, xcb_window_t root, const char* prop_name) {
        const xcb_atom_t prop = intern_atom(conn, prop_name);
        if (!conn || prop == XCB_ATOM_NONE)
            return 1;
        const xcb_void_cookie_t ck  = xcb_delete_property_checked(conn, root, prop);
        auto                    err = make_xcb_reply_ptr(xcb_request_check(conn, ck));
        if (err)
            return 1;
        flush_connection(conn);
        return 0;
    }

} // namespace wm::x11
