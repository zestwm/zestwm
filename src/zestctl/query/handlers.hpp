#pragma once

#include <string>
#include <vector>

#include <xcb/xcb.h>

/* Domain handlers used by `run_query_sections` dispatcher. */
int run_query_version(xcb_connection_t* c, xcb_window_t root, int json);
int run_query_activeworkspace(xcb_connection_t* c, xcb_window_t root, int json);
int run_query_monitors(xcb_connection_t* c, xcb_window_t root, int json);
int run_query_layouts(xcb_connection_t* c, xcb_window_t root, const std::vector<std::string>& t, int json);
int run_query_workspaces(xcb_connection_t* c, xcb_window_t root, int json);
int run_query_clients(xcb_connection_t* c, xcb_window_t root, int json);
int run_query_activewindow(xcb_connection_t* c, xcb_window_t root, int json);
