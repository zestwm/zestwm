#pragma once

#include <cstdint>

#include <expected>
#include <string>
#include <vector>

#include <xcb/xcb.h>

#include "workspace_id.hpp"

/* Query/info handlers and shared parsing helpers used by zestctl routing. */

/* Run read-only `zestctl` info commands (`version`, `workspaces`, `clients`, ...). */
int run_info(xcb_connection_t* c, xcb_window_t root, const std::vector<std::string>& t, int json);

/* Resolve layout token (`index` or layout symbol from `_NET_ZEST_LAYOUT_LIST`). */
int resolve_layout_index(xcb_connection_t* c, xcb_window_t root, const std::string& token, uint32_t* out);
/* Read layout list export used by `dispatch layout next|prev`. */
std::vector<std::pair<uint32_t, std::string>> get_layout_list_for_dispatch(xcb_connection_t* c, xcb_window_t root);
/* Resolve currently active layout index from monitor state + layout export. */
int get_current_layout_index(xcb_connection_t* c, xcb_window_t root, uint32_t* out);
/* Read current workspace index and total workspace count from EWMH exports. */
int get_workspace_meta(xcb_connection_t* c, xcb_window_t root, uint32_t* cur, uint32_t* total);

/* Parse workspace token as numeric id or desktop name from `_NET_DESKTOP_NAMES`. */
std::expected<WorkspaceId, std::string> parse_workspace_id_or_name_token(xcb_connection_t* c, xcb_window_t root, const std::string& token);
/* Parse `special:<tag>` dispatcher token into raw tag payload. */
int parse_special_tag_from_workspace_token(const std::string& token, std::string* out_tag);
/* Populate special dispatch root payloads (`tag` + optional hidden-id bridge). */
int set_special_dispatch_target(xcb_connection_t* c, xcb_window_t root, const std::string& tag);
/* Best-effort wait until `_NET_ACTIVE_WINDOW` converges to target window id. */
int wait_for_active_window(xcb_connection_t* c, xcb_window_t root, uint32_t target_window);
