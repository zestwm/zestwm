/* Monitor arrange-world orchestration implementation.
 *
 * This module centralizes policy-only decisions for one monitor arrange pass:
 * - top active owner selection,
 * - group anchor resolution for that owner,
 * - diagnostics for owner mismatches.
 *
 * It intentionally avoids geometry placement or BSP mutation.
 */
#include "monitor/arrange_world.hpp"

#include "bsp/workspace_store.hpp"
#include "bsp/tree_ops.hpp"
#include "log.hpp"

#include <cstdio>

namespace {
    /* Resolve monitor group-anchor candidate for one workspace snapshot.
 * Policy:
 * 1) Prefer selected grouped leaf when selection belongs to this workspace and has at least one member.
 * 2) Otherwise, pick first grouped leaf from workspace root subtree that has members. */
    LayoutNode* monitor_group_anchor_for_workspace_snapshot(Monitor* m, const WorkspaceRef& ws) {
        if (!m || ws.is_unset())
            return nullptr;
        LayoutNode* const ws_root   = BspWorkspaceStore(*m).read(ws);
        const bool        sel_on_ws = m->sel && m->sel->leaf && m->sel->leaf->type == NODE_GROUPED && m->sel->workspace == ws;
        if (sel_on_ws) {
            LayoutNode* const gl = m->sel->leaf;
            if ((gl->grouped.groupmode && gl->grouped.clients.size() > 0U) || (!gl->grouped.groupmode && gl->grouped.clients.size() > 1U))
                return gl;
        }
        return ws_root ? BspTreeOps::first_tab_anchor(ws_root) : nullptr;
    }
} // namespace

MonitorArrangeWorld::MonitorArrangeWorld(Monitor& monitor) noexcept : m_(monitor) {}

std::optional<TreeTabLaneOwner> MonitorArrangeWorld::top_active_arrange_owner() const noexcept {
    if (m_.special_overlay_open)
        return TreeTabLaneOwner{.ws = WorkspaceRef::special(m_.special_overlay_tag)};
    if (m_.tree_world_viewed.ws.is_normal())
        return TreeTabLaneOwner{.ws = m_.tree_world_viewed.ws};
    return std::nullopt;
}

TreeGroupAnchorReservation MonitorArrangeWorld::resolve_group_anchor() const {
    TreeGroupAnchorReservation            reservation{};
    const std::optional<TreeTabLaneOwner> top_owner = top_active_arrange_owner();
    if (!top_owner.has_value())
        return reservation;
    reservation.owner  = *top_owner;
    reservation.anchor = monitor_group_anchor_for_workspace_snapshot(&m_, top_owner->ws);
    return reservation;
}

int MonitorArrangeWorld::same_tab_owner(const std::optional<TreeTabLaneOwner>& lhs, const std::optional<TreeTabLaneOwner>& rhs) noexcept {
    if (lhs.has_value() != rhs.has_value())
        return 0;
    if (!lhs.has_value())
        return 1;
    return lhs->ws == rhs->ws;
}

void MonitorArrangeWorld::warn_tab_owner_mismatch(const TreeGroupAnchorReservation& group_anchor_reservation, const TreeTabLaneReservation& lane_reservation) {
    if (!group_anchor_reservation.owner.has_value() || !lane_reservation.owner.has_value())
        return;
    if (same_tab_owner(group_anchor_reservation.owner, lane_reservation.owner))
        return;

    char                msg[512];
    const WorkspaceRef& anchor_ws        = group_anchor_reservation.owner->ws;
    const WorkspaceRef& lane_ws          = lane_reservation.owner->ws;
    const char*         anchor_ws_kind   = anchor_ws.is_special() ? "special" : (anchor_ws.is_normal() ? "normal" : "unset");
    const char*         lane_ws_kind     = lane_ws.is_special() ? "special" : (lane_ws.is_normal() ? "normal" : "unset");
    const unsigned      anchor_normal_id = anchor_ws.is_normal() ? static_cast<unsigned>(anchor_ws.normal_id) : 0U;
    const unsigned      lane_normal_id   = lane_ws.is_normal() ? static_cast<unsigned>(lane_ws.normal_id) : 0U;
    const char*         anchor_tag       = anchor_ws.is_special() ? anchor_ws.special_tag.c_str() : "-";
    const char*         lane_tag         = lane_ws.is_special() ? lane_ws.special_tag.c_str() : "-";
    std::snprintf(msg, sizeof(msg), "warning: tab owner mismatch anchor_ws=%s normal=%u tag=%s lane_ws=%s normal=%u tag=%s", anchor_ws_kind, anchor_normal_id, anchor_tag,
                  lane_ws_kind, lane_normal_id, lane_tag);
    wm::log::warn_and_log(msg);
}
