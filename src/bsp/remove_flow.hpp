/* BSP remove-flow API.
 *
 * Role:
 * - Encapsulate structural collapse after grouped-leaf removals.
 * - Keep parent/grand rewiring and root handoff logic consistent.
 */
#pragma once

#include "types.hpp"

class BspRemoveFlow {
  public:
    explicit BspRemoveFlow(Monitor& monitor) noexcept;
    /* Collapse an empty grouped leaf and splice its sibling in place.
     *
     * Preconditions:
     * - `leaf` is a grouped node with zero clients.
     * - `leaf` has a split parent (cannot collapse root grouped node directly).
     *
     * Behavior:
     * - Rewires parent/grand links so sibling replaces the removed split branch.
     * - If removed split was root-level, delegates root ownership to `BspRootHandoff`
     *   for normal/special workspace root slot update.
     * - Releases detached grouped/split nodes after successful rewiring.
     *
     * Return:
     * - `true` when collapse was successfully applied.
     * - `false` when input/state validation fails or structural invariants are broken.
     */
    bool collapse_empty_leaf(LayoutNode* leaf, const WorkspaceRef& workspace) const noexcept;

  private:
    Monitor& monitor_;
};
