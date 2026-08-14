/* BSP workspace-store implementation for monitor workspace roots.
 *
 * This module is the canonical storage facade for monitor workspace BSP roots.
 *
 * Responsibilities:
 * - validate workspace references at the storage boundary,
 * - expose explicit read/write/query operations over `WorkspaceRef -> unique_ptr<LayoutNode>`,
 * - keep slot materialization policy centralized for both normal and special workspaces.
 *
 * Non-goals:
 * - no topology mutation,
 * - no arrange policy,
 * - no monitor world-layer orchestration.
 */
#include "bsp/workspace_store.hpp"
#include "layout_tree.hpp"
#include "monitor/world_state.hpp"
#include "special_workspace_registry.hpp"
#include "state/runtime_authority.hpp"
#include "workspace_ref.hpp"

#include <string>
#include <utility>

BspWorkspaceStore::BspWorkspaceStore(Monitor& monitor) noexcept : m_(monitor) {}

std::unique_ptr<LayoutNode>& BspWorkspaceStore::owned_slot(const WorkspaceRef& ws) {
    if (ws.is_unset()) {
        static std::unique_ptr<LayoutNode> unset_sink;
        return unset_sink;
    }
    if (ws.is_special() && !special_workspace_registry_ensure_tag(ws.special_tag)) {
        static std::unique_ptr<LayoutNode> special_cap_sink;
        return special_cap_sink;
    }
    if (ws.is_normal() && ws.normal_id < kWorkspaceIdMin) {
        static std::unique_ptr<LayoutNode> invalid_sink;
        return invalid_sink;
    }
    return m_.workspace_bsp_roots_by_ref[ws];
}

LayoutNode* BspWorkspaceStore::read(const WorkspaceRef& ws) const {
    if (ws.is_unset())
        return nullptr;
    if (ws.is_special() && !special_workspace_registry_slot_by_tag(ws.special_tag))
        return nullptr;
    if (ws.is_normal() && ws.normal_id < kWorkspaceIdMin)
        return nullptr;
    const auto it = m_.workspace_bsp_roots_by_ref.find(ws);
    return (it == m_.workspace_bsp_roots_by_ref.end()) ? nullptr : it->second.get();
}

bool BspWorkspaceStore::has_special_root_slot(std::string_view tag) const {
    if (!special_workspace_registry_slot_by_tag(tag))
        return false;
    return has_root_slot(WorkspaceRef::special(std::string(tag)));
}

void BspWorkspaceStore::set(const WorkspaceRef& ws, std::unique_ptr<LayoutNode> root) {
    owned_slot(ws) = std::move(root);
}

std::unique_ptr<LayoutNode> BspWorkspaceStore::take(const WorkspaceRef& ws) {
    return std::move(owned_slot(ws));
}

LayoutNode* BspWorkspaceStore::get_or_create_normal(WorkspaceId id) {
    if (id < kWorkspaceIdMin) [[unlikely]]
        return nullptr;
    const WorkspaceRef           ws = WorkspaceRef::normal(id);
    std::unique_ptr<LayoutNode>& r  = owned_slot(ws);
    if (!r)
        r = lt_new_grouped();
    return r.get();
}

bool BspWorkspaceStore::has_root_slot(const WorkspaceRef& ws) const {
    const auto it = m_.workspace_bsp_roots_by_ref.find(ws);
    return it != m_.workspace_bsp_roots_by_ref.end() && it->second != nullptr;
}

void bsp_clear_workspace_tree_state(WorkspaceId id) {
    if (id < kWorkspaceIdMin)
        return;
    const WorkspaceRef ws = WorkspaceRef::normal(id);
    for (Monitor* mon : wm::state::all_monitors()) {
        BspWorkspaceStore store(*mon);
        if (!store.read(ws))
            continue;
        store.set(ws, nullptr);
        if (mon->active_workspace_id == id)
            MonitorWorldState(*mon).sync_viewed_from_active_workspace();
    }
}
