#include "zestctl/query.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include <string>
#include <utility>
#include <vector>

#include "zestctl/query/sections.hpp"
#include "zestctl/query/internal.hpp"
#include "zestctl/x11.hpp"

/* Poll `_NET_ACTIVE_WINDOW` until it matches the requested target. */
int wait_for_active_window(xcb_connection_t* c, xcb_window_t root, uint32_t target_window) {
    xcb_atom_t active_atom = intern_atom(c, "_NET_ACTIVE_WINDOW");
    for (int i = 0; i < 50; ++i) {
        uint32_t active = 0;
        if (get_property32_any(c, root, active_atom, &active) && active == target_window)
            return 1;
        usleep(10000);
    }
    return 0;
}

/* Resolve layout token as numeric index or layout symbol. */
int resolve_layout_index(xcb_connection_t* c, xcb_window_t root, const std::string& token, uint32_t* out) {
    char* end = nullptr;
    long  idx = strtol(token.c_str(), &end, 10);
    if (end && *end == '\0' && idx >= 0) {
        *out = static_cast<uint32_t>(idx);
        return 1;
    }
    /* Non-numeric token: match against the exported `id:symbol` layout list. */
    for (const auto& [id, sym] : get_layout_list(c, root)) {
        if (sym == token) {
            *out = id;
            return 1;
        }
    }
    return 0;
}

/* Expose layout list to command routing for next/prev layout dispatch. */
std::vector<std::pair<uint32_t, std::string>> get_layout_list_for_dispatch(xcb_connection_t* c, xcb_window_t root) {
    return get_layout_list(c, root);
}

/* Resolve active layout index from monitor state + exported layout symbols. */
int get_current_layout_index(xcb_connection_t* c, xcb_window_t root, uint32_t* out) {
    std::vector<uint32_t>                         state = get_cardinal_array(c, root, intern_atom(c, "_NET_ZESTWM_STATE"));
    std::vector<std::pair<uint32_t, std::string>> ll    = get_layout_list(c, root);
    const size_t                                  nmons = state.size() / 2U;
    std::string                                   cur;

    if (nmons == 0)
        return 0;
    cur = get_layout_for_monitor(c, root, state[0]);
    if (cur.empty())
        return 0;
    for (const auto& entry : ll) {
        if (entry.second == cur) {
            *out = entry.first;
            return 1;
        }
    }
    return 0;
}

/* Read current workspace index and number of desktops from EWMH root. */
int get_workspace_meta(xcb_connection_t* c, xcb_window_t root, uint32_t* cur, uint32_t* total) {
    uint32_t cws = 0, nd = 1;

    if (!get_cardinal32(c, root, intern_atom(c, "_NET_CURRENT_DESKTOP"), &cws))
        return 0;
    static_cast<void>(get_cardinal32(c, root, intern_atom(c, "_NET_NUMBER_OF_DESKTOPS"), &nd));
    if (nd == 0)
        nd = 1;
    if (cws >= nd)
        cws = 0;
    *cur   = cws;
    *total = nd;
    return 1;
}

/* Parse workspace token as id first, then by name in `_NET_DESKTOP_NAMES`. */
std::expected<WorkspaceId, std::string> parse_workspace_id_or_name_token(xcb_connection_t* c, xcb_window_t root, const std::string& token) {
    if (token.empty())
        return std::unexpected("workspace token is empty");

    bool all_digits = true;
    for (unsigned char ch : token) {
        if (ch < '0' || ch > '9') {
            all_digits = false;
            break;
        }
    }

    if (all_digits) {
        const auto parsed_id = parse_workspace_id(token);
        if (!parsed_id)
            return std::unexpected(parsed_id.error());
        return *parsed_id;
    }

    xcb_atom_t  utf8_atom  = intern_atom(c, "UTF8_STRING");
    xcb_atom_t  names_atom = intern_atom(c, "_NET_DESKTOP_NAMES");
    std::string names      = get_string_prop(c, root, names_atom, utf8_atom);
    if (names.empty())
        return std::unexpected("cannot read _NET_DESKTOP_NAMES");

    size_t      pos = 0;
    WorkspaceId id  = 1U;
    while (pos < names.size()) {
        const size_t end = names.find('\0', pos);
        if (end == std::string::npos) {
            const std::string name = names.substr(pos);
            if (name == token)
                return id;
            break;
        }
        const std::string name = names.substr(pos, end - pos);
        if (name == token)
            return id;
        pos = end + 1;
        ++id;
    }

    return std::unexpected("unknown workspace name");
}

/* Parse `special:<tag>` token used by workspace/special dispatchers. */
int parse_special_tag_from_workspace_token(const std::string& token, std::string* out_tag) {
    if (!out_tag)
        return 0;
    if (token.size() < 8)
        return 0;
    const std::string prefix = token.substr(0, 8);
    if (strcasecmp(prefix.c_str(), "special:") != 0)
        return 0;
    *out_tag = token.substr(8);
    return 1;
}

/* Publish special dispatch payload (canonical hidden-id metadata only). */
int set_special_dispatch_target(xcb_connection_t* c, xcb_window_t root, const std::string& tag) {
    const auto hidden_by_tag = special_hidden_id_by_tag(c, root);
    const auto it            = hidden_by_tag.find(tag);
    if (it == hidden_by_tag.end()) {
        delete_root_property(c, root, "_NET_ZESTWM_SPECIAL");
        delete_root_property(c, root, "_NET_ZESTWM_SPECIAL_HIDDEN_ID");
        return 1;
    }
    if (delete_root_property(c, root, "_NET_ZESTWM_SPECIAL") != 0)
        return 1;
    return set_root_cardinal32(c, root, "_NET_ZESTWM_SPECIAL_HIDDEN_ID", it->second);
}

/* Route read-only info commands to split handlers. */
int run_info(xcb_connection_t* c, xcb_window_t root, const std::vector<std::string>& t, int json) {
    return run_query_sections(c, root, t, json);
}
