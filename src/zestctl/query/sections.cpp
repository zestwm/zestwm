#include "zestctl/query/sections.hpp"

#include "zestctl/query/handlers.hpp"

int run_query_sections(xcb_connection_t* c, xcb_window_t root, const std::vector<std::string>& t, int json) {
    if (t.empty())
        return 2;
    if (t[0] == "version")
        return run_query_version(c, root, json);
    if (t[0] == "activeworkspace")
        return run_query_activeworkspace(c, root, json);
    if (t[0] == "monitors")
        return run_query_monitors(c, root, json);
    if (t[0] == "layouts")
        return run_query_layouts(c, root, t, json);
    if (t[0] == "workspaces")
        return run_query_workspaces(c, root, json);
    if (t[0] == "clients")
        return run_query_clients(c, root, json);
    if (t[0] == "activewindow")
        return run_query_activewindow(c, root, json);
    return 2;
}
