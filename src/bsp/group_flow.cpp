/* BSP group-flow implementation.
 *
 * This module handles grouped leaf attach/ungroup policy and relies on BspTreeOps for
 * pure local tree operations and BspRootHandoff for monitor/workspace root ownership.
 */
#include "bsp/group_flow.hpp"
#include "bsp/root_handoff.hpp"
#include "bsp/tree_ops.hpp"
#include "bsp/workspace_store.hpp"

#include <algorithm>
#include <utility>

BspGroupFlow::BspGroupFlow(Monitor& monitor) noexcept : monitor_(monitor) {}

bool BspGroupFlow::try_slot_rejoin(LayoutNode* leaf, Client* client) noexcept {
    if (!leaf || leaf->type != NODE_GROUPED || leaf->grouped.clients.size() == 0U || !client)
        return false;
    if (client->rejoin_group_slot_plus_one == 0U)
        return false;
    const size_t want = static_cast<size_t>(client->rejoin_group_slot_plus_one - 1U);
    const size_t pos  = std::min(want, leaf->grouped.clients.size());
    if (!lt_grouped_add_at(leaf, client, pos))
        return false;
    client->leaf = leaf;
    return true;
}

BspGroupFlow::AttachResult BspGroupFlow::attach(Client* client, LayoutNode* leaf, bool layout_is_tree, bool insert_after_current, SplitInsertFn split_insert) const noexcept {
    if (!client || !leaf || leaf->type != NODE_GROUPED) [[unlikely]]
        return {};

    if (leaf->grouped.clients.size() == 0U) {
        const bool ok = BspTreeOps::add_client_to_grouped_leaf(leaf, client);
        return {.attached = ok, .split_root_candidate = {}};
    }
    if (try_slot_rejoin(leaf, client))
        return {.attached = true, .split_root_candidate = {}};
    if (layout_is_tree && leaf->grouped.groupmode) {
        const bool ok = BspTreeOps::add_client_to_grouped_leaf_insert(leaf, client, insert_after_current);
        return {.attached = ok, .split_root_candidate = {}};
    }

    auto newleaf = BspTreeOps::make_grouped_leaf_with_client(client);
    if (!newleaf || !split_insert) [[unlikely]]
        return {};
    LayoutNode* const             newleaf_obs = newleaf.get();

    BspTreeOps::InsertSplitResult inserted = split_insert(&monitor_, leaf, std::move(newleaf));
    if (!inserted.ok) [[unlikely]]
        return {};
    client->leaf = newleaf_obs;
    return {.attached = true, .split_root_candidate = std::move(inserted.new_root)};
}

void BspGroupFlow::detach(LayoutNode* leaf, BspTreeOps::SplitAxisFn split_axis, BspTreeOps::NewFirstFn new_first, BspTreeOps::RatioFn default_ratio) const noexcept {
    if (!leaf || leaf->type != NODE_GROUPED || leaf->grouped.clients.size() < 2)
        return;
    Client* const base = leaf->grouped.clients[0];
    if (!base || base->mon != &monitor_ || !split_axis || !new_first || !default_ratio)
        return;

    const WorkspaceRef workspace    = base->workspace;
    const bool         base_special = workspace.is_special();
    BspRootHandoff     root_handoff(monitor_);

    while (leaf->grouped.clients.size() > 1U) {
        const std::size_t last_idx = leaf->grouped.clients.size() - 1U;
        Client* const     c        = leaf->grouped.clients[last_idx];
        if (!c || !lt_grouped_remove(leaf, c))
            break;
        c->leaf                      = nullptr;
        const auto rollback_reinsert = [&]() noexcept { [[maybe_unused]] const bool restored = BspTreeOps::add_client_to_grouped_leaf(leaf, c); };

        auto       newleaf = BspTreeOps::make_grouped_leaf_with_client(c);
        if (!newleaf) {
            rollback_reinsert();
            break;
        }
        LayoutNode* const           newleaf_obs = newleaf.get();

        std::unique_ptr<LayoutNode> root_take;
        if (!leaf->parent)
            root_take = BspWorkspaceStore(monitor_).take(workspace);

        Client* const                 ref      = lt_grouped_active(leaf);
        BspTreeOps::InsertSplitResult inserted = BspTreeOps::insert_split_with_policy(leaf, std::move(newleaf), std::move(root_take), ref, split_axis, new_first, default_ratio);
        if (!inserted.ok) {
            rollback_reinsert();
            break;
        }

        if (inserted.new_root) {
            if (base_special)
                root_handoff.handoff_special_root(workspace, std::move(inserted.new_root));
            else
                root_handoff.handoff_normal_root(workspace, std::move(inserted.new_root));
        }
        c->leaf = newleaf_obs;
    }
}
