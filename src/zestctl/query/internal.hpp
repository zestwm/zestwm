#pragma once

#include <cstdint>

#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <xcb/xcb.h>

/* Internal helpers shared by `zestctl/query` implementation units. */
struct GroupClientInfo {
    uint32_t group_size;
    int      group_active_index;
    int      groupmode;
};

[[nodiscard]] std::unordered_map<uint32_t, GroupClientInfo> get_group_info_map(xcb_connection_t* c, xcb_window_t root);
[[nodiscard]] std::unordered_map<uint32_t, std::string>     get_special_client_tags_map(xcb_connection_t* c, xcb_window_t root);
[[nodiscard]] std::map<std::string, unsigned>               special_tag_client_counts(xcb_connection_t* c, xcb_window_t root);
[[nodiscard]] int32_t                                       special_workspace_row_id(const std::string& tag);
[[nodiscard]] std::unordered_map<std::string, bool>         special_overlay_visible_by_tag(xcb_connection_t* c, xcb_window_t root);
[[nodiscard]] std::unordered_map<std::string, uint32_t>     special_hidden_id_by_tag(xcb_connection_t* c, xcb_window_t root);
[[nodiscard]] std::string                                   get_layout_for_monitor(xcb_connection_t* c, xcb_window_t root, uint32_t mon_id);
[[nodiscard]] std::vector<std::pair<uint32_t, std::string>> get_layout_list(xcb_connection_t* c, xcb_window_t root);
