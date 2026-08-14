#include "zestctl/x11.hpp"

#include "x11/xcb_props.hpp"
#include "x11/randr_output_names.hpp"
#include "x11/reply_ptr.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include <string>
#include <vector>

#include <xcb/randr.h>

void flush_connection(xcb_connection_t* c) {
    wm::x11::flush_connection(c);
}

xcb_atom_t intern_atom(xcb_connection_t* c, const char* name) {
    return wm::x11::intern_atom(c, name);
}

int set_root_utf8_string(xcb_connection_t* c, xcb_window_t root, const char* prop_name, const std::string& s) {
    return wm::x11::set_root_utf8_string(c, root, prop_name, s);
}

int set_root_cardinal32(xcb_connection_t* c, xcb_window_t root, const char* prop_name, uint32_t value) {
    return wm::x11::set_root_cardinal32(c, root, prop_name, value);
}

int delete_root_property(xcb_connection_t* c, xcb_window_t root, const char* prop_name) {
    return wm::x11::delete_root_property(c, root, prop_name);
}

/* Emit `_NET_ZEST_DISPATCH` root payload consumed by zestwm. */
int send_dispatch(xcb_connection_t* c, xcb_window_t root, uint32_t cmd, uint32_t val) {
    xcb_atom_t msg_atom = intern_atom(c, "_NET_ZEST_DISPATCH");
    uint32_t   payload[3];

    if (msg_atom == XCB_ATOM_NONE) {
        fprintf(stderr, "zestctl: _NET_ZEST_DISPATCH not available\n");
        return 1;
    }
    payload[0]               = cmd;
    payload[1]               = val;
    payload[2]               = static_cast<uint32_t>(getpid());
    xcb_void_cookie_t cookie = xcb_change_property_checked(c, XCB_PROP_MODE_REPLACE, root, msg_atom, XCB_ATOM_CARDINAL, 32, 3, payload);
    auto              err    = make_xcb_reply_ptr(xcb_request_check(c, cookie));
    if (err) {
        fprintf(stderr, "zestctl: dispatch failed\n");
        return 1;
    }
    flush_connection(c);
    return 0;
}

int get_cardinal32(xcb_connection_t* c, xcb_window_t w, xcb_atom_t atom, uint32_t* out) {
    return wm::x11::get_cardinal32(c, w, atom, out);
}

int get_property32_any(xcb_connection_t* c, xcb_window_t w, xcb_atom_t atom, uint32_t* out) {
    return wm::x11::get_property32_any(c, w, atom, out);
}

/* Read full property bytes as string payload (UTF-8/STRING). */
std::string get_string_prop(xcb_connection_t* c, xcb_window_t w, xcb_atom_t atom, xcb_atom_t type) {
    xcb_get_property_cookie_t cookie = xcb_get_property(c, 0, w, atom, type, 0, 4096);
    auto                      reply  = make_xcb_reply_ptr(xcb_get_property_reply(c, cookie, nullptr));
    int                       len;
    const char*               ptr;
    std::string               out;

    if (!reply)
        return out;
    len = xcb_get_property_value_length(reply.get());
    if (len > 0) {
        ptr = static_cast<const char*>(xcb_get_property_value(reply.get()));
        out.assign(ptr, static_cast<size_t>(len));
    }
    return out;
}

/* Read WM_CLASS into instance/class strings. */
void get_wm_class(xcb_connection_t* c, xcb_window_t w, std::string* instance, std::string* klass) {
    xcb_get_property_cookie_t cookie = xcb_get_property(c, 0, w, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 0, 1024);
    auto                      reply  = make_xcb_reply_ptr(xcb_get_property_reply(c, cookie, nullptr));
    const char*               p;
    int                       len;

    instance->clear();
    klass->clear();
    if (!reply)
        return;
    len = xcb_get_property_value_length(reply.get());
    if (reply->format != 8 || len <= 0) {
        return;
    }
    p         = static_cast<const char*>(xcb_get_property_value(reply.get()));
    *instance = std::string(p, strnlen(p, static_cast<size_t>(len)));
    if (instance->size() + 1U < static_cast<size_t>(len)) {
        const char* p2 = p + instance->size() + 1U;
        *klass         = std::string(p2, strnlen(p2, static_cast<size_t>(len) - instance->size() - 1U));
    }
}

/* Check whether a window is currently fullscreen according to EWMH state list. */
int is_fullscreen(xcb_connection_t* c, xcb_window_t w) {
    xcb_atom_t                wmstate = intern_atom(c, "_NET_WM_STATE");
    xcb_atom_t                fs      = intern_atom(c, "_NET_WM_STATE_FULLSCREEN");
    xcb_get_property_cookie_t cookie;
    xcb_atom_t*               vals;
    int                       count, i;

    if (wmstate == XCB_ATOM_NONE || fs == XCB_ATOM_NONE)
        return 0;
    cookie     = xcb_get_property(c, 0, w, wmstate, XCB_ATOM_ATOM, 0, 64);
    auto reply = make_xcb_reply_ptr(xcb_get_property_reply(c, cookie, nullptr));
    if (!reply || reply->format != 32) {
        return 0;
    }
    count = xcb_get_property_value_length(reply.get()) / static_cast<int>(sizeof(xcb_atom_t));
    vals  = static_cast<xcb_atom_t*>(xcb_get_property_value(reply.get()));
    for (i = 0; i < count; i++) {
        if (vals[i] == fs)
            return 1;
    }
    return 0;
}

/* Read managed client windows from `_NET_CLIENT_LIST`. */
std::vector<xcb_window_t> get_client_list(xcb_connection_t* c, xcb_window_t root) {
    std::vector<xcb_window_t> out;
    xcb_atom_t                atom   = intern_atom(c, "_NET_CLIENT_LIST");
    xcb_get_property_cookie_t cookie = xcb_get_property(c, 0, root, atom, XCB_ATOM_WINDOW, 0, 4096);
    auto                      reply  = make_xcb_reply_ptr(xcb_get_property_reply(c, cookie, nullptr));
    int                       count, i;
    xcb_window_t*             data;

    if (!reply || atom == XCB_ATOM_NONE)
        return out;
    if (reply->format != 32)
        return out;
    count = xcb_get_property_value_length(reply.get()) / static_cast<int>(sizeof(xcb_window_t));
    data  = static_cast<xcb_window_t*>(xcb_get_property_value(reply.get()));
    for (i = 0; i < count; i++)
        out.push_back(data[i]);
    return out;
}

/* Read floating managed clients from `_NET_ZEST_FLOATING_CLIENTS`. */
std::vector<xcb_window_t> get_floating_client_list(xcb_connection_t* c, xcb_window_t root) {
    std::vector<xcb_window_t> out;
    xcb_atom_t                atom   = intern_atom(c, "_NET_ZEST_FLOATING_CLIENTS");
    xcb_get_property_cookie_t cookie = xcb_get_property(c, 0, root, atom, XCB_ATOM_WINDOW, 0, 4096);
    auto                      reply  = make_xcb_reply_ptr(xcb_get_property_reply(c, cookie, nullptr));
    if (!reply || atom == XCB_ATOM_NONE)
        return out;
    if (reply->format != 32)
        return out;
    const int     count = xcb_get_property_value_length(reply.get()) / static_cast<int>(sizeof(xcb_window_t));
    xcb_window_t* data  = static_cast<xcb_window_t*>(xcb_get_property_value(reply.get()));
    for (int i = 0; i < count; i++)
        out.push_back(data[i]);
    return out;
}

/* Read full CARDINAL(32) property vector into host array. */
std::vector<uint32_t> get_cardinal_array(xcb_connection_t* c, xcb_window_t w, xcb_atom_t atom) {
    std::vector<uint32_t>     out;
    xcb_get_property_cookie_t cookie = xcb_get_property(c, 0, w, atom, XCB_ATOM_CARDINAL, 0, 4096);
    auto                      reply  = make_xcb_reply_ptr(xcb_get_property_reply(c, cookie, nullptr));
    uint32_t*                 data;
    int                       n, i;

    if (!reply || atom == XCB_ATOM_NONE)
        return out;
    if (reply->format != 32)
        return out;
    n    = xcb_get_property_value_length(reply.get()) / static_cast<int>(sizeof(uint32_t));
    data = static_cast<uint32_t*>(xcb_get_property_value(reply.get()));
    for (i = 0; i < n; i++)
        out.push_back(data[i]);
    return out;
}

/* Read connected RandR output names ordered by screen resource output list. */
std::vector<std::string> get_connected_output_names(xcb_connection_t* c, xcb_window_t root) {
    return randr_connected_output_names(c, root);
}
