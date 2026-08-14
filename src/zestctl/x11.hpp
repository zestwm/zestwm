#pragma once

#include <cstdint>

#include <string>
#include <vector>

#include <xcb/xcb.h>

/* Low-level X11/XCB helpers shared by zestctl command/query paths. */
/* Flush pending requests on the given XCB connection. */
void flush_connection(xcb_connection_t* c);
/* Intern atom name on demand; returns `XCB_ATOM_NONE` on failure. */
xcb_atom_t intern_atom(xcb_connection_t* c, const char* name);

/* Replace root UTF-8 string property used by dispatch/query paths. */
int set_root_utf8_string(xcb_connection_t* c, xcb_window_t root, const char* prop_name, const std::string& s);
/* Replace root CARDINAL(32) property with a single value. */
int set_root_cardinal32(xcb_connection_t* c, xcb_window_t root, const char* prop_name, uint32_t value);
/* Delete a root property when bridge metadata is absent/stale. */
int delete_root_property(xcb_connection_t* c, xcb_window_t root, const char* prop_name);
/* Send `_NET_ZEST_DISPATCH` command payload to WM root properties. */
int send_dispatch(xcb_connection_t* c, xcb_window_t root, uint32_t cmd, uint32_t val);

/* Read one 32-bit CARDINAL property value from a window/root. */
int get_cardinal32(xcb_connection_t* c, xcb_window_t w, xcb_atom_t atom, uint32_t* out);
/* Read one 32-bit property value using `TYPE_ANY` compatibility mode. */
int get_property32_any(xcb_connection_t* c, xcb_window_t w, xcb_atom_t atom, uint32_t* out);

/* Read full text property payload (UTF-8/STRING) preserving embedded NUL bytes. */
std::string get_string_prop(xcb_connection_t* c, xcb_window_t w, xcb_atom_t atom, xcb_atom_t type);
/* Read WM_CLASS as instance/class tuple. */
void get_wm_class(xcb_connection_t* c, xcb_window_t w, std::string* instance, std::string* klass);
/* Check `_NET_WM_STATE_FULLSCREEN` membership. */
int is_fullscreen(xcb_connection_t* c, xcb_window_t w);
/* Read `_NET_CLIENT_LIST` windows from root. */
std::vector<xcb_window_t> get_client_list(xcb_connection_t* c, xcb_window_t root);
/* Read `_NET_ZEST_FLOATING_CLIENTS` windows from root (runtime `isfloating`). */
std::vector<xcb_window_t> get_floating_client_list(xcb_connection_t* c, xcb_window_t root);
/* Read full CARDINAL array export (`_NET_*` vectors). */
std::vector<uint32_t> get_cardinal_array(xcb_connection_t* c, xcb_window_t w, xcb_atom_t atom);
/* Read connected RandR output names in server-provided order. */
std::vector<std::string> get_connected_output_names(xcb_connection_t* c, xcb_window_t root);
