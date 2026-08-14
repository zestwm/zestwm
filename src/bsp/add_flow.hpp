/*
 * BSP client attach/remove lifecycle API.
 *
 * Role:
 * - Orchestrate bsp_add_client / bsp_remove_client and related mutation helpers.
 * - Host compact/sanitize primitives used by arrange paths until tree_arrange split.
 *
 * Scope:
 * - Monitor/workspace ownership delegates to BspRootHandoff, BspWorkspaceStore, BspGroupFlow, BspRemoveFlow.
 * - Does not perform pixel geometry placement (see monitor/tree_arrange).
 */
#pragma once

#include "types.hpp"

#include <memory>
#include <unordered_set>
#include <utility>

Client*     wintoclient(Window w);

void        bsp_set_preselect_dir(int dir);
void        bsp_add_client(Client* c, Monitor* m);
void        bsp_remove_client(Client* c);
void        bsp_focus_client(Client* c);
void        bsp_untab_leaf(LayoutNode* leaf);
LayoutNode* bsp_active_split(Client* c);
int         bsp_set_split_ratio(LayoutNode* split, float ratio);
int         bsp_swap_clients(Client* a, Client* b);
float       bsp_default_split_ratio(void);

/* Collapse empty splits and grouped leaves in a BSP subtree (returns compacted owned tree). */
std::unique_ptr<LayoutNode> bsp_compact_tree(std::unique_ptr<LayoutNode> node);

/* Remove stale/duplicate clients from a BSP tree before `bsp_compact_tree`. */
template <typename Pred>
void sanitize_tree_for_clients(LayoutNode* node, Monitor* m, Pred&& pred, std::unordered_set<Window>& seen_windows) {
    if (!node)
        return;
    if (node->type == NODE_SPLIT) {
        sanitize_tree_for_clients(node->split.first.get(), m, pred, seen_windows);
        sanitize_tree_for_clients(node->split.second.get(), m, pred, seen_windows);
        return;
    }

    size_t write_idx  = 0;
    size_t active_idx = node->grouped.active;
    size_t new_active = 0;
    int    active_set = 0;

    for (size_t read_idx = 0; read_idx < node->grouped.clients.size(); ++read_idx) {
        Client* c = node->grouped.clients[read_idx];
        if (!c)
            continue;
        if (wintoclient(c->win) != c)
            continue;
        if (!pred(c))
            continue;
        if (!seen_windows.insert(c->win).second)
            continue;
        node->grouped.clients[write_idx] = c;
        c->leaf                          = node;
        if (read_idx == active_idx) {
            new_active = write_idx;
            active_set = 1;
        }
        ++write_idx;
    }

    node->grouped.clients.resize(write_idx);
    if (write_idx == 0) {
        node->grouped.active = 0;
    } else if (active_set) {
        node->grouped.active = new_active;
    } else if (node->grouped.active >= write_idx) {
        node->grouped.active = write_idx - 1;
    }
}
