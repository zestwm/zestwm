#include "zestctl/query/handlers.hpp"

#include <cstdio>

#include <set>
#include <string>
#include <vector>

#include "zestctl/helpers.hpp"
#include "zestctl/query/internal.hpp"
#include "zestctl/x11.hpp"

/* Render `zestctl workspaces` including synthetic `special:<tag>` rows. */
int run_query_workspaces(xcb_connection_t* c, xcb_window_t root, int json) {
    uint32_t    nd = 0, cur = 0;
    std::string names            = get_string_prop(c, root, intern_atom(c, "_NET_DESKTOP_NAMES"), intern_atom(c, "UTF8_STRING"));
    auto        clients          = get_client_list(c, root);
    xcb_atom_t  desk             = intern_atom(c, "_NET_WM_DESKTOP");
    size_t      pos              = 0;
    int         json_needs_comma = 0;

    if (!get_cardinal32(c, root, intern_atom(c, "_NET_NUMBER_OF_DESKTOPS"), &nd))
        nd = 1;
    static_cast<void>(get_cardinal32(c, root, intern_atom(c, "_NET_CURRENT_DESKTOP"), &cur));
    const auto            special_counts = special_tag_client_counts(c, root);
    const auto            special_by_win = get_special_client_tags_map(c, root);
    const auto            special_vis    = special_overlay_visible_by_tag(c, root);
    const auto            special_hidden = special_hidden_id_by_tag(c, root);
    std::vector<uint32_t> client_desktops(clients.size(), UINT32_MAX);
    for (size_t ci = 0; ci < clients.size(); ++ci) {
        if (special_by_win.find(clients[ci]) != special_by_win.end())
            continue;
        uint32_t d = 0;
        if (get_cardinal32(c, clients[ci], desk, &d))
            client_desktops[ci] = d;
    }
    if (json)
        printf("[");
    for (size_t i = 0; i < nd; i++) {
        std::string n         = std::to_string(i + 1U);
        unsigned    win_count = 0;
        if (pos < names.size()) {
            size_t end = names.find('\0', pos);
            if (end == std::string::npos)
                end = names.size();
            if (end > pos)
                n = names.substr(pos, end - pos);
            pos = end + 1;
        }
        for (size_t ci = 0; ci < clients.size(); ci++) {
            if (client_desktops[ci] == static_cast<uint32_t>(i))
                win_count++;
        }
        if (json) {
            printf("%s{\"id\":%u,\"name\":\"%s\",\"active\":%s,\"windows\":%u}", json_needs_comma ? "," : "", static_cast<unsigned>(i + 1U), json_escape(n).c_str(),
                   (i == cur ? "true" : "false"), win_count);
            json_needs_comma = 1;
        } else {
            printf("%c id:%u name:%s windows:%u\n", (i == cur ? '*' : '-'), static_cast<unsigned>(i + 1U), n.c_str(), win_count);
        }
    }
    std::set<std::string> special_tags_ordered;
    for (const auto& sc : special_counts)
        special_tags_ordered.insert(sc.first);
    for (const auto& kv : special_vis) {
        if (kv.second)
            special_tags_ordered.insert(kv.first);
    }
    for (const auto& tag : special_tags_ordered) {
        const auto        cnt_it    = special_counts.find(tag);
        const unsigned    win_count = (cnt_it == special_counts.end()) ? 0U : cnt_it->second;
        const std::string disp      = std::string("special:") + tag;
        const int32_t     sid       = special_workspace_row_id(tag);
        const auto        vit       = special_vis.find(tag);
        const bool        vis       = vit != special_vis.end() && vit->second;
        const auto        hid_it    = special_hidden.find(tag);
        const int         has_hid   = hid_it != special_hidden.end();
        if (json) {
            printf("%s{\"id\":%d,\"name\":\"%s\",\"active\":false,\"windows\":%u,\"visible\":%s,\"hidden_id\":", json_needs_comma ? "," : "", static_cast<int>(sid),
                   json_escape(disp).c_str(), win_count, vis ? "true" : "false");
            if (has_hid)
                printf("%u", hid_it->second);
            else
                printf("null");
            printf("}");
            json_needs_comma = 1;
        } else {
            if (has_hid) {
                if (vis)
                    printf("- id:%d name:%s windows:%u hidden_id:%u visible\n", static_cast<int>(sid), disp.c_str(), win_count, hid_it->second);
                else
                    printf("- id:%d name:%s windows:%u hidden_id:%u\n", static_cast<int>(sid), disp.c_str(), win_count, hid_it->second);
            } else {
                if (vis)
                    printf("- id:%d name:%s windows:%u hidden_id:- visible\n", static_cast<int>(sid), disp.c_str(), win_count);
                else
                    printf("- id:%d name:%s windows:%u hidden_id:-\n", static_cast<int>(sid), disp.c_str(), win_count);
            }
        }
    }
    if (json)
        printf("]\n");
    return 0;
}
