/* Monitor arrange-world orchestration API.
 *
 * Role:
 * - Encapsulate policy-level decisions used by one monitor arrange pass.
 * - Resolve "which workspace layer owns UI affordances" (group anchor / tab lane owner).
 * - Keep diagnostics (owner mismatch warnings) outside `zestwm.cpp`.
 *
 * Scope:
 * - This module does not place windows or mutate BSP topology.
 * - It consumes world snapshots already prepared on `Monitor` (`tree_world_viewed`/`tree_world_overlay`).
 *
 * Invariants:
 * - Owner decisions are derived only from current world-layer snapshots.
 * - Overlay layer has precedence over viewed layer when both are valid in the same pass.
 */
#pragma once

#include "types.hpp"

#include <optional>

struct TabLaneGeometry {
    int tx{0};
    int ty{0};
    int tw{0};
    int th{0};
    int valid{0};
};

/* Logical owner of the tab lane / anchor for one arrange pass. */
struct TreeTabLaneOwner {
    WorkspaceRef ws = WorkspaceRef::unset();
};

/* Tab lane reservation result from arrange pass (geometry + owner if reserved). */
struct TreeTabLaneReservation {
    TabLaneGeometry                 geometry{};
    std::optional<TreeTabLaneOwner> owner  = std::nullopt;
    LayoutNode*                     anchor = nullptr;
};

/* Group anchor reservation result from arrange pass (leaf + owner if resolved). */
struct TreeGroupAnchorReservation {
    LayoutNode*                     anchor = nullptr;
    std::optional<TreeTabLaneOwner> owner  = std::nullopt;
};

/* Aggregate output for one monitor arrange pass. */
struct TreeArrangePassResult {
    TreeGroupAnchorReservation group_anchor{};
    TreeTabLaneReservation     tab_lane{};
};

/* C++23 object wrapper for monitor arrange-world owner policy.
 * This class reads monitor world snapshots and exposes deterministic owner/anchor decisions. */
class MonitorArrangeWorld {
  public:
    explicit MonitorArrangeWorld(Monitor& monitor) noexcept;

    /* Return topmost active owner from current world snapshot (overlay first, then viewed). */
    [[nodiscard]] std::optional<TreeTabLaneOwner> top_active_arrange_owner() const noexcept;
    /* Resolve group anchor leaf for current top owner workspace. */
    [[nodiscard]] TreeGroupAnchorReservation resolve_group_anchor() const;
    /* Emit warning when lane owner and anchor owner diverge in the same pass. */
    static void warn_tab_owner_mismatch(const TreeGroupAnchorReservation& group_anchor_reservation, const TreeTabLaneReservation& lane_reservation);

  private:
    /* Ownership equality helper for optional owner descriptors. */
    static int same_tab_owner(const std::optional<TreeTabLaneOwner>& lhs, const std::optional<TreeTabLaneOwner>& rhs) noexcept;

    Monitor&   m_;
};
