/* BSP root handoff API.
 *
 * Role:
 * - Centralize root replacement when a local BSP operation creates a detached top-level node.
 * - Keep monitor/workspace ownership updates explicit and consistent across call sites.
 */
#pragma once

#include "types.hpp"

#include <memory>

class BspRootHandoff {
  public:
    explicit BspRootHandoff(Monitor& monitor) noexcept;

    /* Handoff detached root candidate to special-workspace storage. */
    void handoff_special_root(const WorkspaceRef& workspace, std::unique_ptr<LayoutNode> root_candidate) const noexcept;

    /* Handoff detached root candidate to normal-workspace ownership path. */
    void handoff_normal_root(const WorkspaceRef& workspace, std::unique_ptr<LayoutNode> root_candidate) const noexcept;

  private:
    Monitor& monitor_;
};
