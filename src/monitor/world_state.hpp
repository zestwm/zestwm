/* Monitor world-state API.
 *
 * Role:
 * - Encapsulate canonical monitor world snapshot operations (`viewed` + `overlay` layers).
 * - Keep which WorkspaceRef is viewed/overlay; BSP roots live only in BspWorkspaceStore.
 *
 * Scope:
 * - This module does not run arrange passes and does not mutate BSP topology internals.
 *
 * Invariants:
 * - World layers store WorkspaceRef only; roots are resolved from the owned map.
 */
#pragma once

#include "types.hpp"

#include <memory>

class MonitorWorldState {
  public:
    explicit MonitorWorldState(Monitor& monitor) noexcept;

    /* Store viewed workspace identity (root resolved from map). */
    void set_viewed(const WorkspaceRef& ws) noexcept;
    /* Move root into active viewed workspace store and mark viewed ws. */
    void set_viewed_active(std::unique_ptr<LayoutNode> root) noexcept;
    /* Store overlay workspace identity (root from map via read). */
    void set_overlay(const WorkspaceRef& ws) noexcept;
    /* Clear overlay snapshot when no special layer is active. */
    void clear_overlay() noexcept;
    /* Return viewed root; lazily resync from active workspace when missing. */
    LayoutNode* current_viewed_root() noexcept;
    /* Rebuild viewed ws from active normal workspace; materialize grouped root when missing. */
    void sync_viewed_from_active_workspace();

  private:
    Monitor& m_;
};
