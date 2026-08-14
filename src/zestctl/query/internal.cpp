#include "zestctl/query/internal.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>

#include <functional>
#include <unordered_set>

#include "zestctl/x11.hpp"
#include "util.hpp"
#include "x11/reply_ptr.hpp"

namespace {

    /* Collect grouped/split tree window ids from a tree-state payload (excludes `|F(...)` geometry). */
    void collect_tree_window_ids_from_payload(std::string_view payload, std::unordered_set<uint32_t>& out) {
        const size_t fpos = payload.find("|F(");
        if (fpos != std::string_view::npos)
            payload = payload.substr(0, fpos);
        for_each_window_id(payload, [&](unsigned long win) {
            if (win <= 0xffffffffUL)
                out.insert(static_cast<uint32_t>(win));
        });
    }

    /* Collect floating window ids from a tree-state `|F(win:x:y:w:h,...)` suffix. */
    void collect_float_window_ids_from_suffix(std::string_view suffix, std::unordered_set<uint32_t>& out) {
        const size_t open  = suffix.find('(');
        const size_t close = (open == std::string_view::npos) ? std::string_view::npos : suffix.find(')', open + 1U);
        if (open == std::string_view::npos || close == std::string_view::npos || close <= open + 1U)
            return;
        const std::string_view body = suffix.substr(open + 1U, close - open - 1U);
        size_t                 p    = 0U;
        while (p < body.size()) {
            const size_t colon = body.find(':', p);
            if (colon == std::string_view::npos || colon <= p)
                break;
            const unsigned long win = strtoul(std::string(body.substr(p, colon - p)).c_str(), nullptr, 10);
            if (win != 0UL && win <= 0xffffffffUL)
                out.insert(static_cast<uint32_t>(win));
            const size_t comma = body.find(',', colon);
            if (comma == std::string_view::npos)
                break;
            p = comma + 1U;
        }
    }

    void collect_tree_state_window_ids_for_ws_key(const std::string& tree_state, char ws_prefix, std::unordered_set<uint32_t>& out) {
        size_t pos = 0U;
        while (pos < tree_state.size()) {
            const size_t      end     = tree_state.find('\0', pos);
            const size_t      rec_end = (end == std::string::npos) ? tree_state.size() : end;
            const std::string entry   = tree_state.substr(pos, rec_end - pos);
            pos                       = (end == std::string::npos) ? tree_state.size() : (end + 1U);
            if (entry.empty())
                continue;
            const size_t p0 = entry.find(':');
            const size_t p1 = (p0 == std::string::npos) ? std::string::npos : entry.find(':', p0 + 1U);
            if (p0 == std::string::npos || p1 == std::string::npos || p1 + 1U >= entry.size())
                continue;
            const std::string ws_key = entry.substr(p0 + 1U, p1 - p0 - 1U);
            if (ws_key.empty() || ws_key[0] != ws_prefix)
                continue;
            const std::string_view payload = std::string_view(entry).substr(p1 + 1U);
            collect_tree_window_ids_from_payload(payload, out);
            const size_t fpos = payload.find("|F(");
            if (fpos != std::string_view::npos)
                collect_float_window_ids_from_suffix(payload.substr(fpos), out);
        }
    }

} // namespace

/* Parse `_NET_ZEST_TREE_STATE` grouped-client (`G(...)`) nodes into per-window group metadata.
 * Tree-state entries are `<mon>:<ws_key>:<tree_node>` (NUL-separated); a tree_node may nest
 * `S(axis:ratio:first:second)` splits containing one or more `G(groupmode:activewin:win,win,...)`
 * leaves. The optional `|F(...)` floating suffix is stripped before scanning so its `:`-delimited
 * geometry values are not mis-read as window ids. */
std::unordered_map<uint32_t, GroupClientInfo> get_group_info_map(xcb_connection_t* c, xcb_window_t root) {
    std::unordered_map<uint32_t, GroupClientInfo> out;
    const std::string                             tree_state = get_string_prop(c, root, intern_atom(c, "_NET_ZEST_TREE_STATE"), intern_atom(c, "UTF8_STRING"));
    size_t                                        pos        = 0U;

    while (pos < tree_state.size()) {
        const size_t      end     = tree_state.find('\0', pos);
        const size_t      rec_end = (end == std::string::npos) ? tree_state.size() : end;
        const std::string entry   = tree_state.substr(pos, rec_end - pos);
        pos                       = (end == std::string::npos) ? tree_state.size() : (end + 1U);
        if (entry.empty())
            continue;
        /* Entry layout: `<mon>:<ws_key>:<payload>`. Skip the first two `:` fields to reach the
         * tree payload (which itself uses `:` as a field delimiter inside G/S nodes). */
        const size_t p0 = entry.find(':');
        const size_t p1 = (p0 == std::string::npos) ? std::string::npos : entry.find(':', p0 + 1U);
        if (p0 == std::string::npos || p1 == std::string::npos)
            continue;
        std::string payload = entry.substr(p1 + 1U);
        /* Drop the `|F(...)` floating suffix: floating window ids are not grouped leaves. */
        const size_t fpos = payload.find("|F(");
        if (fpos != std::string::npos)
            payload.resize(fpos);

        /* Scan every `G(groupmode:activewin:win,win,...)` leaf in the (possibly nested) payload. */
        size_t scan = 0U;
        while (scan < payload.size()) {
            const size_t gpos = payload.find("G(", scan);
            if (gpos == std::string::npos)
                break;
            const size_t open  = gpos + 2U;
            const size_t close = payload.find(')', open);
            if (close == std::string::npos)
                break;
            const std::string body = payload.substr(open, close - open);
            scan                   = close + 1U;

            /* body = `groupmode:activewin:win,win,...` */
            const size_t c0 = body.find(':');
            const size_t c1 = (c0 == std::string::npos) ? std::string::npos : body.find(':', c0 + 1U);
            if (c0 == std::string::npos || c1 == std::string::npos)
                continue;
            const int             groupmode = (strtoul(body.substr(0U, c0).c_str(), nullptr, 10) != 0UL) ? 1 : 0;
            const unsigned long   activewin = strtoul(body.substr(c0 + 1U, c1 - c0 - 1U).c_str(), nullptr, 10);
            const std::string     members   = body.substr(c1 + 1U);

            std::vector<uint32_t> member_windows;
            size_t                start = 0U;
            while (start <= members.size()) {
                const size_t comma = members.find(',', start);
                const size_t stop  = (comma == std::string::npos) ? members.size() : comma;
                const auto   tok   = members.substr(start, stop - start);
                if (!tok.empty()) {
                    const unsigned long win = strtoul(tok.c_str(), nullptr, 10);
                    if (win != 0UL && win <= 0xffffffffUL)
                        member_windows.push_back(static_cast<uint32_t>(win));
                }
                if (comma == std::string::npos)
                    break;
                start = comma + 1U;
            }

            const uint32_t group_size = static_cast<uint32_t>(member_windows.size());
            if (group_size == 0U)
                continue;
            int active_idx = -1;
            for (size_t i = 0U; i < member_windows.size(); ++i) {
                if (member_windows[i] == static_cast<uint32_t>(activewin)) {
                    active_idx = static_cast<int>(i);
                    break;
                }
            }
            for (const uint32_t win : member_windows)
                out[win] = GroupClientInfo{group_size, active_idx, groupmode};
        }
    }

    return out;
}

/* Parse special workspace ownership from `_NET_ZEST_TREE_STATE` (`window -> special tag`). */
std::unordered_map<uint32_t, std::string> get_special_client_tags_map(xcb_connection_t* c, xcb_window_t root) {
    std::unordered_map<uint32_t, std::string> out;
    const auto                                hidden_by_tag = special_hidden_id_by_tag(c, root);
    std::unordered_map<uint32_t, std::string> tag_by_hidden;
    for (const auto& kv : hidden_by_tag)
        tag_by_hidden[kv.second] = kv.first;
    const std::string            tree_state = get_string_prop(c, root, intern_atom(c, "_NET_ZEST_TREE_STATE"), intern_atom(c, "UTF8_STRING"));
    std::unordered_set<uint32_t> normal_tree_windows;
    collect_tree_state_window_ids_for_ws_key(tree_state, 'w', normal_tree_windows);
    size_t pos = 0U;
    while (pos < tree_state.size()) {
        const size_t      end     = tree_state.find('\0', pos);
        const size_t      rec_end = (end == std::string::npos) ? tree_state.size() : end;
        const std::string entry   = tree_state.substr(pos, rec_end - pos);
        pos                       = (end == std::string::npos) ? tree_state.size() : (end + 1U);
        if (entry.empty())
            continue;
        const size_t p0 = entry.find(':');
        const size_t p1 = (p0 == std::string::npos) ? std::string::npos : entry.find(':', p0 + 1U);
        if (p0 == std::string::npos || p1 == std::string::npos || p1 + 1U >= entry.size())
            continue;
        const std::string ws_key = entry.substr(p0 + 1U, p1 - p0 - 1U);
        std::string       special_tag;
        if (!ws_key.empty() && ws_key[0] == 's') {
            const unsigned long hidden_id = strtoul(ws_key.substr(1U).c_str(), nullptr, 10);
            if (hidden_id == 0UL || hidden_id > 0xffffffffUL)
                continue;
            const auto hit = tag_by_hidden.find(static_cast<uint32_t>(hidden_id));
            if (hit == tag_by_hidden.end())
                continue;
            special_tag = hit->second;
        } else {
            continue;
        }
        const std::string_view       payload = std::string_view(entry).substr(p1 + 1U);
        std::unordered_set<uint32_t> special_wins;
        collect_tree_window_ids_from_payload(payload, special_wins);
        const size_t fpos = payload.find("|F(");
        if (fpos != std::string_view::npos)
            collect_float_window_ids_from_suffix(payload.substr(fpos), special_wins);
        for (const uint32_t win : special_wins) {
            if (normal_tree_windows.count(win) != 0U)
                continue;
            out[win] = special_tag;
        }
    }

    return out;
}

/* Aggregate special window counts by tag for `workspaces` output. */
std::map<std::string, unsigned> special_tag_client_counts(xcb_connection_t* c, xcb_window_t root) {
    std::map<std::string, unsigned>                 counts;
    const std::unordered_map<uint32_t, std::string> by_win = get_special_client_tags_map(c, root);
    for (const auto& kv : by_win)
        counts[kv.second]++;
    return counts;
}

/* Stable negative row id for synthetic `special:<tag>` workspace rows. */
int32_t special_workspace_row_id(const std::string& tag) {
    const std::size_t h = std::hash<std::string>{}(tag);
    uint32_t          u = static_cast<uint32_t>(h & 0x7fffffffu);
    if (u == 0U)
        u = 1U;
    return -static_cast<int32_t>(u);
}

/* Parse `_NET_ZESTWM_SPECIAL_OVERLAY` (`tag -> visible`). */
std::unordered_map<std::string, bool> special_overlay_visible_by_tag(xcb_connection_t* c, xcb_window_t root) {
    std::unordered_map<std::string, bool> out;
    xcb_atom_t                            prop = intern_atom(c, "_NET_ZESTWM_SPECIAL_OVERLAY");
    xcb_atom_t                            utf8 = intern_atom(c, "UTF8_STRING");
    if (prop == XCB_NONE || utf8 == XCB_NONE)
        return out;
    xcb_get_property_cookie_t cookie = xcb_get_property(c, 0, root, prop, utf8, 0, 1U << 20);
    auto                      reply  = make_xcb_reply_ptr(xcb_get_property_reply(c, cookie, nullptr));
    if (!reply || reply->format != 8) {
        return out;
    }
    const char*  raw = static_cast<const char*>(xcb_get_property_value(reply.get()));
    const size_t len = static_cast<size_t>(xcb_get_property_value_length(reply.get()));
    for_each_nul_entry(raw, len, [&](std::string_view entry) {
        const size_t t1 = entry.find('\t');
        const size_t t2 = (t1 == std::string_view::npos) ? std::string_view::npos : entry.find('\t', t1 + 1U);
        if (t1 == std::string_view::npos || t2 == std::string_view::npos)
            return;
        const std::string_view flag = entry.substr(t2 + 1U);
        if (flag != "1")
            return;
        out[std::string(entry.substr(t1 + 1U, t2 - t1 - 1U))] = true;
    });
    return out;
}

/* Parse `_NET_ZESTWM_SPECIAL_HIDDEN_IDS` (`tag -> hidden-id`). */
std::unordered_map<std::string, uint32_t> special_hidden_id_by_tag(xcb_connection_t* c, xcb_window_t root) {
    std::unordered_map<std::string, uint32_t> out;
    xcb_atom_t                                prop = intern_atom(c, "_NET_ZESTWM_SPECIAL_HIDDEN_IDS");
    xcb_atom_t                                utf8 = intern_atom(c, "UTF8_STRING");
    if (prop == XCB_NONE || utf8 == XCB_NONE)
        return out;
    xcb_get_property_cookie_t cookie = xcb_get_property(c, 0, root, prop, utf8, 0, 1U << 20);
    auto                      reply  = make_xcb_reply_ptr(xcb_get_property_reply(c, cookie, nullptr));
    if (!reply || reply->format != 8) {
        return out;
    }
    const char*  raw = static_cast<const char*>(xcb_get_property_value(reply.get()));
    const size_t len = static_cast<size_t>(xcb_get_property_value_length(reply.get()));
    for_each_nul_entry(raw, len, [&](std::string_view entry) {
        const size_t t1 = entry.find('\t');
        if (t1 == std::string_view::npos || t1 + 1U >= entry.size())
            return;
        const std::string   tag(entry.substr(0, t1));
        const unsigned long hid = strtoul(std::string(entry.substr(t1 + 1U)).c_str(), nullptr, 10);
        if (hid == 0UL || hid > 0xffffffffUL)
            return;
        out[tag] = static_cast<uint32_t>(hid);
    });
    return out;
}

/* Parse a UTF-8, NUL-separated `id:symbol` root property into (id, symbol) pairs.
 * Shared shape for `_NET_ZEST_LAYOUT_LIST` (all layouts) and `_NET_ZEST_LAYOUTS`
 * (per-monitor current layout); malformed or negative-id entries are skipped. */
static std::vector<std::pair<uint32_t, std::string>> parse_id_symbol_list(xcb_connection_t* c, xcb_window_t root, xcb_atom_t atom) {
    std::vector<std::pair<uint32_t, std::string>> out;
    if (atom == XCB_ATOM_NONE)
        return out;
    xcb_atom_t                utf8   = intern_atom(c, "UTF8_STRING");
    xcb_get_property_cookie_t cookie = xcb_get_property(c, 0, root, atom, utf8, 0, 4096);
    auto                      reply  = make_xcb_reply_ptr(xcb_get_property_reply(c, cookie, nullptr));
    if (!reply || reply->format != 8)
        return out;
    const char* data = static_cast<const char*>(xcb_get_property_value(reply.get()));
    const int   len  = xcb_get_property_value_length(reply.get());
    int         i    = 0;
    while (i < len) {
        const char*  entry = data + i;
        const size_t elen  = strnlen(entry, static_cast<size_t>(len - i));
        if (elen == 0U) {
            i++;
            continue;
        }
        const char* colon = static_cast<const char*>(memchr(entry, ':', elen));
        if (colon) {
            char*      end = nullptr;
            const long id  = strtol(std::string(entry, static_cast<size_t>(colon - entry)).c_str(), &end, 10);
            if (end && *end == '\0' && id >= 0)
                out.push_back({static_cast<uint32_t>(id), std::string(colon + 1, static_cast<size_t>((entry + elen) - (colon + 1)))});
        }
        i += static_cast<int>(elen) + 1;
    }
    return out;
}

/* Resolve monitor layout symbol from `_NET_ZEST_LAYOUTS`. */
std::string get_layout_for_monitor(xcb_connection_t* c, xcb_window_t root, uint32_t mon_id) {
    for (const auto& [id, sym] : parse_id_symbol_list(c, root, intern_atom(c, "_NET_ZEST_LAYOUTS"))) {
        if (id == mon_id)
            return sym;
    }
    return std::string();
}

/* Read layout index/symbol pairs from `_NET_ZEST_LAYOUT_LIST`. */
std::vector<std::pair<uint32_t, std::string>> get_layout_list(xcb_connection_t* c, xcb_window_t root) {
    return parse_id_symbol_list(c, root, intern_atom(c, "_NET_ZEST_LAYOUT_LIST"));
}
