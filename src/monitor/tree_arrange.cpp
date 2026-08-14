/* BSP tree layout geometry and arrange pass implementation. */
#include "monitor/tree_arrange.hpp"

#include "bsp/add_flow.hpp"
#include "bsp/workspace_store.hpp"
#include "client/client_lifecycle.hpp"
#include "config.hpp"
#include "draw/bar.hpp"
#include "geometry.hpp"
#include "intern.hpp"
#include "layout_tree.hpp"
#include "monitor/arrange_world.hpp"
#include "monitor/world_state.hpp"
#include "wm_state.hpp"
#include "x11/wm_window.hpp"

#include <algorithm>
#include <memory>
#include <unordered_set>
#include <utility>
#include <vector>

static void arrangetreenode(Monitor* m, LayoutNode* node, int x, int y, int w, int h, int inner_gap, TabLaneGeometry* lane_geometry, LayoutNode** lane_anchor,
                            LayoutNode* preferred_lane_anchor, int collect_groupbars) {
    size_t    i;
    Client*   c;
    int       w1, h1;
    int       gap;
    int       showtabs;
    int       gbthick;
    SplitAxis axis;

    if (!node)
        return;
    if (node->type == NODE_SPLIT) {
        axis = node->split.axis;
        if (!g_config.dwindle_preserve_split)
            axis = (static_cast<float>(w) * g_config.dwindle_split_width_multiplier >= static_cast<float>(h)) ? SPLIT_VERTICAL : SPLIT_HORIZONTAL;
        gap = inner_gap;
        if (axis == SPLIT_VERTICAL) {
            if (w <= gap)
                gap = 0;
            w1 = static_cast<int>((static_cast<float>(w - gap)) * node->split.ratio);
            arrangetreenode(m, node->split.first.get(), x, y, w1, h, inner_gap, lane_geometry, lane_anchor, preferred_lane_anchor, collect_groupbars);
            arrangetreenode(m, node->split.second.get(), x + w1 + gap, y, w - w1 - gap, h, inner_gap, lane_geometry, lane_anchor, preferred_lane_anchor, collect_groupbars);
        } else {
            if (h <= gap)
                gap = 0;
            h1 = static_cast<int>((static_cast<float>(h - gap)) * node->split.ratio);
            arrangetreenode(m, node->split.first.get(), x, y, w, h1, inner_gap, lane_geometry, lane_anchor, preferred_lane_anchor, collect_groupbars);
            arrangetreenode(m, node->split.second.get(), x, y + h1 + gap, w, h - h1 - gap, inner_gap, lane_geometry, lane_anchor, preferred_lane_anchor, collect_groupbars);
        }
        return;
    }
    if (node->grouped.clients.size() == 0)
        return;
    showtabs = g_config.groupbar_enabled && ((node->grouped.groupmode && node->grouped.clients.size() > 0U) || (!node->grouped.groupmode && node->grouped.clients.size() > 1U));
    if (showtabs) {
        gbthick = groupbar_thickness();
        GroupbarSlot slot{};
        slot.anchor = node;
        slot.ntabs  = countvisibleintab(node);
        if (g_config.groupbar_position == 1) {
            slot.x = x;
            slot.y = y;
            slot.w = gbthick;
            slot.h = h;
            x += gbthick;
            w -= gbthick;
        } else if (g_config.groupbar_position == 2) {
            slot.x = x + w - gbthick;
            slot.y = y;
            slot.w = gbthick;
            slot.h = h;
            w -= gbthick;
        } else if (g_config.groupbar_position == 3) {
            slot.x = x;
            slot.y = y + h - gbthick;
            slot.w = w;
            slot.h = gbthick;
            h -= gbthick;
        } else {
            slot.x = x;
            slot.y = y;
            slot.w = w;
            slot.h = gbthick;
            y += gbthick;
            h -= gbthick;
        }
        if (collect_groupbars && slot.ntabs > 0 && slot.w > 0 && slot.h > 0)
            m->groupbars.push_back(slot);

        if (lane_geometry && !lane_geometry->valid && (!preferred_lane_anchor || node == preferred_lane_anchor)) {
            if (lane_anchor)
                *lane_anchor = node;
            lane_geometry->tx    = slot.x;
            lane_geometry->tw    = slot.w;
            lane_geometry->ty    = slot.y;
            lane_geometry->th    = slot.h;
            lane_geometry->valid = 1;
        }
    }
    for (i = 0; i < node->grouped.clients.size(); i++) {
        c = node->grouped.clients[i];
        if (!c || !client_is_visible(c))
            continue;
        if (c->isfloating)
            continue;
        if (i == node->grouped.active || !node->grouped.groupmode)
            resize(c, x, y, w - 2 * c->bw, h - 2 * c->bw, 0);
        else {
            const int hide_x = client_outer_width(c) * -2;
            c->oldx          = c->x;
            c->x             = hide_x;
            wm::x11::move_window(c->win, c->x, c->y);
        }
    }
}

int countvisibleintab(LayoutNode* leaf) {
    int     i, n = 0;
    Client* c;

    if (!leaf || leaf->type != NODE_GROUPED)
        return 0;
    for (i = 0; i < static_cast<int>(leaf->grouped.clients.size()); i++) {
        c = leaf->grouped.clients[i];
        if (c && client_is_visible(c))
            n++;
    }
    return n;
}

/* True when target grouped/split node pointer belongs to this subtree. */
static int tree_contains_node(LayoutNode* root, LayoutNode* target) {
    if (!root || !target)
        return 0;
    if (root == target)
        return 1;
    if (root->type == NODE_GROUPED)
        return 0;
    return tree_contains_node(root->split.first.get(), target) || tree_contains_node(root->split.second.get(), target);
}

/* Non-dock tiled clients matching `pred`; stops counting past 2 (sole vs multi). */
template <typename Pred>
static int tree_count_visible_tiled_for_pred(Monitor* m, Client** sole_out, Pred&& pred) {
    int     n    = 0;
    Client* sole = nullptr;
    if (sole_out)
        *sole_out = nullptr;
    if (!m)
        return 0;
    for (Client* x : m->clients) {
        if (x->isdock || !pred(x))
            continue;
        if (x->isfloating)
            continue;
        ++n;
        sole = x;
        if (n > 1)
            break;
    }
    if (sole_out)
        *sole_out = sole;
    return n;
}

static void tree_fill_inner_workarea(Monitor* m, int* x, int* y, int* w, int* h, int* inner_g) {
    unsigned outer_g = g_config.gaps_in;
    unsigned inner_u = g_config.gaps_in;
    if (!m || !x || !y || !w || !h || !inner_g)
        return;
    tiling_gaps_for_monitor_workspace(m, &outer_g, &inner_u);
    *x = m->wx + static_cast<int>(outer_g);
    *y = m->wy + static_cast<int>(outer_g);
    *w = m->ww - 2 * static_cast<int>(outer_g);
    *h = m->wh - 2 * static_cast<int>(outer_g);
    if (*w < 1)
        *w = 1;
    if (*h < 1)
        *h = 1;
    *inner_g = static_cast<int>(inner_u);
}

template <typename Pred>
static void tree_sanitize_compact_for_pred(Monitor* m, std::unique_ptr<LayoutNode>& root, Pred&& pred) {
    if (!m || !root)
        return;
    std::unordered_set<Window> seen;
    sanitize_tree_for_clients(root.get(), m, std::forward<Pred>(pred), seen);
    root = bsp_compact_tree(std::move(root));
}

static void tree_sanitize_compact_workspace_ref(std::unique_ptr<LayoutNode>& root, Monitor* m, const WorkspaceRef& ws) {
    if (!m || ws.is_unset() || !root)
        return;
    tree_sanitize_compact_for_pred(m, root, [&](Client* c) { return client_tree_member_on_workspace(c, m, ws); });
}

/* Single tiled client → one grouped leaf; skips redundant rebuild when already minimal. */
static LayoutNode* tree_regroup_sole_tiled_grouped_root(Monitor* m, std::unique_ptr<LayoutNode>& root_slot, Client* sole_client, int tiled_count, int sync_viewed_workspace_slot) {
    if (tiled_count != 1 || !sole_client || !m)
        return root_slot.get();
    LayoutNode* const cur            = root_slot.get();
    const int         already_single = cur && cur->type == NODE_GROUPED && cur->grouped.clients.size() == 1U && cur->grouped.clients[0] == sole_client && sole_client->leaf == cur;
    if (already_single)
        return root_slot.get();

    const int preserve_groupmode = (sole_client->leaf && sole_client->leaf->type == NODE_GROUPED && sole_client->leaf->grouped.groupmode) ? 1 : 0;
    auto      single_root        = lt_new_grouped();
    if (single_root && lt_grouped_add(single_root.get(), sole_client)) {
        single_root->grouped.groupmode = preserve_groupmode;
        sole_client->leaf              = single_root.get();
        root_slot                      = std::move(single_root);
        if (sync_viewed_workspace_slot)
            MonitorWorldState(*m).set_viewed(WorkspaceRef::normal(m->active_workspace_id));
        return root_slot.get();
    }
    return root_slot.get();
}

template <typename CountPred>
static LayoutNode* tree_sanitize_count_regroup_for_workspace_ref(Monitor* m, std::unique_ptr<LayoutNode>& root_slot, const WorkspaceRef& ws, int sync_viewed_workspace_slot,
                                                                 CountPred&& count_pred) {
    tree_sanitize_compact_workspace_ref(root_slot, m, ws);
    Client*   sole_client     = nullptr;
    const int visible_clients = tree_count_visible_tiled_for_pred(m, &sole_client, std::forward<CountPred>(count_pred));
    return tree_regroup_sole_tiled_grouped_root(m, root_slot, sole_client, visible_clients, sync_viewed_workspace_slot);
}

static LayoutNode* tree_prepare_workspace_bsp_for_arrange(Monitor* m, const WorkspaceRef& ws, int sync_viewed_workspace_slot) {
    if (!m || ws.is_unset())
        return nullptr;
    std::unique_ptr<LayoutNode>& slot = BspWorkspaceStore(*m).owned_slot(ws);
    if (!slot)
        return nullptr;
    return tree_sanitize_count_regroup_for_workspace_ref(m, slot, ws, sync_viewed_workspace_slot, [&](Client* x) { return client_tree_member_on_workspace(x, m, ws); });
}

static void tree_refresh_world_roots(Monitor* m) {
    if (!m)
        return;
    const WorkspaceRef viewed_ws = WorkspaceRef::normal(m->active_workspace_id);
    if (!tree_prepare_workspace_bsp_for_arrange(m, viewed_ws, 1)) {
        BspWorkspaceStore(*m).set(viewed_ws, lt_new_grouped());
    }
    MonitorWorldState(*m).set_viewed(viewed_ws);

    MonitorWorldState(*m).clear_overlay();
    if (!m->special_overlay_open)
        return;
    const WorkspaceRef overlay_ws = WorkspaceRef::special(m->special_overlay_tag);
    tree_prepare_workspace_bsp_for_arrange(m, overlay_ws, 0);
    MonitorWorldState(*m).set_overlay(overlay_ws);
}

static void tree_clear_tab_lane_geometry(Monitor* m) {
    if (!m)
        return;
    m->ty = -th;
    m->tx = m->tw = 0;
    m->th         = 0;
}

static void tree_apply_tab_lane_geometry(Monitor* m, const TabLaneGeometry& lane_geometry) {
    if (!m)
        return;
    if (!lane_geometry.valid) {
        tree_clear_tab_lane_geometry(m);
        return;
    }
    m->tx = lane_geometry.tx;
    m->ty = lane_geometry.ty;
    m->tw = lane_geometry.tw;
    m->th = lane_geometry.th;
}

/* Inner tiling rectangle: compositing policy is a fixed stack of BSP layers in the same work area — viewed desktop
 * first, then (if open) the special workspace overlay. Each layer receives the same inner rect from
 * `tree_fill_inner_workarea`; stacking order of X windows is handled separately in `restack`. */
static TreeTabLaneReservation tree_arrange_inner_tile_stack(Monitor* m, const TreeGroupAnchorReservation& group_anchor_reservation) {
    int                    x, y, w, h, inner_g;
    TreeTabLaneReservation lane_reservation{};
    const WorkspaceRef     viewed_ws = (m ? m->tree_world_viewed.ws : WorkspaceRef::unset());
    /* Overlay open with zero clients has no BSP root; still suppress base-workspace groupbars. */
    const int overlay_active = (m && m->special_overlay_open) ? 1 : 0;
    tree_fill_inner_workarea(m, &x, &y, &w, &h, &inner_g);
    LayoutNode* const viewed_root = m ? BspWorkspaceStore(*m).read(viewed_ws) : nullptr;
    if (m && viewed_root) {
        TabLaneGeometry  viewed_lane{};
        LayoutNode*      viewed_anchor           = nullptr;
        TabLaneGeometry* lane_ptr                = overlay_active ? nullptr : &viewed_lane;
        LayoutNode*      viewed_preferred_anchor = nullptr;
        if (group_anchor_reservation.owner.has_value() && group_anchor_reservation.owner->ws == viewed_ws)
            viewed_preferred_anchor = group_anchor_reservation.anchor;
        if (viewed_preferred_anchor && !tree_contains_node(viewed_root, viewed_preferred_anchor))
            viewed_preferred_anchor = nullptr;
        arrangetreenode(m, viewed_root, x, y, w, h, inner_g, lane_ptr, overlay_active ? nullptr : &viewed_anchor, viewed_preferred_anchor, !overlay_active);
        if (!overlay_active && viewed_lane.valid) {
            lane_reservation.geometry = viewed_lane;
            lane_reservation.owner    = TreeTabLaneOwner{.ws = viewed_ws};
            lane_reservation.anchor   = viewed_anchor;
        }
    }
    LayoutNode* const overlay_root = (m && m->tree_world_overlay.ws.is_special()) ? BspWorkspaceStore(*m).read(m->tree_world_overlay.ws) : nullptr;
    if (m && overlay_root && m->tree_world_overlay.ws.is_special()) {
        TabLaneGeometry overlay_lane{};
        LayoutNode*     overlay_anchor           = nullptr;
        LayoutNode*     overlay_preferred_anchor = nullptr;
        if (group_anchor_reservation.owner.has_value() && group_anchor_reservation.owner->ws == m->tree_world_overlay.ws)
            overlay_preferred_anchor = group_anchor_reservation.anchor;
        if (overlay_preferred_anchor && !tree_contains_node(overlay_root, overlay_preferred_anchor))
            overlay_preferred_anchor = nullptr;
        arrangetreenode(m, overlay_root, x, y, w, h, inner_g, &overlay_lane, &overlay_anchor, overlay_preferred_anchor, 1);
        if (overlay_lane.valid) {
            lane_reservation.geometry = overlay_lane;
            lane_reservation.owner    = TreeTabLaneOwner{.ws = m->tree_world_overlay.ws};
            lane_reservation.anchor   = overlay_anchor;
        }
    }
    return lane_reservation;
}

/* Compose one arrange pass result from logical roots (anchor owner + tab lane reservation). */
static TreeArrangePassResult tree_compose_arrange_pass(Monitor* m) {
    TreeArrangePassResult out{};
    if (m)
        out.group_anchor = MonitorArrangeWorld(*m).resolve_group_anchor();
    out.tab_lane = tree_arrange_inner_tile_stack(m, out.group_anchor);
    return out;
}

void tree(Monitor* m) {
    /* Tree layout is workspace-id based; keep compatibility window properties synchronized. */
    for (Client* list_c : m->clients) {
        if (list_c->isdock)
            continue;
        sync_client_workspace_props(list_c);
    }

    tree_refresh_world_roots(m);
    std::vector<GroupbarSlot> previous_groupbars = std::move(m->groupbars);
    m->groupbars.clear();
    const TreeArrangePassResult arrange_pass = tree_compose_arrange_pass(m);
    const std::size_t           carry        = std::min(m->groupbars.size(), previous_groupbars.size());
    for (std::size_t i = 0; i < carry; ++i)
        m->groupbars[i].win = previous_groupbars[i].win;
    for (std::size_t i = carry; i < previous_groupbars.size(); ++i) {
        if (!previous_groupbars[i].win)
            continue;
        wm::x11::unmap_window(previous_groupbars[i].win);
        wm::x11::destroy_window(previous_groupbars[i].win);
    }
    m->group_anchor = arrange_pass.tab_lane.anchor ? arrange_pass.tab_lane.anchor : arrange_pass.group_anchor.anchor;
    MonitorArrangeWorld::warn_tab_owner_mismatch(arrange_pass.group_anchor, arrange_pass.tab_lane);
    if (arrange_pass.tab_lane.owner.has_value()) {
        /* Tab lane owner is explicit: topmost arranged layer that reserved lane geometry wins. */
        tree_apply_tab_lane_geometry(m, arrange_pass.tab_lane.geometry);
    } else {
        tree_clear_tab_lane_geometry(m);
    }
    updategroupbarwin();
    arrange_docks(m);
    drawtab(m);
}
