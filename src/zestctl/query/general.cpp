#include "zestctl/query/handlers.hpp"

#include <cstdio>

#include <string>
#include <vector>

#include "zestctl/helpers.hpp"
#include "zestctl/query/internal.hpp"
#include "zestctl/x11.hpp"

/* Render `zestctl version` output from WM name metadata. */
int run_query_version(xcb_connection_t* c, xcb_window_t root, int json) {
    std::string wm_name = get_string_prop(c, root, intern_atom(c, "_NET_WM_NAME"), intern_atom(c, "UTF8_STRING"));
    if (wm_name.empty()) {
        xcb_window_t check = XCB_WINDOW_NONE;
        if (get_property32_any(c, root, intern_atom(c, "_NET_SUPPORTING_WM_CHECK"), reinterpret_cast<uint32_t*>(&check)))
            wm_name = get_string_prop(c, check, intern_atom(c, "_NET_WM_NAME"), intern_atom(c, "UTF8_STRING"));
    }
    if (wm_name.empty())
        wm_name = "zestwm";
    if (json)
        printf("{\"wm\":\"%s\"}\n", json_escape(wm_name).c_str());
    else
        printf("%s\n", wm_name.c_str());
    return 0;
}

/* Render `zestctl activeworkspace` from EWMH desktop exports. */
int run_query_activeworkspace(xcb_connection_t* c, xcb_window_t root, int json) {
    uint32_t cur = 0;
    if (!get_cardinal32(c, root, intern_atom(c, "_NET_CURRENT_DESKTOP"), &cur)) {
        fprintf(stderr, "zestctl: cannot read _NET_CURRENT_DESKTOP\n");
        return 1;
    }
    std::string active_name = std::to_string(cur + 1U);
    std::string names       = get_string_prop(c, root, intern_atom(c, "_NET_DESKTOP_NAMES"), intern_atom(c, "UTF8_STRING"));
    if (!names.empty()) {
        size_t pos = 0;
        for (uint32_t i = 0; i <= cur; ++i) {
            const size_t end = names.find('\0', pos);
            if (end == std::string::npos) {
                if (i == cur && pos < names.size())
                    active_name = names.substr(pos);
                break;
            }
            if (i == cur)
                active_name = names.substr(pos, end - pos);
            pos = end + 1;
        }
    }
    if (json)
        printf("{\"id\":%u,\"name\":\"%s\"}\n", cur + 1U, json_escape(active_name).c_str());
    else
        printf("%u\n", cur + 1U);
    return 0;
}

/* Render `zestctl monitors` with per-output layout state. */
int run_query_monitors(xcb_connection_t* c, xcb_window_t root, int json) {
    std::vector<uint32_t>    state        = get_cardinal_array(c, root, intern_atom(c, "_NET_ZESTWM_STATE"));
    std::vector<std::string> output_names = get_connected_output_names(c, root);
    const size_t             nmons        = state.size() / 2U;
    if (json)
        printf("[");
    for (size_t i = 0; i < nmons; i++) {
        uint32_t    num    = state[i * 2U + 0U];
        std::string layout = get_layout_for_monitor(c, root, num);
        std::string output_name;

        if (num < output_names.size())
            output_name = output_names[num];
        if (json) {
            printf("%s{\"id\":%u,\"output\":\"%s\",\"layout\":\"%s\"}", (i == 0 ? "" : ","), num, json_escape(output_name).c_str(), json_escape(layout).c_str());
        } else {
            printf("id:%u output:%s layout:%s\n", num, output_name.empty() ? "-" : output_name.c_str(), layout.empty() ? "-" : layout.c_str());
        }
    }
    if (json)
        printf("]\n");
    return 0;
}

/* Render `zestctl g_config.layouts` in current-monitor mode or full list mode. */
int run_query_layouts(xcb_connection_t* c, xcb_window_t root, const std::vector<std::string>& t, int json) {
    const int list_all = (t.size() >= 2 && t[1] == "--all");
    if (list_all) {
        const auto ll = get_layout_list(c, root);
        if (json)
            printf("[");
        for (size_t i = 0; i < ll.size(); i++) {
            if (json)
                printf("%s{\"index\":%u,\"symbol\":\"%s\"}", (i == 0 ? "" : ","), ll[i].first, json_escape(ll[i].second).c_str());
            else
                printf("index:%u symbol:%s\n", ll[i].first, ll[i].second.empty() ? "-" : ll[i].second.c_str());
        }
        if (json)
            printf("]\n");
        return 0;
    }

    const std::vector<uint32_t> state = get_cardinal_array(c, root, intern_atom(c, "_NET_ZESTWM_STATE"));
    const size_t                nmons = state.size() / 2U;
    if (json)
        printf("[");
    for (size_t i = 0; i < nmons; i++) {
        uint32_t    num    = state[i * 2U + 0U];
        std::string layout = get_layout_for_monitor(c, root, num);
        if (json)
            printf("%s{\"id\":%u,\"layout\":\"%s\"}", (i == 0 ? "" : ","), num, json_escape(layout).c_str());
        else
            printf("id:%u layout:%s\n", num, layout.empty() ? "-" : layout.c_str());
    }
    if (json)
        printf("]\n");
    return 0;
}

/* Render `zestctl activewindow` current focused client metadata. */
int run_query_activewindow(xcb_connection_t* c, xcb_window_t root, int json) {
    uint32_t    awin   = 0;
    xcb_atom_t  act    = intern_atom(c, "_NET_ACTIVE_WINDOW");
    xcb_atom_t  desk   = intern_atom(c, "_NET_WM_DESKTOP");
    xcb_atom_t  wmname = intern_atom(c, "_NET_WM_NAME");
    xcb_atom_t  utf8   = intern_atom(c, "UTF8_STRING");
    std::string title;
    uint32_t    d = 0;

    if (!get_property32_any(c, root, act, &awin) || awin == 0) {
        if (json)
            printf("{\"window\":null}\n");
        else
            printf("none\n");
        return 0;
    }
    title = get_string_prop(c, static_cast<xcb_window_t>(awin), wmname, utf8);
    static_cast<void>(get_cardinal32(c, static_cast<xcb_window_t>(awin), desk, &d));
    std::string inst, klass;
    get_wm_class(c, static_cast<xcb_window_t>(awin), &inst, &klass);
    if (json)
        printf("{\"window\":\"0x%08x\",\"workspace\":%u,\"title\":\"%s\",\"class\":\"%s\",\"instance\":\"%s\"}\n", awin, d + 1U, json_escape(title).c_str(),
               json_escape(klass).c_str(), json_escape(inst).c_str());
    else {
        const int fs = is_fullscreen(c, static_cast<xcb_window_t>(awin));
        printf("win:0x%08x class:%s instance:%s ws:%u focused:yes fullscreen:%s title:%s\n", static_cast<unsigned int>(awin), klass.empty() ? "-" : klass.c_str(),
               inst.empty() ? "-" : inst.c_str(), d + 1U, (fs ? "yes" : "no"), title.empty() ? "-" : title.c_str());
    }
    return 0;
}
