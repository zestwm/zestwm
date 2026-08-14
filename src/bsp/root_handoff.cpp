/* BSP root handoff implementation.
 *
 * These helpers write detached BSP roots into monitor-owned slots after structural tree
 * operations. They do not decide tree topology; they only bridge resulting roots to
 * monitor/workspace ownership.
 */
#include "bsp/root_handoff.hpp"

#include "bsp/workspace_store.hpp"
#include "monitor/world_state.hpp"
#include "special_workspace_registry.hpp"

#include <utility>

namespace {
    [[nodiscard]] static inline bool is_detached_root_candidate(LayoutNode* node) noexcept {
        return node && !node->parent;
    }
} // namespace

BspRootHandoff::BspRootHandoff(Monitor& monitor) noexcept : monitor_(monitor) {}

void BspRootHandoff::handoff_special_root(const WorkspaceRef& workspace, std::unique_ptr<LayoutNode> root_candidate) const noexcept {
    if (!workspace.is_special() || !is_detached_root_candidate(root_candidate.get())) [[unlikely]]
        return;
    /* Accept any registered special tag. Do not require an existing non-null slot: split/insert
     * paths `take()` the root first, so the slot is empty until handoff writes the new root. */
    if (!special_workspace_registry_slot_by_tag(workspace.special_tag)) [[unlikely]]
        return;
    BspWorkspaceStore(monitor_).set(workspace, std::move(root_candidate));
}

void BspRootHandoff::handoff_normal_root(const WorkspaceRef& workspace, std::unique_ptr<LayoutNode> root_candidate) const noexcept {
    if (!workspace.is_normal() || !is_detached_root_candidate(root_candidate.get())) [[unlikely]]
        return;
    if (workspace.normal_id == monitor_.active_workspace_id)
        MonitorWorldState(monitor_).set_viewed_active(std::move(root_candidate));
    else
        BspWorkspaceStore(monitor_).set(workspace, std::move(root_candidate));
}
