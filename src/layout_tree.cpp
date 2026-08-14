/* BSP layout tree allocation and grouped-leaf mutation helpers. */
#include "layout_tree.hpp"
#include "wm_layout_limits.hpp"

#include <algorithm>
#include <limits>

namespace {
    constexpr size_t kGroupedPrevActiveNone = std::numeric_limits<size_t>::max();
}

/* Allocate an empty grouped leaf (tab group container). */
std::unique_ptr<LayoutNode> lt_new_grouped(void) {
    auto node                 = std::make_unique<LayoutNode>();
    node->type                = NODE_GROUPED;
    node->grouped.groupmode   = 0;
    node->grouped.prev_active = kGroupedPrevActiveNone;
    return node;
}

/* Allocate a split node wiring optional child subtrees. */
std::unique_ptr<LayoutNode> lt_new_split(SplitAxis axis, float ratio, std::unique_ptr<LayoutNode> first, std::unique_ptr<LayoutNode> second) {
    auto node         = std::make_unique<LayoutNode>();
    node->type        = NODE_SPLIT;
    node->split.axis  = axis;
    node->split.ratio = std::clamp(ratio, kSplitRatioMinF, kSplitRatioMaxF);
    if (first)
        first->parent = node.get();
    if (second)
        second->parent = node.get();
    node->split.first  = std::move(first);
    node->split.second = std::move(second);
    return node;
}

int lt_grouped_add_insert(LayoutNode* node, struct Client* client, int insert_after_active) {
    GroupedNode* grouped;

    if (!node || node->type != NODE_GROUPED || !client)
        return 0;
    grouped = &node->grouped;
    if (grouped->clients.empty() || !insert_after_active || grouped->active >= grouped->clients.size()) {
        if (!grouped->clients.empty())
            grouped->prev_active = grouped->active;
        grouped->clients.push_back(client);
        grouped->active = grouped->clients.size() - 1U;
        return 1;
    }
    grouped->prev_active = grouped->active;
    const size_t pos     = grouped->active + 1U;
    grouped->clients.insert(grouped->clients.begin() + static_cast<std::ptrdiff_t>(pos), client);
    grouped->active = pos;
    return 1;
}

int lt_grouped_add(LayoutNode* node, struct Client* client) {
    return lt_grouped_add_insert(node, client, 0);
}

int lt_grouped_add_at(LayoutNode* node, struct Client* client, size_t insert_index) {
    GroupedNode* grouped;

    if (!node || node->type != NODE_GROUPED || !client)
        return 0;
    grouped = &node->grouped;
    if (insert_index > grouped->clients.size())
        insert_index = grouped->clients.size();
    if (!grouped->clients.empty()) {
        const size_t old_act = grouped->active;
        if (insert_index <= old_act)
            grouped->prev_active = old_act + 1U;
        else
            grouped->prev_active = old_act;
    }
    grouped->clients.insert(grouped->clients.begin() + static_cast<std::ptrdiff_t>(insert_index), client);
    grouped->active = insert_index;
    return 1;
}

int lt_grouped_remove(LayoutNode* node, struct Client* client) {
    GroupedNode* grouped;
    size_t       i;

    if (!node || node->type != NODE_GROUPED || !client)
        return 0;
    grouped = &node->grouped;
    for (i = 0; i < grouped->clients.size(); i++) {
        if (grouped->clients[i] != client)
            continue;
        grouped->clients.erase(grouped->clients.begin() + static_cast<std::ptrdiff_t>(i));
        if (grouped->prev_active != kGroupedPrevActiveNone) {
            if (i < grouped->prev_active)
                grouped->prev_active--;
            else if (i == grouped->prev_active)
                grouped->prev_active = kGroupedPrevActiveNone;
        }
        if (grouped->clients.empty()) {
            grouped->active = 0;
        } else if (grouped->active >= grouped->clients.size()) {
            grouped->active = grouped->clients.size() - 1U;
        }
        return 1;
    }
    return 0;
}

int lt_grouped_move_active(LayoutNode* node, int direction) {
    GroupedNode*   grouped;
    size_t         from, to;
    struct Client* tmp;

    if (!node || node->type != NODE_GROUPED || direction == 0)
        return 0;
    grouped = &node->grouped;
    if (grouped->clients.size() < 2U)
        return 0;
    from = grouped->active;
    if (direction < 0) {
        if (from == 0U)
            return 0;
        to = from - 1U;
    } else {
        if (from + 1U >= grouped->clients.size())
            return 0;
        to = from + 1U;
    }
    tmp                    = grouped->clients[from];
    grouped->clients[from] = grouped->clients[to];
    grouped->clients[to]   = tmp;
    grouped->active        = to;
    grouped->prev_active   = kGroupedPrevActiveNone;
    return 1;
}

int lt_grouped_groupmode_enabled(const LayoutNode* node) {
    if (!node || node->type != NODE_GROUPED)
        return 0;
    return node->grouped.groupmode ? 1 : 0;
}

struct Client* lt_grouped_active(const LayoutNode* node) {
    const GroupedNode* grouped;

    if (!node || node->type != NODE_GROUPED)
        return nullptr;
    grouped = &node->grouped;
    if (grouped->clients.empty() || grouped->active >= grouped->clients.size())
        return nullptr;
    return grouped->clients[grouped->active];
}
