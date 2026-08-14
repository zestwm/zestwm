/* Monitor world-state implementation for `MonitorWorldState` object API. */
#include "monitor/world_state.hpp"
#include "bsp/workspace_store.hpp"
#include "layout_tree.hpp"

#include <utility>

MonitorWorldState::MonitorWorldState(Monitor& monitor) noexcept : m_(monitor) {}

void MonitorWorldState::set_viewed(const WorkspaceRef& ws) noexcept {
    m_.tree_world_viewed.ws = ws;
}

void MonitorWorldState::set_viewed_active(std::unique_ptr<LayoutNode> root) noexcept {
    const WorkspaceRef viewed_ws = WorkspaceRef::normal(m_.active_workspace_id);
    BspWorkspaceStore(m_).set(viewed_ws, std::move(root));
    set_viewed(viewed_ws);
}

void MonitorWorldState::set_overlay(const WorkspaceRef& ws) noexcept {
    m_.tree_world_overlay.ws = ws;
}

void MonitorWorldState::clear_overlay() noexcept {
    set_overlay(WorkspaceRef::unset());
}

LayoutNode* MonitorWorldState::current_viewed_root() noexcept {
    if (LayoutNode* root = BspWorkspaceStore(m_).read(m_.tree_world_viewed.ws))
        return root;
    sync_viewed_from_active_workspace();
    return BspWorkspaceStore(m_).read(m_.tree_world_viewed.ws);
}

void MonitorWorldState::sync_viewed_from_active_workspace() {
    const WorkspaceRef           viewed_ws   = WorkspaceRef::normal(m_.active_workspace_id);
    std::unique_ptr<LayoutNode>& viewed_slot = BspWorkspaceStore(m_).owned_slot(viewed_ws);
    if (!viewed_slot)
        viewed_slot = lt_new_grouped();
    set_viewed(viewed_ws);
}
