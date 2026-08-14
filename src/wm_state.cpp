#include "wm_state.hpp"
#include "bsp/add_flow.hpp"
#include "bsp/tree_ops.hpp"
#include "bsp/tree_serialize.hpp"
#include "bsp/workspace_store.hpp"
#include "client/client_lifecycle.hpp"
#include "client/client_props.hpp"
#include "config.hpp"
#include "intern.hpp"
#include "layout_tree.hpp"
#include "monitor/world_state.hpp"
#include "monitor/persist_key.hpp"
#include "special_workspace_registry.hpp"
#include "monitor/monitor_model.hpp"
#include "util.hpp"
#include "workspace_id.hpp"
#include "workspace_registry.hpp"
#include "x11/backend.hpp"
#include "x11/connection.hpp"
#include "x11/reply_ptr.hpp"
#include "state/runtime_authority.hpp"
#include "state/wm_state_root.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <set>
#include <unordered_set>
#include <unordered_map>
#include <memory>
#include <utility>
#include <vector>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace wm::state {
    namespace {
        WMRuntimeAuthority  g_default_runtime_authority{};
        WMRuntimeAuthority* g_active_runtime_authority = &g_default_runtime_authority;
    } // namespace

    WMRuntimeAuthority& runtime_authority() noexcept {
        return *g_active_runtime_authority;
    }

    void install_runtime_authority(WMRuntimeAuthority* const authority) noexcept {
        g_active_runtime_authority = authority ? authority : &g_default_runtime_authority;
    }

    Client*& lastfocused_slot() noexcept {
        return g_active_runtime_authority->ref_last_focused();
    }
} // namespace wm::state

int                               screen;
int                               sw, sh;
int                               bh, th, lrpad;
unsigned int                      numlockmask = 0;
Atom                              wmatom[WMLast], netatom[NetLast];
Atom                              utf8_atom;
int                               running = 1;
int                               restart = 0;
Cur*                              cursor[kCursorSlotCount];
std::vector<std::array<Clr, 3>>   scheme;
xcb_connection_t*                 xcb;
xcb_screen_t*                     xscreen;
xcb_visualtype_t*                 root_visual;
uint8_t                           root_depth;
xcb_cursor_context_t*             cursor_ctx = nullptr;
std::unique_ptr<wm::draw::Canvas> canvas{};
unsigned int                      canvas_font_height = 0U;
Window                            root, wmcheckwin;
/* Last-written payload cache for a root property: skips redundant X writes/deletes
 * when the payload is unchanged from the previous persist. */
struct PersistCache {
    std::string payload;
    bool        valid = false;
};
static PersistCache       g_special_overlay_state_cache;
static PersistCache       g_special_hidden_id_state_cache;
static int                treestate_restore_complete             = 0;
static int                selectionstate_restore_complete        = 0;
static int                special_overlay_state_restore_complete = 0;
static constexpr uint32_t kSelectionStateVersionWorkspaceId      = 1U;

static int                state_persist_frozen(void) {
    return restart && !running;
}

/* Ensure workspace registry includes the workspace id (may grow ordered names). */
static void ensure_workspace_id_registered(WorkspaceId id) {
    if (id < kWorkspaceIdMin)
        return;
    ensure_workspace_registry_for_id(id);
}

/* Flush persisted state updates through active backend context when available. */
static void backend_flush_connection(void) {
    if (X11Backend* backend_ctx = x11_backend_peek_context()) {
        backend_ctx->flush();
        return;
    }
    if (xcb_connection_t* const conn = wm::x11::connection())
        xcb_flush(conn);
}

/* Persist a UTF-8, NUL-separated payload to a root property (the shared tail of every
 * save*state that emits a string blob). An empty payload deletes the property. When
 * `cache` is provided, payloads identical to the previous persist are skipped to avoid
 * redundant X round-trips; the cache mirrors what was last written. */
static void persist_utf8_root_property(xcb_atom_t prop, std::string buf, PersistCache* cache = nullptr) {
    xcb_connection_t* const conn     = wm::x11::connection();
    const Window            root_win = wm::x11::root_window();
    if (!conn)
        return;
    if (buf.empty()) {
        if (cache && cache->valid && cache->payload.empty())
            return;
        xcb_delete_property(conn, static_cast<xcb_window_t>(root_win), prop);
        backend_flush_connection();
        if (cache) {
            cache->payload.clear();
            cache->valid = true;
        }
        return;
    }
    if (cache && cache->valid && cache->payload == buf)
        return;
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, static_cast<xcb_window_t>(root_win), prop, static_cast<xcb_atom_t>(wm::x11::utf8_string_atom()), 8,
                        static_cast<uint32_t>(buf.size()), buf.data());
    backend_flush_connection();
    if (cache) {
        cache->payload = std::move(buf);
        cache->valid   = true;
    }
}

/* Persist a CARDINAL32 array to a root property (mirror of persist_utf8_root_property
 * for the non-string save*state emitters). A zero-length payload deletes the property. */
static void persist_cardinal_root_property(xcb_atom_t prop, const uint32_t* data, uint32_t count) {
    xcb_connection_t* const conn     = wm::x11::connection();
    const Window            root_win = wm::x11::root_window();
    if (!conn)
        return;
    if (count == 0U) {
        xcb_delete_property(conn, static_cast<xcb_window_t>(root_win), prop);
        backend_flush_connection();
        return;
    }
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, static_cast<xcb_window_t>(root_win), prop, XCB_ATOM_CARDINAL, 32, count, data);
    backend_flush_connection();
}

/* Decode persisted selection payload workspace field (v1 WorkspaceId only). */
[[nodiscard]] static WorkspaceId decode_selection_state_workspace_id(const uint32_t* data, unsigned long count) {
    if (!data || count < 4U)
        return kWorkspaceIdMin;
    if (data[3] != kSelectionStateVersionWorkspaceId)
        return kWorkspaceIdMin;
    return static_cast<WorkspaceId>(data[1]);
}

void monitor_set_active_workspace_id(Monitor* m, WorkspaceId id) {
    if (!m)
        return;
    if (id < kWorkspaceIdMin)
        return;
    ensure_workspace_id_registered(id);
    m->active_workspace_id = id;
}

int monitor_workspace_contains_id(const Monitor* m, WorkspaceId id) {
    if (!m || id < kWorkspaceIdMin)
        return 0;
    return m->active_workspace_id == id ? 1 : 0;
}

int count_clients_on_workspace(WorkspaceId id) {
    if (id < kWorkspaceIdMin)
        return 0;
    const WorkspaceRef ws = WorkspaceRef::normal(id);
    int                n  = 0;
    for (Monitor* mon : wm::state::all_monitors()) {
        for (Client* x : mon->clients) {
            if (!x->isdock && client_tree_member_on_workspace(x, mon, ws))
                ++n;
        }
    }
    return n;
}

/* Until scratchpad is a “hidden desktop” in the same viewed stack, special visibility == overlay policy for this tag. */
static int client_visible_special_on_monitor(const Client* c, const Monitor* m) {
    if (!c || !m || c->mon != m)
        return 0;
    return monitor_special_overlay_shows_tag(m, c->workspace.special_tag);
}

static int client_visible_normal_on_monitor(const Client* c, const Monitor* m) {
    if (!c || !m || !c->workspace.is_normal())
        return 0;
    return monitor_workspace_contains_id(m, c->workspace.normal_id);
}

int client_is_visible_on_monitor(const Client* c, const Monitor* m) {
    if (!c || !m)
        return 0;
    if (c->isdock)
        return 1;
    if (c->workspace.is_special())
        return client_visible_special_on_monitor(c, m);
    return client_visible_normal_on_monitor(c, m);
}

std::optional<WorkspaceId> workspace_hidden_id_for_special_tag(const std::string_view tag) {
    return special_workspace_registry_hidden_id_by_tag(tag);
}

std::optional<WorkspaceRef> workspace_special_ref_from_hidden_id(const WorkspaceId hidden_id) {
    const std::optional<std::uint8_t> slot = special_workspace_registry_slot_by_hidden_id(hidden_id);
    if (!slot.has_value())
        return std::nullopt;
    const SpecialWorkspaceMeta* meta = special_workspace_registry_at(static_cast<std::size_t>(*slot));
    if (!meta)
        return std::nullopt;
    return WorkspaceRef::special(meta->tag);
}

WorkspaceRef workspace_normalize_special_ref_with_hidden_id(const WorkspaceRef& ws, const std::optional<WorkspaceId> hidden_id) {
    if (!ws.is_special())
        return ws;
    /* Prefer explicit special tag identity from payload/config over bridge metadata.
     * This avoids cross-tag remap when cached hidden-id metadata becomes stale after slot reorder. */
    if (const std::optional<WorkspaceId> mapped_hidden = workspace_hidden_id_for_special_tag(ws.special_tag); mapped_hidden.has_value())
        if (const std::optional<WorkspaceRef> from_hidden = workspace_special_ref_from_hidden_id(*mapped_hidden); from_hidden.has_value())
            return *from_hidden;
    if (hidden_id.has_value()) {
        if (const std::optional<WorkspaceRef> from_hidden = workspace_special_ref_from_hidden_id(*hidden_id); from_hidden.has_value())
            return *from_hidden;
    }
    return ws;
}

void monitor_free_workspace_trees(Monitor* m) {
    if (!m)
        return;
    m->workspace_bsp_roots_by_ref.clear();
}

void setclientstate(Client* c, long state) {
    uint32_t data[] = {static_cast<uint32_t>(state), static_cast<uint32_t>(0)};

    if (!wm::x11::connection())
        return;
    xcb_change_property(wm::x11::connection(), XCB_PROP_MODE_REPLACE, static_cast<xcb_window_t>(c->win), static_cast<xcb_atom_t>(wm::x11::wm_atom(WMState)),
                        static_cast<xcb_atom_t>(wm::x11::wm_atom(WMState)), 32, 2, data);
    backend_flush_connection();
}

void setclientworkspaceprop(Client* c) {
    constexpr uint32_t kNetWmDesktopAll = 0xFFFFFFFFU;
    const WorkspaceId  wid_for_ewmh     = client_workspace_normal_id(c);
    uint32_t           desktop          = kNetWmDesktopAll;
    if (!c->isdock && wid_for_ewmh >= kWorkspaceIdMin) {
        desktop = static_cast<uint32_t>(wid_for_ewmh - 1U);
    }

    if (!wm::x11::connection())
        return;
    xcb_change_property(wm::x11::connection(), XCB_PROP_MODE_REPLACE, static_cast<xcb_window_t>(c->win), static_cast<xcb_atom_t>(wm::x11::net_atom(NetWMDesktop)),
                        XCB_ATOM_CARDINAL, 32, 1, &desktop);
    backend_flush_connection();
}

/* Export dynamic workspace registry to EWMH desktop properties on the root window. */
void update_net_desktop_props(void) {
    constexpr std::size_t kMaxDesktopNamesBytes = 4096U;
    const std::size_t     reg_count             = workspace_registry_count();
    WorkspaceId           max_seen_workspace_id = reg_count > 0U ? static_cast<WorkspaceId>(reg_count) : kWorkspaceIdMin;
    unsigned int          nd;
    uint32_t              nd32;
    uint32_t              cur32 = 0;
    Monitor*              m     = wm::state::monitor_or_fallback(wm::state::runtime_authority().ref_current_monitor(), wm::state::mons_slot());

    for (Monitor* it_m : wm::state::all_monitors()) {
        if (it_m->active_workspace_id > max_seen_workspace_id)
            max_seen_workspace_id = it_m->active_workspace_id;
        for (Client* c : it_m->clients) {
            if (c->isdock)
                continue;
            if (c->workspace.is_normal() && c->workspace.normal_id > max_seen_workspace_id)
                max_seen_workspace_id = c->workspace.normal_id;
        }
    }
    workspace_registry_ensure_id(max_seen_workspace_id);

    nd   = static_cast<unsigned>(workspace_registry_count());
    nd32 = nd;
    if (!wm::x11::connection() || nd == 0)
        return;
    if (m && m->active_workspace_id >= kWorkspaceIdMin) {
        const uint32_t active_index = static_cast<uint32_t>(m->active_workspace_id - 1U);
        if (active_index < nd)
            cur32 = active_index;
    }
    xcb_change_property(wm::x11::connection(), XCB_PROP_MODE_REPLACE, static_cast<xcb_window_t>(wm::x11::root_window()),
                        static_cast<xcb_atom_t>(wm::x11::net_atom(NetNumberOfDesktops)), XCB_ATOM_CARDINAL, 32, 1, &nd32);
    xcb_change_property(wm::x11::connection(), XCB_PROP_MODE_REPLACE, static_cast<xcb_window_t>(wm::x11::root_window()),
                        static_cast<xcb_atom_t>(wm::x11::net_atom(NetCurrentDesktop)), XCB_ATOM_CARDINAL, 32, 1, &cur32);

    std::string names;
    names.reserve(std::min(static_cast<std::size_t>(nd) * 16U, kMaxDesktopNamesBytes));
    for (std::size_t i = 0; i < static_cast<std::size_t>(nd); ++i) {
        const WorkspaceMeta* meta = workspace_registry_at(i);
        if (!meta)
            break;
        if (i > 0U) {
            if (names.size() >= kMaxDesktopNamesBytes - 1U)
                break;
            names.push_back('\0');
        }
        const std::string& ws_name = meta->name;
        std::size_t        len     = ws_name.size();
        if (names.size() + len >= kMaxDesktopNamesBytes)
            len = kMaxDesktopNamesBytes - names.size() - 1U;
        if (len > 0U)
            names.append(ws_name.data(), len);
    }
    if (!names.empty()) {
        if (names.size() < kMaxDesktopNamesBytes)
            names.push_back('\0');
        xcb_change_property(wm::x11::connection(), XCB_PROP_MODE_REPLACE, static_cast<xcb_window_t>(wm::x11::root_window()),
                            static_cast<xcb_atom_t>(wm::x11::net_atom(NetDesktopNames)), static_cast<xcb_atom_t>(wm::x11::utf8_string_atom()), 8,
                            static_cast<uint32_t>(names.size()), names.data());
    }
    backend_flush_connection();
}

[[nodiscard]] static int client_matches_persist_workspace(const Client* c, const WorkspaceRef& ref) {
    if (!c)
        return 0;
    if (ref.is_normal())
        return client_on_normal_workspace(c, ref.normal_id) ? 1 : 0;
    if (ref.is_special())
        return client_special_workspace_tag_equals(c, ref.special_tag) ? 1 : 0;
    return 0;
}

void savezestwmstate(void) {
    if (state_persist_frozen())
        return;
    const int nmons = static_cast<int>(wm::state::monitor_count());
    if (nmons == 0)
        return;
    if (!wm::x11::connection())
        return;
    /* Monitor state pairs (num, active_workspace_id) written atomically to the root property. */
    std::vector<uint32_t> data;
    data.reserve(static_cast<std::size_t>(nmons) * 2U);
    for (Monitor* m : wm::state::all_monitors()) {
        data.push_back(static_cast<uint32_t>(m->num));
        data.push_back(static_cast<uint32_t>(m->active_workspace_id));
    }
    persist_cardinal_root_property(static_cast<xcb_atom_t>(wm::x11::net_atom(NetZestwmState)), data.data(), static_cast<uint32_t>(nmons * 2));
}

template <typename Fn>
static void for_each_special_bsp_root(Monitor* m, Fn&& fn) {
    if (!m)
        return;
    for (const auto& [ws, root] : m->workspace_bsp_roots_by_ref) {
        if (!root || !ws.is_special())
            continue;
        fn(ws.special_tag, root.get());
    }
}

/* Every non-null BSP root on `m` (normal desktop slots, then special slots): one loop shape for persist/layout hooks. */
template <typename Fn>
static void for_each_monitor_bsp_root_by_workspace(Monitor* m, Fn&& fn) {
    if (!m)
        return;
    BspWorkspaceStore store(*m);
    const std::size_t reg_count = workspace_registry_count();
    for (WorkspaceId ws_id = kWorkspaceIdMin; ws_id <= static_cast<WorkspaceId>(reg_count); ++ws_id) {
        const WorkspaceRef ws = WorkspaceRef::normal(ws_id);
        LayoutNode* const  r  = store.read(ws);
        if (!r)
            continue;
        fn(ws, r);
    }
    for_each_special_bsp_root(m, [&](const std::string& tag, LayoutNode* r) { fn(WorkspaceRef::special(tag), r); });
}

static int tree_has_clients(LayoutNode* node) {
    if (!node)
        return 0;
    if (node->type == NODE_GROUPED)
        return node->grouped.clients.size() > 0;
    return tree_has_clients(node->split.first.get()) || tree_has_clients(node->split.second.get());
}

/* Apply workspace ownership from serialized grouped-client lists in one tree payload (`...G(...:<win[,win...]>)...`).
 * When `special_normal_conflict` is set (reload restore), skip XIDs listed there for special assignments only.
 * The set is **normal ∩ special** from the blob (before de-duping special lists): stale special entries often still
 * list a client that a normal-desktop entry already owns after movetoworkspace; skipping only that intersection
 * avoids stripping clients that exist solely on special (they may not appear in any normal entry). */
/* Move client onto target monitor without rewriting workspace (tree restore sets that next). */
static void rehome_client_for_tree_restore(Client* c, Monitor* m) {
    if (!c || !m || c->mon == m)
        return;
    Monitor* const src = c->mon;
    if (src) {
        bsp_remove_client(c);
        client_unlink(src, c);
        client_unlink_stack(src, c);
    }
    c->mon = m;
    client_link(m, c);
    client_link_stack(m, c);
}

static void apply_workspace_from_tree_payload(Monitor* m, const WorkspaceRef& assign_ws, std::string_view payload,
                                              const std::unordered_set<Window>* special_normal_conflict = nullptr) {
    if (!m || payload.empty())
        return;
    for_each_window_id(payload, [&](unsigned long win) {
        const Window w = static_cast<Window>(win);
        if (special_normal_conflict != nullptr && assign_ws.is_special() && special_normal_conflict->count(w) != 0U)
            return;
        Client* c = wintoclient(w);
        if (!c || c->isdock)
            return;
        rehome_client_for_tree_restore(c, m);
        if (c->mon != m)
            return;
        if (c->workspace_set_by_rule && c->workspace != assign_ws)
            return;
        /* Tree-state restore is authoritative for workspace ownership at startup reload. */
        c->workspace = assign_ws;
    });
}

/* Scan one NUL-separated tree-state entry and insert decimal window ids from its BSP payload. */
static void collect_tree_window_ids_from_entry_payload(std::string_view payload, std::unordered_set<Window>& out) {
    /* The payload may carry a `|F(...)` floating suffix; exclude it so floating
     * geometry values (e.g. an `h` field like `500` followed by `,`) are not
     * mis-extracted as window ids. */
    const size_t fpos = payload.find("|F(");
    if (fpos != std::string_view::npos)
        payload = payload.substr(0, fpos);
    for_each_window_id(payload, [&](unsigned long win) { out.insert(static_cast<Window>(win)); });
}

/* Collect window ids from persisted tree entries for either special or normal workspaces.
 * `want_special` selects which workspace kind to match (same blob holds both entry types). */
static void collect_tree_windows_by_kind(const char* raw, size_t len, bool want_special, std::unordered_set<Window>& out) {
    for_each_nul_entry(raw, len, [&](std::string_view entry_sv) {
        const size_t p0 = entry_sv.find(':');
        const size_t p1 = (p0 == std::string_view::npos) ? std::string_view::npos : entry_sv.find(':', p0 + 1U);
        if (p0 == std::string_view::npos || p1 == std::string_view::npos)
            return;
        const std::optional<WorkspaceRef> assign_ws = wm::bsp::decode_persist_workspace_ref(std::string(entry_sv.substr(p0 + 1U, p1 - p0 - 1U)), 1);
        if (!assign_ws.has_value())
            return;
        /* Normal entries are authoritative when a client moved special -> normal before reload. */
        const bool kind_match = want_special ? assign_ws->is_special() : assign_ws->is_normal();
        if (!kind_match)
            return;
        collect_tree_window_ids_from_entry_payload(entry_sv.substr(p1 + 1U), out);
    });
}

void savezesttreestate(void) {
    std::string buf;

    if (state_persist_frozen() || !wm::x11::connection())
        return;
    if (!treestate_restore_complete) {
        return;
    }
    for (Monitor* m : wm::state::all_monitors()) {
        for_each_monitor_bsp_root_by_workspace(m, [&](const WorkspaceRef& ws, LayoutNode* rootn) {
            /* Collect floating clients on this workspace (they live in m->clients, not the
             * tree, so the serialized BSP tree would miss them). Emit a `|F(win:x:y:w:h,...)`
             * suffix so they survive reload alongside the tree structure. */
            std::vector<wm::bsp::FloatingRecord> frecs;
            for (Client* c : m->clients) {
                if (!c->isfloating || c->isdock || c->leaf != nullptr)
                    continue;
                if (!client_matches_persist_workspace(c, ws))
                    continue;
                frecs.push_back({c->win, c->x, c->y, c->w, c->h});
            }
            const std::string float_suffix = wm::bsp::format_floating_suffix(frecs);
            /* Emit the entry when there are tiled clients OR floating clients (a workspace
             * holding only floating clients must still be persisted). */
            if (!tree_has_clients(rootn) && float_suffix.empty())
                return;
            buf += wm::monitor::format_persist_monitor_key(m->output_name, m->num);
            buf += ":";
            buf += wm::bsp::format_persist_workspace_key(ws);
            buf += ":";
            if (tree_has_clients(rootn)) {
                if (const auto sn = wm::bsp::serialized_from_layout(rootn))
                    buf += wm::bsp::serialize_tree(*sn);
            }
            buf += float_suffix;
            buf.push_back('\0');
        });
    }
    persist_utf8_root_property(static_cast<xcb_atom_t>(wm::x11::net_atom(NetZestTreeState)), std::move(buf));
}

/* Apply the `|F(win:x:y:w:h,...)` floating suffix from a tree-state entry to the matching
 * clients on monitor m. For each window id, set isfloating, restore its saved geometry, clamp
 * to the monitor, and pull it out of the BSP tree (floating clients are never tiled). */
static void apply_floating_suffix(Monitor* m, std::string_view suffix) {
    for (const wm::bsp::FloatingRecord& r : wm::bsp::parse_floating_suffix(suffix)) {
        Client* c = wintoclient(r.win);
        if (!c || !m)
            continue;
        rehome_client_for_tree_restore(c, m);
        if (c->mon != m)
            continue;
        c->isfloating = 1;
        c->x          = r.x;
        c->y          = r.y;
        c->w          = r.w;
        c->h          = r.h;
        if (c->leaf)
            bsp_remove_client(c);
        /* Clamp to the monitor work area like adopt_client() does for restored floating geometry. */
        if (c->w < 1)
            c->w = 1;
        if (c->h < 1)
            c->h = 1;
    }
}

void restorezesttreestate(void) {
    xcb_get_property_cookie_t cookie;
    const char*               raw;
    size_t                    len;

    if (!wm::x11::connection()) {
        treestate_restore_complete = 1;
        return;
    }
    cookie     = xcb_get_property(wm::x11::connection(), 0, static_cast<xcb_window_t>(wm::x11::root_window()), static_cast<xcb_atom_t>(wm::x11::net_atom(NetZestTreeState)),
                                  static_cast<xcb_atom_t>(wm::x11::utf8_string_atom()), 0, 1U << 20);
    auto reply = make_xcb_reply_ptr(xcb_get_property_reply(wm::x11::connection(), cookie, nullptr));
    if (!reply) {
        treestate_restore_complete = 1;
        return;
    }
    if (reply->format != 8) {
        treestate_restore_complete = 1;
        return;
    }
    raw = static_cast<const char*>(xcb_get_property_value(reply.get()));
    len = static_cast<size_t>(xcb_get_property_value_length(reply.get()));
    std::unordered_set<Window> special_tree_windows;
    collect_tree_windows_by_kind(raw, len, /*want_special=*/true, special_tree_windows);
    /* If the same XID still appears in a stale special entry but also in a normal-desktop entry,
     * normal ownership wins (e.g. movetoworkspace from special to numeric before reload). */
    std::unordered_set<Window> normal_tree_windows;
    collect_tree_windows_by_kind(raw, len, /*want_special=*/false, normal_tree_windows);
    std::unordered_set<Window> special_normal_conflict;
    for (const Window w : normal_tree_windows) {
        if (special_tree_windows.count(w) != 0U)
            special_normal_conflict.insert(w);
    }
    for (auto it = special_tree_windows.begin(); it != special_tree_windows.end();) {
        if (normal_tree_windows.count(*it) != 0U)
            it = special_tree_windows.erase(it);
        else
            ++it;
    }

    for_each_nul_entry(raw, len, [&](std::string_view entry_sv) {
        Monitor*                    m = nullptr;
        std::set<Window>            used;
        std::unique_ptr<LayoutNode> parsed;

        const size_t                p0 = entry_sv.find(':');
        const size_t                p1 = (p0 == std::string_view::npos) ? std::string_view::npos : entry_sv.find(':', p0 + 1U);
        if (p0 == std::string_view::npos || p1 == std::string_view::npos)
            return;
        const auto mon_key = wm::monitor::parse_persist_monitor_key(entry_sv.substr(0, p0));
        if (!mon_key.has_value())
            return;
        const std::optional<WorkspaceRef> assign_ws = wm::bsp::decode_persist_workspace_ref(std::string(entry_sv.substr(p0 + 1U, p1 - p0 - 1U)), 1);
        if (!assign_ws.has_value())
            return;
        for (Monitor* it : wm::state::all_monitors()) {
            if (!mon_key->numeric) {
                if (!it->output_name.empty() && it->output_name == mon_key->name) {
                    m = it;
                    break;
                }
            } else if (static_cast<unsigned long>(it->num) == mon_key->num) {
                m = it;
                break;
            }
        }
        /* Numeric keys: also accept matching output_name equal to the digit string (rare). */
        if (!m && mon_key->numeric) {
            const std::string_view as_name = entry_sv.substr(0, p0);
            for (Monitor* it : wm::state::all_monitors()) {
                if (it->output_name == as_name) {
                    m = it;
                    break;
                }
            }
        }
        if (!m)
            return;
        if (assign_ws->is_normal() && assign_ws->normal_id < kWorkspaceIdMin)
            return;
        /* Split the payload into the tree-node portion and the optional `|F(...)` floating
         * suffix so floating window ids (which use `:` as field delimiter, not the tree's
         * `,`/`)`) are not misinterpreted by tree-window extraction. */
        const std::string_view payload_full = entry_sv.substr(p1 + 1U);
        std::string_view       payload_tree = payload_full;
        std::string_view       float_suffix;
        const size_t           fpos = payload_full.find("|F(");
        if (fpos != std::string_view::npos) {
            payload_tree = payload_full.substr(0, fpos);
            float_suffix = payload_full.substr(fpos);
        }
        apply_workspace_from_tree_payload(m, *assign_ws, payload_tree, assign_ws->is_special() ? &special_normal_conflict : nullptr);
        /* Detach scan-time membership before rebuild so clients are not left in stale leaves.
         * Clear rejoin hints: bsp_remove_client records peer/slot for interactive moves; restore
         * must not let those hints force orphans back into a tab group. */
        for (Client* c : m->clients) {
            if (c->isdock || !client_matches_persist_workspace(c, *assign_ws))
                continue;
            if (c->leaf)
                bsp_remove_client(c);
            c->leaf                       = nullptr;
            c->rejoin_group_peer_win      = 0;
            c->rejoin_group_slot_plus_one = 0;
            c->rejoin_bsp                 = ClientBspRejoinHint{};
        }
        /* Drop the scan-time root for this workspace; rebuilt tree replaces it below. */
        static_cast<void>(BspWorkspaceStore(*m).take(*assign_ws));
        /* Apply the floating suffix before tree parse so floating clients are already
         * marked isfloating when the catch-all sweep runs (the guard skips them). */
        if (!float_suffix.empty())
            apply_floating_suffix(m, float_suffix);

        if (payload_tree.empty()) {
            /* Floating-only entry: reattach any still-tiled clients with normal BSP policy. */
            for (Client* c : m->clients) {
                if (c->isdock || c->isfloating || c->leaf || !client_matches_persist_workspace(c, *assign_ws))
                    continue;
                if (assign_ws->is_normal() && special_tree_windows.count(c->win))
                    continue;
                bsp_add_client(c, m);
            }
            return;
        }
        /* Parse the tree-node portion into a pure SerializedNode, then bind live clients.
         * `parse_tree` stops after the root node's closing paren, leaving any `|F(...)`
         * suffix unconsumed, so we can parse the full entry starting at the payload offset. */
        std::size_t pnode = p1 + 1U;
        auto        sn    = wm::bsp::parse_tree(entry_sv, pnode);
        if (!sn) {
            for (Client* c : m->clients) {
                if (c->isdock || c->isfloating || c->leaf || !client_matches_persist_workspace(c, *assign_ws))
                    continue;
                if (assign_ws->is_normal() && special_tree_windows.count(c->win))
                    continue;
                bsp_add_client(c, m);
            }
            return;
        }
        const std::unordered_set<Window>* conflict = assign_ws->is_special() ? &special_normal_conflict : nullptr;
        parsed                                     = wm::bsp::build_layout_tree(*sn, [&](LayoutNode* leaf_node, Window win) {
            Client* c = wintoclient(win);
            if (!c || used.count(win) != 0U)
                return;
            if (conflict != nullptr && conflict->count(win) != 0U)
                return;
            rehome_client_for_tree_restore(c, m);
            if (c->mon != m)
                return;
            if (c->workspace_set_by_rule && c->workspace != *assign_ws)
                return;
            /* Tree-state restore is authoritative for workspace ownership at startup reload. */
            c->workspace = *assign_ws;
            if (lt_grouped_add(leaf_node, c)) {
                c->leaf = leaf_node;
                used.insert(win);
            }
        });
        if (!parsed) {
            for (Client* c : m->clients) {
                if (c->isdock || c->isfloating || c->leaf || !client_matches_persist_workspace(c, *assign_ws))
                    continue;
                if (assign_ws->is_normal() && special_tree_windows.count(c->win))
                    continue;
                bsp_add_client(c, m);
            }
            return;
        }

        /* Clients present on this workspace but missing from the serialized tree (or skipped by
         * conflict/rule checks) are reattached after the root is installed, using normal BSP
         * policy. Force-merging them into first_grouped() incorrectly creates tab groups. */
        std::vector<Client*> orphans;
        for (Client* c : m->clients) {
            if (c->isdock || c->isfloating || !client_matches_persist_workspace(c, *assign_ws) || c->leaf)
                continue;
            if (assign_ws->is_normal() && special_tree_windows.count(c->win))
                continue;
            orphans.push_back(c);
        }

        parsed->parent = nullptr;
        BspWorkspaceStore(*m).set(*assign_ws, std::move(parsed));
        if (assign_ws->is_normal() && assign_ws->normal_id == m->active_workspace_id)
            MonitorWorldState(*m).set_viewed(*assign_ws);
        for (Client* c : orphans) {
            if (!c || c->leaf)
                continue;
            bsp_add_client(c, m);
        }
    });

    treestate_restore_complete = 1;
    updatefloatingclientlist();
}

void savezestselectionstate(void) {
    uint32_t data[4];
    Monitor* current = wm::state::runtime_authority().ref_current_monitor();
    if (state_persist_frozen() || !wm::x11::connection() || !current || !selectionstate_restore_complete)
        return;
    if (current->active_workspace_id < kWorkspaceIdMin)
        return;
    data[0] = static_cast<uint32_t>(current->num);
    data[1] = static_cast<uint32_t>(current->active_workspace_id);
    data[2] = static_cast<uint32_t>(current->sel ? current->sel->win : 0U);
    data[3] = kSelectionStateVersionWorkspaceId; /* v1: data[1] stores WorkspaceId instead of raw mask. */
    persist_cardinal_root_property(static_cast<xcb_atom_t>(wm::x11::net_atom(NetZestSelectionState)), data, 4U);
}

void restorezestselectionstate(void) {
    xcb_get_property_cookie_t cookie;
    uint32_t*                 data;
    unsigned long             n;
    unsigned int              monnum;
    Window                    selwin;
    Monitor*                  m = nullptr;
    Client*                   c;
    WorkspaceId               restore_workspace_id = kWorkspaceIdMin;

    if (!wm::x11::connection()) {
        selectionstate_restore_complete = 1;
        return;
    }
    cookie     = xcb_get_property(wm::x11::connection(), 0, static_cast<xcb_window_t>(wm::x11::root_window()), static_cast<xcb_atom_t>(wm::x11::net_atom(NetZestSelectionState)),
                                  XCB_ATOM_CARDINAL, 0, 16);
    auto reply = make_xcb_reply_ptr(xcb_get_property_reply(wm::x11::connection(), cookie, nullptr));
    if (!reply) {
        selectionstate_restore_complete = 1;
        return;
    }
    if (reply->format != 32) {
        selectionstate_restore_complete = 1;
        return;
    }
    n    = xcb_get_property_value_length(reply.get()) / sizeof(uint32_t);
    data = static_cast<uint32_t*>(xcb_get_property_value(reply.get()));
    if (n < 3) {
        selectionstate_restore_complete = 1;
        return;
    }
    monnum               = data[0];
    selwin               = static_cast<Window>(data[2]);
    restore_workspace_id = decode_selection_state_workspace_id(data, n);
    ensure_workspace_id_registered(restore_workspace_id);
    Monitor*& current_ref = wm::state::runtime_authority().ref_current_monitor();
    for (Monitor* it : wm::state::all_monitors()) {
        if (static_cast<unsigned int>(it->num) == monnum) {
            m = it;
            break;
        }
    }
    if (!m) {
        Monitor* cur = current_ref;
        m            = cur ? cur : wm::state::mons_slot();
    }
    if (!m) {
        selectionstate_restore_complete = 1;
        return;
    }
    current_ref = m;
    if (restore_workspace_id >= kWorkspaceIdMin)
        monitor_set_active_workspace_id(m, restore_workspace_id);
    MonitorWorldState(*m).sync_viewed_from_active_workspace();
    if (selwin) {
        c = wintoclient(selwin);
        if (c && c->mon == m && client_is_visible_on_monitor(c, m))
            m->sel = c;
    }
    selectionstate_restore_complete = 1;
}

void savezestlayoutstate(void) {
    char   buf[4096];
    size_t pos = 0;

    if (state_persist_frozen())
        return;
    if (!wm::x11::connection())
        return;
    for (Monitor* m : wm::state::all_monitors()) {
        const char* sym = monitor_active_layout(m) ? monitor_active_layout(m)->symbol.c_str() : "";
        char        ent[128];
        int         n = snprintf(ent, sizeof ent, "%u:%s", static_cast<unsigned>(m->num), sym);

        if (n < 0)
            continue;
        if (pos + static_cast<size_t>(n) + 1U >= sizeof buf)
            break;
        memcpy(buf + pos, ent, static_cast<size_t>(n));
        pos += static_cast<size_t>(n);
        buf[pos++] = '\0';
    }
    persist_utf8_root_property(static_cast<xcb_atom_t>(wm::x11::net_atom(NetZestLayouts)), std::string(buf, pos));
}

void savezestlayoutliststate(void) {
    char   buf[4096];
    size_t pos = 0, i;

    if (!wm::x11::connection())
        return;
    for (i = 0; i < g_config.layouts.size(); i++) {
        const char* sym = g_config.layouts[i].symbol.c_str();
        char        ent[128];
        int         n = snprintf(ent, sizeof ent, "%u:%s", static_cast<unsigned>(i), sym);

        if (n < 0)
            continue;
        if (pos + static_cast<size_t>(n) + 1U >= sizeof buf)
            break;
        memcpy(buf + pos, ent, static_cast<size_t>(n));
        pos += static_cast<size_t>(n);
        buf[pos++] = '\0';
    }
    persist_utf8_root_property(static_cast<xcb_atom_t>(wm::x11::net_atom(NetZestLayoutList)), std::string(buf, pos));
}

/* `_NET_ZESTWM_SPECIAL_OVERLAY`: UTF-8, NUL-separated records `mon\\ttag\\t0|1` (one row per monitor; tag only when open). */
void savezestspecialoverlaystate(void) {
    if (state_persist_frozen() || !wm::x11::connection())
        return;
    /* Skip until restore finishes so scan-time arrange cannot clobber the pre-reload blob. */
    if (!special_overlay_state_restore_complete)
        return;
    std::string buf;
    for (Monitor* m : wm::state::all_monitors()) {
        buf += std::to_string(static_cast<unsigned>(m->num));
        buf.push_back('\t');
        if (m->special_overlay_open)
            buf += m->special_overlay_tag;
        buf.push_back('\t');
        buf += m->special_overlay_open ? "1" : "0";
        buf.push_back('\0');
    }
    persist_utf8_root_property(static_cast<xcb_atom_t>(wm::x11::net_atom(NetZestSpecialOverlayState)), std::move(buf), &g_special_overlay_state_cache);
}

/* Restore per-monitor special overlay open/closed + tag from `_NET_ZESTWM_SPECIAL_OVERLAY`. */
void restorezestspecialoverlaystate(void) {
    if (!wm::x11::connection()) {
        special_overlay_state_restore_complete = 1;
        return;
    }
    const xcb_get_property_cookie_t cookie =
        xcb_get_property(wm::x11::connection(), 0, static_cast<xcb_window_t>(wm::x11::root_window()), static_cast<xcb_atom_t>(wm::x11::net_atom(NetZestSpecialOverlayState)),
                         static_cast<xcb_atom_t>(wm::x11::utf8_string_atom()), 0, 1U << 20);
    auto reply = make_xcb_reply_ptr(xcb_get_property_reply(wm::x11::connection(), cookie, nullptr));
    if (!reply || reply->format != 8) {
        special_overlay_state_restore_complete = 1;
        return;
    }
    const char* const raw = static_cast<const char*>(xcb_get_property_value(reply.get()));
    const size_t      len = static_cast<size_t>(xcb_get_property_value_length(reply.get()));
    for_each_nul_entry(raw, len, [&](std::string_view entry) {
        const size_t t1 = entry.find('\t');
        const size_t t2 = (t1 == std::string_view::npos) ? std::string_view::npos : entry.find('\t', t1 + 1U);
        if (t1 == std::string_view::npos || t2 == std::string_view::npos)
            return;
        const unsigned long    monnum = strtoul(std::string(entry.substr(0, t1)).c_str(), nullptr, 10);
        const std::string      tag(entry.substr(t1 + 1U, t2 - t1 - 1U));
        const std::string_view flag = entry.substr(t2 + 1U);
        Monitor*               m    = nullptr;
        for (Monitor* it : wm::state::all_monitors()) {
            if (static_cast<unsigned long>(it->num) == monnum) {
                m = it;
                break;
            }
        }
        if (!m)
            return;
        if (flag == "1" && special_workspace_registry_ensure_tag(tag)) {
            m->special_overlay_open = true;
            m->special_overlay_tag  = tag;
        } else {
            m->special_overlay_open = false;
            m->special_overlay_tag.clear();
        }
    });
    special_overlay_state_restore_complete = 1;
}

/* `_NET_ZESTWM_SPECIAL_HIDDEN_IDS`: UTF-8, NUL-separated `tag\thidden_id` rows from special registry slot mapping. */
void savezestspecialhiddenidstate(void) {
    if (state_persist_frozen() || !wm::x11::connection())
        return;
    std::string       buf;
    const std::size_t n = special_workspace_registry_count();
    for (std::size_t i = 0; i < n; ++i) {
        const SpecialWorkspaceMeta* meta = special_workspace_registry_at(i);
        if (!meta)
            continue;
        const std::optional<WorkspaceId> hidden_id = special_workspace_registry_hidden_id_by_tag(meta->tag);
        if (!hidden_id.has_value())
            continue;
        buf += meta->tag;
        buf.push_back('\t');
        buf += std::to_string(static_cast<unsigned>(*hidden_id));
        buf.push_back('\0');
    }
    persist_utf8_root_property(static_cast<xcb_atom_t>(wm::x11::net_atom(NetZestSpecialHiddenIdState)), std::move(buf), &g_special_hidden_id_state_cache);
}

/* Returns first persisted workspace token whose grouped-client list contains `win` (used for deterministic startup precedence in `adopt_client()`). */
std::optional<WorkspaceRef> tree_state_find_workspace_for_window(Window win) {
    xcb_get_property_cookie_t cookie;
    const char*               raw;
    size_t                    len;
    const std::string         win_token = std::to_string(static_cast<unsigned long>(win));

    if (!wm::x11::connection() || win == 0U)
        return std::nullopt;
    cookie     = xcb_get_property(wm::x11::connection(), 0, static_cast<xcb_window_t>(wm::x11::root_window()), static_cast<xcb_atom_t>(wm::x11::net_atom(NetZestTreeState)),
                                  static_cast<xcb_atom_t>(wm::x11::utf8_string_atom()), 0, 1U << 20);
    auto reply = make_xcb_reply_ptr(xcb_get_property_reply(wm::x11::connection(), cookie, nullptr));
    if (!reply || reply->format != 8) {
        return std::nullopt;
    }
    raw = static_cast<const char*>(xcb_get_property_value(reply.get()));
    len = static_cast<size_t>(xcb_get_property_value_length(reply.get()));

    std::optional<WorkspaceRef> special_match;
    std::optional<WorkspaceRef> normal_match;

    for_each_nul_entry(raw, len, [&](std::string_view entry_sv) -> bool {
        const size_t p0 = entry_sv.find(':');
        const size_t p1 = (p0 == std::string_view::npos) ? std::string_view::npos : entry_sv.find(':', p0 + 1U);
        if (p0 == std::string_view::npos || p1 == std::string_view::npos || p1 + 1U >= entry_sv.size())
            return true;
        const std::optional<WorkspaceRef> assign_ws = wm::bsp::decode_persist_workspace_ref(std::string(entry_sv.substr(p0 + 1U, p1 - p0 - 1U)), 1);
        if (!assign_ws.has_value())
            return true;
        const std::string_view payload    = entry_sv.substr(p1 + 1U);
        size_t                 search_pos = payload.find(win_token);
        while (search_pos != std::string_view::npos) {
            const bool has_prev = search_pos > 0U;
            const bool has_next = (search_pos + win_token.size()) < payload.size();
            const char prev_ch  = has_prev ? payload[search_pos - 1U] : '\0';
            const char next_ch  = has_next ? payload[search_pos + win_token.size()] : '\0';
            const bool prev_ok  = (prev_ch == ':' || prev_ch == ',');
            const bool next_ok  = (next_ch == ',' || next_ch == ')');
            if (prev_ok && next_ok) {
                if (assign_ws->is_special()) {
                    if (!special_match.has_value())
                        special_match = *assign_ws;
                } else {
                    normal_match = *assign_ws;
                    return false;
                }
            }
            search_pos = payload.find(win_token, search_pos + 1U);
        }
        return true;
    });
    if (normal_match.has_value())
        return normal_match;
    return special_match;
}

/* Resolve startup workspace ownership from persisted sources.
 * Hard-cut policy: tree-state is authoritative for restore ownership.
 * Normal desktop entries win over `open-on-workspace` rules so movetoworkspace(normal) survives reload/exec restart. */
std::optional<WorkspaceRef> apply_workspace_from_persistence(Client* c, int workspace_set_by_rule) {
    if (!c)
        return std::nullopt;
    const std::optional<WorkspaceRef> ws_from_tree_state = tree_state_find_workspace_for_window(c->win);
    if (ws_from_tree_state.has_value() && ws_from_tree_state->is_normal())
        return ws_from_tree_state;
    if (workspace_set_by_rule) {
        return std::nullopt;
    }
    if (ws_from_tree_state.has_value() && ws_from_tree_state->is_special()) {
        return std::nullopt;
    }
    return std::nullopt;
}
