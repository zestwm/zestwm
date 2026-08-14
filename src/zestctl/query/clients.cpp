#include "zestctl/query/handlers.hpp"

#include <cstdio>

#include <string>
#include <unordered_set>

#include "zestctl/helpers.hpp"
#include "zestctl/query/internal.hpp"
#include "zestctl/x11.hpp"

/* Render `zestctl clients` with grouped/special/focus/floating metadata. */
int run_query_clients(xcb_connection_t* c, xcb_window_t root, int json) {
    auto                         clients      = get_client_list(c, root);
    xcb_atom_t                   desk         = intern_atom(c, "_NET_WM_DESKTOP");
    xcb_atom_t                   wmname       = intern_atom(c, "_NET_WM_NAME");
    xcb_atom_t                   utf8         = intern_atom(c, "UTF8_STRING");
    xcb_atom_t                   active_atom  = intern_atom(c, "_NET_ACTIVE_WINDOW");
    const auto                   group_info   = get_group_info_map(c, root);
    const auto                   special_tags = get_special_client_tags_map(c, root);
    const auto                   floating_vec = get_floating_client_list(c, root);
    std::unordered_set<uint32_t> floating_wins;
    floating_wins.reserve(floating_vec.size());
    for (const xcb_window_t w : floating_vec)
        floating_wins.insert(static_cast<uint32_t>(w));
    uint32_t awin = 0;

    static_cast<void>(get_property32_any(c, root, active_atom, &awin));
    if (json)
        printf("[");
    for (size_t i = 0; i < clients.size(); i++) {
        uint32_t       d          = 0;
        const uint32_t win_u      = static_cast<uint32_t>(clients[i]);
        const auto     sp_it      = special_tags.find(win_u);
        const int      on_special = (sp_it != special_tags.end()) ? 1 : 0;
        const char*    sp_plain   = "-";
        std::string    sp_tag_s;
        if (on_special) {
            sp_tag_s = sp_it->second;
            sp_plain = sp_tag_s.empty() ? "(empty)" : sp_tag_s.c_str();
        }
        std::string title = get_string_prop(c, clients[i], wmname, utf8);
        std::string inst, klass;
        int         fs;
        int         grouped            = 0;
        uint32_t    group_size         = 0;
        int         group_active_index = -1;
        int         groupmode          = 0;
        const int   floating           = (floating_wins.count(win_u) != 0U) ? 1 : 0;

        static_cast<void>(get_cardinal32(c, clients[i], desk, &d));
        get_wm_class(c, clients[i], &inst, &klass);
        fs = is_fullscreen(c, clients[i]);
        {
            const auto it = group_info.find(static_cast<uint32_t>(clients[i]));
            if (it != group_info.end()) {
                group_size         = it->second.group_size;
                group_active_index = it->second.group_active_index;
                groupmode          = it->second.groupmode;
                /* A solo leaf with groupmode enabled is still an active group intent. */
                grouped = ((group_size > 1U) || groupmode) ? 1 : 0;
            }
        }
        if (json) {
            printf("%s{\"window\":\"0x%08x\",\"workspace\":%u,\"special_tag\":", (i == 0 ? "" : ","), static_cast<unsigned int>(win_u), d + 1U);
            if (on_special)
                printf("\"%s\"", json_escape(sp_tag_s).c_str());
            else
                printf("null");
            printf(",\"title\":\"%s\",\"class\":\"%s\",\"instance\":\"%s\","
                   "\"fullscreen\":%s,\"focused\":%s,\"floating\":%s,\"grouped\":%s,\"group_size\":%u,\"group_active_index\":",
                   json_escape(title).c_str(), json_escape(klass).c_str(), json_escape(inst).c_str(), (fs ? "true" : "false"), (win_u == awin ? "true" : "false"),
                   (floating ? "true" : "false"), (grouped ? "true" : "false"), group_size);
            if (grouped && group_active_index >= 0)
                printf("%d", group_active_index);
            else
                printf("null");
            printf(",\"groupmode\":%s}", (groupmode ? "true" : "false"));
        } else {
            printf("win:0x%08x class:%s instance:%s ws:%u special_tag:%s focused:%s fullscreen:%s floating:%s grouped:%s group_size:%u "
                   "group_active_index:%d groupmode:%s title:%s\n",
                   static_cast<unsigned int>(win_u), klass.empty() ? "-" : klass.c_str(), inst.empty() ? "-" : inst.c_str(), d + 1U, sp_plain, (win_u == awin ? "yes" : "no"),
                   (fs ? "yes" : "no"), (floating ? "yes" : "no"), (grouped ? "yes" : "no"), group_size, group_active_index, (groupmode ? "yes" : "no"),
                   title.empty() ? "-" : title.c_str());
        }
    }
    if (json)
        printf("]\n");
    return 0;
}
