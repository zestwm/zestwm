/* BSP remove-flow implementation.
 *
 * This module only handles structural collapse after a grouped leaf becomes empty.
 * Rejoin-hint policy remains managed by caller orchestration.
 */
#include "bsp/remove_flow.hpp"
#include "bsp/root_handoff.hpp"
#include "layout_tree.hpp"

#include <utility>

BspRemoveFlow::BspRemoveFlow(Monitor& monitor) noexcept : monitor_(monitor) {}

/* Collapse one empty grouped leaf from BSP topology. */
bool BspRemoveFlow::collapse_empty_leaf(LayoutNode* leaf, const WorkspaceRef& workspace) const noexcept {
    if (!leaf || leaf->type != NODE_GROUPED || leaf->grouped.clients.size() > 0 || !leaf->parent) [[unlikely]]
        return false;
    LayoutNode* const parent = leaf->parent;
    if (parent->type != NODE_SPLIT) [[unlikely]]
        return false;

    const bool                  leaf_was_first = parent->split.first.get() == leaf;
    std::unique_ptr<LayoutNode> leaf_owned     = leaf_was_first ? std::move(parent->split.first) : std::move(parent->split.second);
    std::unique_ptr<LayoutNode> sibling        = leaf_was_first ? std::move(parent->split.second) : std::move(parent->split.first);
    if (!sibling || sibling.get() == leaf) [[unlikely]]
        return false;

    LayoutNode* const grand = parent->parent;
    if (grand && grand->type == NODE_SPLIT) {
        const bool                  parent_was_first = grand->split.first.get() == parent;
        std::unique_ptr<LayoutNode> parent_owned     = parent_was_first ? std::move(grand->split.first) : std::move(grand->split.second);
        sibling->parent                              = grand;
        if (parent_was_first)
            lt_attach_first(grand, std::move(sibling));
        else
            lt_attach_second(grand, std::move(sibling));
        /* parent_owned + leaf_owned destroy empty shells. */
        static_cast<void>(parent_owned);
        static_cast<void>(leaf_owned);
    } else {
        sibling->parent = nullptr;
        BspRootHandoff handoff(monitor_);
        if (workspace.is_special())
            handoff.handoff_special_root(workspace, std::move(sibling));
        else
            handoff.handoff_normal_root(workspace, std::move(sibling));
        /* parent still in store until handoff set replaces it; leaf_owned destroys leaf.
         * parent unique_ptr is still in the map — handoff set destroys parent (children already null). */
        static_cast<void>(leaf_owned);
    }
    return true;
}
