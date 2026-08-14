/* Shared WM core types (Client, Monitor, binds, layouts). */
#pragma once

#include "layout_tree.hpp"
#include "workspace_ref.hpp"
#include "x11/backend.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace wm::config::parse {
    struct ActionCommand;
}

enum class CursorKind : unsigned {
    Normal = 0,
    Resize,
    ResizeH,
    ResizeV,
    Move,
    Last
};

enum class SchemeKind : unsigned {
    Normal = 0,
    Selected
};

enum class ClickTarget : unsigned {
    WorkspaceBar = 0,
    GroupBar,
    LayoutSymbol,
    StatusText,
    WindowTitle,
    ClientWindow,
    RootWindow,
    Last
};

struct Monitor;
struct Client;

/** Tiled geometry captured when entering fullscreen; `old*` may change during FS, so restore from this on exit. */
struct ClientFullscreenTileSnap {
    int           x, y, w, h;
    unsigned char valid;
};

/** Sole tile removed from a BSP split: restore sibling edge + ratio after float/fullscreen (no tab peer). */
struct ClientBspRejoinHint {
    Window        neighbor_win; /* representative tiled client in sibling subtree */
    float         split_ratio;
    SplitAxis     split_axis;
    unsigned char was_second_child; /* removed leaf was parent->second */
    unsigned char valid;
};

struct Client {
    std::string              name;
    float                    mina, maxa;
    int                      x, y, w, h;
    int                      oldx, oldy, oldw, oldh;
    ClientFullscreenTileSnap fs_tile;
    int                      basew, baseh, incw, inch, maxw, maxh, minw, minh, hintsvalid;
    int                      bw, oldbw;
    WorkspaceRef             workspace;
    /* Set when a matching windowrule applied `workspace ...`; EWMH restore must not overwrite it. */
    unsigned char workspace_set_by_rule;
    /* From windowrule `workspace ... silent` — keep hidden normal-workspace routes from auto-view on activate. */
    unsigned char workspace_rule_silent;
    /* From windowrule `workspace special:… silent` — avoid auto-focus when opening the special overlay. */
    unsigned char workspace_special_silent;
    int           isfixed, isfloating, isdock, isurgent, neverfocus, oldstate, isfullscreen, needresize;
    /* Window-rule post-adopt_client hooks (0 = unset; see window_rule.cpp). */
    unsigned char rule_fullscreen_pending; /* 0 none, 1 off, 2 on */
    unsigned char rule_center_pending;     /* 0 none, 1 center once, 2 center persist */
    LayoutNode*   leaf;
    /* Set in `bsp_remove_client` when another client remains in the same grouped leaf (float/fullscreen client_unlink). */
    Window rejoin_group_peer_win;
    /* 0 = unset; otherwise tab index in that leaf before client_unlink + 1 (see `lt_grouped_add_at` on reattach). */
    unsigned int        rejoin_group_slot_plus_one;
    ClientBspRejoinHint rejoin_bsp;
    Monitor*            mon;
    Window              win;
};

[[nodiscard]] inline WorkspaceId client_workspace_normal_id(const Client* c) noexcept {
    if (!c || !c->workspace.is_normal())
        return 0U;
    return c->workspace.normal_id;
}

[[nodiscard]] inline bool client_on_normal_workspace(const Client* c, WorkspaceId id) noexcept {
    return c && c->workspace.is_normal() && c->workspace.normal_id == id;
}

[[nodiscard]] inline bool client_workspace_is_unset(const Client* c) noexcept {
    return !c || c->workspace.is_unset();
}

struct Layout {
    std::string symbol;
    void (*arrange)(Monitor*);
};

enum BindFlags : unsigned int {
    BindFlagNone         = 0U,
    BindFlagRelease      = 1U << 0,
    BindFlagRepeat       = 1U << 2,
    BindFlagNonConsuming = 1U << 3,
    BindFlagIgnoreMods   = 1U << 4,
};

struct Key {
    unsigned int mod;
    KeySym       keysym;
    uint8_t      keycode;
    unsigned int flags;
    /* Optional per-bind cooldown in milliseconds (0 = disabled). */
    unsigned int cooldown_ms;
    /* Monotonic timestamp (ms) when this bind can trigger again (runtime state). */
    std::uint64_t cooldown_until_ms;
    void (*func)(const wm::config::parse::ActionCommand*);
    /* Typed bind command payload attached by config parser/runtime bind paths. */
    std::shared_ptr<const wm::config::parse::ActionCommand> command{};
};

struct Button {
    unsigned int click;
    unsigned int mask;
    unsigned int button;
    unsigned int flags;
    void (*func)(const wm::config::parse::ActionCommand* arg);
    /* Typed bind command payload attached by config parser/runtime bind paths. */
    std::shared_ptr<const wm::config::parse::ActionCommand> command{};
};

struct GroupbarSlot {
    Window      win{};
    LayoutNode* anchor{};
    int         x{};
    int         y{};
    int         w{};
    int         h{};
    int         ntabs{};
};

struct Monitor {
    std::string layout_label;
    /* RandR output / monitor name (e.g. DP-1); empty when unknown. Stable identity across reorders. */
    std::string output_name;
    int         num;
    int         by;
    int         ty;
    int         tx, tw, th;
    int         confy, confh;
    int         mx, my, mw, mh;
    int         wx, wy, ww, wh;
    WorkspaceId active_workspace_id;
    /* Dual layout slots for toggle semantics (`layout_slots` + `active_layout_slot`). */
    const Layout*             layout_slots[2]{};
    unsigned int              active_layout_slot{};
    std::vector<Client*>      clients;
    std::vector<Client*>      stack;
    Client*                   sel;
    Window                    barwin;
    Window                    confwin;
    std::vector<GroupbarSlot> groupbars;
    /* Canonical arrange-world layer state: which workspace is viewed/overlay (root from map). */
    struct TreeWorldLayer {
        WorkspaceRef ws{WorkspaceRef::unset()};
    };
    TreeWorldLayer tree_world_viewed{};
    TreeWorldLayer tree_world_overlay{};
    /* Logical workspace -> owned BSP root (normal + special in one keyed map). */
    std::unordered_map<WorkspaceRef, std::unique_ptr<LayoutNode>, WorkspaceRefHash> workspace_bsp_roots_by_ref;
    LayoutNode*                                                                     group_anchor{};
    /* Special workspace overlay on this monitor (floating clients with matching `WorkspaceRef::special_tag`). */
    bool        special_overlay_open{};
    std::string special_overlay_tag{};
    /* Full-area dim layer when `special_overlay_open` (compositor: `_NET_WM_WINDOW_OPACITY`). */
    Window special_dimwin{};
};

/* BSP `sanitize_tree_for_clients` / `tree_count_visible_tiled_for_pred`: client on `wid` for this monitor (dock/float filtered by caller). */
[[nodiscard]] inline bool client_tree_member_on_normal_workspace(const Client* c, const Monitor* m, WorkspaceId wid) noexcept {
    return c && m && c->mon == m && client_on_normal_workspace(c, wid);
}

/* Workspace-aware membership check used by unified BSP paths (normal + special). */
[[nodiscard]] inline bool client_tree_member_on_workspace(const Client* c, const Monitor* m, const WorkspaceRef& ws) noexcept {
    if (!c || !m || c->mon != m || ws.is_unset())
        return false;
    if (ws.is_special())
        return c->workspace == ws;
    if (ws.is_normal())
        return client_tree_member_on_normal_workspace(c, m, ws.normal_id);
    return false;
}

/* Client assigned to `special:<tag>` (any monitor; persistence / cross-checks). */
[[nodiscard]] inline bool client_special_workspace_tag_equals(const Client* c, const std::string& tag) noexcept {
    return c && c->workspace.is_special() && c->workspace.special_tag == tag;
}

/* Scratchpad overlay visible for this client: open overlay + same monitor + tag matches `m->special_overlay_tag`. */
[[nodiscard]] inline bool client_on_open_special_overlay_tag(const Client* c, const Monitor* m) noexcept {
    return m && m->special_overlay_open && client_tree_member_on_workspace(c, m, WorkspaceRef::special(m->special_overlay_tag));
}

/* Layout arrange functions (`tree` in monitor/tree_arrange.cpp, `monocle` in geometry.cpp). */
void tree(Monitor* m);
void monocle(Monitor* m);
