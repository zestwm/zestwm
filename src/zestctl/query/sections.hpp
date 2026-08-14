#pragma once

#include <string>
#include <vector>

#include <xcb/xcb.h>

/* Split-out query dispatcher implementation kept in `query/sections.cpp`. */
[[nodiscard]] int run_query_sections(xcb_connection_t* c, xcb_window_t root, const std::vector<std::string>& t, int json);
