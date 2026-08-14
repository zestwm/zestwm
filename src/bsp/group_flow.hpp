/* BSP group-flow API.
 *
 * Role:
 * - Encapsulate grouped-leaf attach and ungroup operations.
 * - Keep grouping policy, rollback, and root handoff behavior aligned in one module.
 */
#pragma once

#include "bsp/tree_ops.hpp"
#include "types.hpp"

#include <memory>

class BspGroupFlow {
  public:
    /* Result of grouped attach policy application. */
    struct AttachResult {
        bool                        attached{false};
        std::unique_ptr<LayoutNode> split_root_candidate{};
    };
    using SplitInsertFn = BspTreeOps::InsertSplitResult (*)(Monitor*, LayoutNode*, std::unique_ptr<LayoutNode>);

    explicit BspGroupFlow(Monitor& monitor) noexcept;

    AttachResult attach(Client* client, LayoutNode* leaf, bool layout_is_tree, bool insert_after_current, SplitInsertFn split_insert) const noexcept;

    void         detach(LayoutNode* leaf, BspTreeOps::SplitAxisFn split_axis, BspTreeOps::NewFirstFn new_first, BspTreeOps::RatioFn default_ratio) const noexcept;

  private:
    static bool try_slot_rejoin(LayoutNode* leaf, Client* client) noexcept;

    Monitor&    monitor_;
};
