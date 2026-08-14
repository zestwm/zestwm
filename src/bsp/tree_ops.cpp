/* BSP tree-ops implementation.
 *
 * This module centralizes low-risk BSP primitives used by higher-level monitor/workspace
 * orchestration code. Helpers here can:
 * - traverse node subtrees,
 * - compute insertion policy from explicit runtime inputs,
 * - rewire local split/parent links.
 *
 * They do not read global runtime toggles directly and do not mutate monitor-level ownership.
 */
#include "bsp/tree_ops.hpp"
#include "direction_keys.hpp"
#include "layout_tree.hpp"

#include <utility>

/* Parse preselect direction payload once, then reuse typed key downstream.
 *
 * Boundary rationale:
 * - Callers pass legacy int payloads from input/config paths.
 * - Internal helpers consume only typed `DirectionKey` to keep policy logic explicit.
 */
[[nodiscard]] static inline std::optional<DirectionKey> parse_preselect_direction(int preselect_dir) noexcept {
    return direction_key_from_int(preselect_dir);
}

/* Check whether grouped node can act as tab anchor.
 *
 * Eligible when:
 * - grouped-tab mode is active and at least one client exists, or
 * - grouped-tab mode is disabled and split-like tab lane has at least two clients.
 */
[[nodiscard]] static inline bool is_valid_tab_anchor(const LayoutNode* node) noexcept {
    const auto& g = node->grouped;
    return (g.groupmode && g.clients.size() > 0U) || (!g.groupmode && g.clients.size() > 1U);
}

/* Depth-first (left-first) search for the first grouped node that still contains clients. */
LayoutNode* BspTreeOps::first_grouped(LayoutNode* node) noexcept {
    if (!node) [[unlikely]]
        return nullptr;
    if (node->type == NODE_GROUPED)
        return node->grouped.clients.size() ? node : nullptr;
    if (LayoutNode* left = first_grouped(node->split.first.get()); left)
        return left;
    return first_grouped(node->split.second.get());
}

/* Follow first-child edges until grouped leaf (or null). */
LayoutNode* BspTreeOps::leftmost_grouped_leaf(LayoutNode* node) noexcept {
    while (node && node->type == NODE_SPLIT)
        node = node->split.first.get();
    return (node && node->type == NODE_GROUPED) ? node : nullptr;
}

/* Depth-first (left-first) search for the first grouped node eligible as tab-lane anchor. */
LayoutNode* BspTreeOps::first_tab_anchor(LayoutNode* node) noexcept {
    if (!node) [[unlikely]]
        return nullptr;
    if (node->type == NODE_GROUPED)
        return is_valid_tab_anchor(node) ? node : nullptr;
    if (LayoutNode* left = first_tab_anchor(node->split.first.get()); left)
        return left;
    return first_tab_anchor(node->split.second.get());
}

/* Resolve insertion axis from preselect direction or grouped-leaf geometry fallback.
 *
 * Decision order:
 * 1) typed preselect direction (`l/r` -> vertical, `u/d/t/b` -> horizontal),
 * 2) explicit fallback client if it belongs to current leaf,
 * 3) grouped active client (with active-index clamp),
 * 4) deterministic vertical default when no reference exists.
 */
SplitAxis BspTreeOps::infer_insert_split_axis(LayoutNode* leaf, Client* fallback, int preselect_dir) noexcept {
    if (const auto dir = parse_preselect_direction(preselect_dir); dir.has_value()) {
        if (direction_is_vertical_split_hint(*dir))
            return SPLIT_VERTICAL;
        if (direction_is_horizontal_split_hint(*dir))
            return SPLIT_HORIZONTAL;
    }

    Client* ref = nullptr;
    if (fallback && fallback->leaf == leaf)
        ref = fallback;
    else
        ref = lt_grouped_active(leaf);
    if (!ref && leaf && leaf->type == NODE_GROUPED && leaf->grouped.clients.size() > 0U) {
        size_t ai = leaf->grouped.active;
        if (ai >= leaf->grouped.clients.size())
            ai = leaf->grouped.clients.size() - 1U;
        ref = leaf->grouped.clients[ai];
    }
    if (!ref)
        return SPLIT_VERTICAL;
    return ref->w >= ref->h ? SPLIT_VERTICAL : SPLIT_HORIZONTAL;
}

/* Resolve split child order from preselect, force policy, pointer bias, and grouped fallback.
 *
 * Return semantics:
 * - `1` => place new node as first child.
 * - `0` => place new node as second child.
 *
 * The function is intentionally side-effect-free: caller gathers pointer position
 * and force policy, this helper only evaluates deterministic precedence.
 */
int BspTreeOps::compute_insert_new_first(LayoutNode* leaf, SplitAxis axis, Client* fallback, int preselect_dir, int force_split,
                                         std::optional<std::pair<int, int>> pointer_pos) noexcept {
    if (const auto dir = parse_preselect_direction(preselect_dir); dir.has_value()) {
        if (const auto preferred = direction_prefers_new_first(*dir); preferred.has_value())
            return *preferred ? 1 : 0;
    }

    if (force_split == 1)
        return 1;
    if (force_split == 2)
        return 0;
    if (pointer_pos.has_value() && fallback) {
        const auto [px, py] = *pointer_pos;
        if (axis == SPLIT_VERTICAL) {
            const int center_x = fallback->x + (fallback->w >> 1);
            return px < center_x;
        }
        const int center_y = fallback->y + (fallback->h >> 1);
        return py < center_y;
    }
    if (leaf && leaf->type == NODE_GROUPED && leaf->grouped.active == 0)
        return 1;
    return 0;
}

/* Insert a split above `leaf`; steal leaf from parent or take `root_take` when leaf is root. */
[[nodiscard]] BspTreeOps::InsertSplitResult BspTreeOps::insert_split(LayoutNode* leaf, std::unique_ptr<LayoutNode> newleaf, std::unique_ptr<LayoutNode> root_take, SplitAxis axis,
                                                                     float ratio, int new_first) noexcept {
    if (!leaf || !newleaf) [[unlikely]]
        return {};

    LayoutNode* const           parent = leaf->parent;
    std::unique_ptr<LayoutNode> leaf_owned;
    bool                        leaf_was_first = false;
    if (parent && parent->type == NODE_SPLIT) {
        leaf_was_first = parent->split.first.get() == leaf;
        leaf_owned     = lt_steal_child(parent, leaf);
        if (!leaf_owned) [[unlikely]]
            return {};
    } else if (!parent) {
        if (root_take.get() != leaf) [[unlikely]]
            return {};
        leaf_owned = std::move(root_take);
    } else [[unlikely]] {
        return {};
    }

    std::unique_ptr<LayoutNode> split =
        new_first ? lt_new_split(axis, ratio, std::move(newleaf), std::move(leaf_owned)) : lt_new_split(axis, ratio, std::move(leaf_owned), std::move(newleaf));
    if (!split) [[unlikely]]
        return {};

    if (parent) {
        if (leaf_was_first)
            lt_attach_first(parent, std::move(split));
        else
            lt_attach_second(parent, std::move(split));
        return {.ok = true, .new_root = {}};
    }
    return {.ok = true, .new_root = std::move(split)};
}

/* Policy-driven split insert; drops `newleaf` on failure via unique_ptr. */
[[nodiscard]] BspTreeOps::InsertSplitResult BspTreeOps::insert_split_with_policy(LayoutNode* leaf, std::unique_ptr<LayoutNode> newleaf, std::unique_ptr<LayoutNode> root_take,
                                                                                 Client* fallback, SplitAxisFn split_axis, NewFirstFn new_first, RatioFn default_ratio) noexcept {
    if (!leaf || !newleaf || !split_axis || !new_first || !default_ratio) [[unlikely]]
        return {};
    const auto axis  = split_axis(leaf, fallback);
    const auto first = new_first(leaf, axis, fallback);
    const auto ratio = default_ratio();
    return insert_split(leaf, std::move(newleaf), std::move(root_take), axis, ratio, first);
}

/* Create a grouped leaf already populated with one client. */
[[nodiscard]] std::unique_ptr<LayoutNode> BspTreeOps::make_grouped_leaf_with_client(Client* client) noexcept {
    auto newleaf = lt_new_grouped();
    if (!newleaf) [[unlikely]]
        return {};
    if (!client || !lt_grouped_add(newleaf.get(), client)) [[unlikely]]
        return {};
    return newleaf;
}

/* Append client to existing grouped leaf and keep back-reference coherent.
 *
 * This is a narrow invariant helper: grouped storage and back-reference update are
 * treated as one atomic semantic operation from caller perspective.
 */
[[nodiscard]] bool BspTreeOps::add_client_to_grouped_leaf(LayoutNode* leaf, Client* client) noexcept {
    if (!leaf || leaf->type != NODE_GROUPED || !client) [[unlikely]]
        return false;
    if (!lt_grouped_add(leaf, client)) [[unlikely]]
        return false;
    client->leaf = leaf;
    return true;
}

/* Insert client in grouped leaf relative to active slot and keep back-reference coherent.
 *
 * Conversion note:
 * - `insert_after_current` stays strongly typed at API level.
 * - Conversion to grouped int flag is restricted to this ABI boundary call.
 */
[[nodiscard]] bool BspTreeOps::add_client_to_grouped_leaf_insert(LayoutNode* leaf, Client* client, bool insert_after_current) noexcept {
    if (!leaf || leaf->type != NODE_GROUPED || !client) [[unlikely]]
        return false;
    if (!lt_grouped_add_insert(leaf, client, insert_after_current ? 1 : 0)) [[unlikely]]
        return false;
    client->leaf = leaf;
    return true;
}
