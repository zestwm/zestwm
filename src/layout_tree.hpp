/* BSP layout tree node types and mutation API (split/grouped leaves). */
#pragma once

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

struct Client;

typedef enum {
    SPLIT_HORIZONTAL = 0,
    SPLIT_VERTICAL   = 1
} SplitAxis;

typedef enum {
    NODE_SPLIT   = 0,
    NODE_GROUPED = 1
} NodeType;

struct LayoutNode;

typedef struct {
    SplitAxis                   axis;
    float                       ratio; /* clamp to kSplitRatioMinF..kSplitRatioMaxF (see wm_layout_limits.hpp) */
    std::unique_ptr<LayoutNode> first;
    std::unique_ptr<LayoutNode> second;
} SplitNode;

typedef struct {
    std::vector<struct Client*> clients;
    size_t                      active;
    /* Index of the tab that had `active` before the last in-group focus/insert change; `(size_t)-1` when unset. */
    size_t prev_active;
    /* Tab UI: bar, inactive clients parked off-screen. Independent of client count (solo tile can have groupmode). */
    int groupmode;
} GroupedNode;

struct LayoutNode {
    NodeType    type;
    LayoutNode* parent;
    SplitNode   split;
    GroupedNode grouped;
};

/* Allocate an empty grouped leaf (caller owns via unique_ptr). */
[[nodiscard]] std::unique_ptr<LayoutNode> lt_new_grouped(void);
/* Allocate a split owning optional child subtrees. */
[[nodiscard]] std::unique_ptr<LayoutNode> lt_new_split(SplitAxis axis, float ratio, std::unique_ptr<LayoutNode> first, std::unique_ptr<LayoutNode> second);

/* Steal `child` from split `parent` (empty unique_ptr when not found). */
[[nodiscard]] inline std::unique_ptr<LayoutNode> lt_steal_child(LayoutNode* parent, LayoutNode* child) noexcept {
    if (!parent || parent->type != NODE_SPLIT || !child)
        return {};
    if (parent->split.first.get() == child)
        return std::move(parent->split.first);
    if (parent->split.second.get() == child)
        return std::move(parent->split.second);
    return {};
}

/* Attach owned child as split first; sets child->parent. */
inline void lt_attach_first(LayoutNode* parent, std::unique_ptr<LayoutNode> child) noexcept {
    if (!parent)
        return;
    if (child)
        child->parent = parent;
    parent->split.first = std::move(child);
}

/* Attach owned child as split second; sets child->parent. */
inline void lt_attach_second(LayoutNode* parent, std::unique_ptr<LayoutNode> child) noexcept {
    if (!parent)
        return;
    if (child)
        child->parent = parent;
    parent->split.second = std::move(child);
}

[[nodiscard]] int lt_grouped_add(LayoutNode* node, struct Client* client);
[[nodiscard]] int lt_grouped_add_insert(LayoutNode* node, struct Client* client, int insert_after_active);
/* Insert at `insert_index` (0..size inclusive); sets `active` to that slot (e.g. after fullscreen reattach). */
[[nodiscard]] int lt_grouped_add_at(LayoutNode* node, struct Client* client, size_t insert_index);
[[nodiscard]] int lt_grouped_remove(LayoutNode* node, struct Client* client);
[[nodiscard]] int lt_grouped_move_active(LayoutNode* node, int direction);
/* True only for grouped nodes with tab groupmode enabled. */
[[nodiscard]] int            lt_grouped_groupmode_enabled(const LayoutNode* node);
[[nodiscard]] struct Client* lt_grouped_active(const LayoutNode* node);
