/* BSP client attach/remove lifecycle implementation.
 *
 * Adds and removes clients in the tiled layout tree and keeps the tree consistent
 * (compact, sanitize, focus, swap, split ratio). Ownership handoffs delegate to
 * sibling BSP modules. Does not place or resize windows (monitor/tree_arrange).
 */
#include "monitor/monitor_model.hpp"
#include "bsp/add_flow.hpp"

#include "bsp/group_flow.hpp"
#include "bsp/remove_flow.hpp"
#include "bsp/root_handoff.hpp"
#include "bsp/tree_ops.hpp"
#include "bsp/workspace_store.hpp"
#include "config.hpp"
#include "intern.hpp"
#include "layout_tree.hpp"
#include "monitor/world_state.hpp"
#include "state/runtime_authority.hpp"
#include "wm_layout_limits.hpp"
#include "x11/connection.hpp"
#include "x11/wm_pointer.hpp"

#include <algorithm>
#include <optional>
#include <utility>

namespace {
    /* One-shot tree insertion override for layoutmsg preselect (l/r/u/d/t/b). */
    int        g_tree_preselect_dir = 0;

    static int groupedindex(LayoutNode* leaf, Client* c, size_t* idx) {
        if (!leaf || leaf->type != NODE_GROUPED || !c || !idx)
            return 0;
        for (size_t i = 0; i < leaf->grouped.clients.size(); i++) {
            if (leaf->grouped.clients[i] == c) {
                *idx = i;
                return 1;
            }
        }
        return 0;
    }

    static SplitAxis treeinsertsplitaxis(LayoutNode* leaf, Client* fallback) {
        return BspTreeOps::infer_insert_split_axis(leaf, fallback, g_tree_preselect_dir);
    }

    static int treeinsertnewfirst(LayoutNode* leaf, SplitAxis axis, Client* fallback) {
        int px, py;
        int preselect_dir = 0;

        if (g_tree_preselect_dir != 0) {
            preselect_dir        = g_tree_preselect_dir;
            g_tree_preselect_dir = 0;
        }
        std::optional<std::pair<int, int>> pointer_pos = std::nullopt;
        if (getrootptr(&px, &py))
            pointer_pos = std::pair<int, int>{px, py};

        return BspTreeOps::compute_insert_new_first(leaf, axis, fallback, preselect_dir, g_config.dwindle_force_split, pointer_pos);
    }

    /* When `rejoin_group_peer_win` is unset, pick the only tiled grouped leaf on this workspace. */
    static LayoutNode* tree_unique_tiled_leaf_for_workspace(Monitor* m, const Client* c) {
        LayoutNode* only = nullptr;

        if (!m || !c)
            return nullptr;
        for (Client* x : m->clients) {
            if (x == c || x->isfloating || x->isdock || !x->leaf || x->leaf->type != NODE_GROUPED)
                continue;
            if (x->mon != m || x->workspace != c->workspace)
                continue;
            if (!only)
                only = x->leaf;
            else if (only != x->leaf)
                return nullptr;
        }
        return only;
    }

    static LayoutNode* tree_resolve_leaf_for_saved_slot(Monitor* m, Client* c, LayoutNode* leaf) {
        if (!c || !c->rejoin_group_slot_plus_one || c->rejoin_group_peer_win)
            return leaf;
        LayoutNode* u = tree_unique_tiled_leaf_for_workspace(m, c);
        return u ? u : leaf;
    }

    static LayoutNode* tree_leaf_from_rejoin_peer(Monitor* m, Client* c) {
        if (!m || !c || !c->rejoin_group_peer_win)
            return nullptr;
        Client* p = wintoclient(c->rejoin_group_peer_win);
        if (!p || p->mon != m || !p->leaf || p->leaf->type != NODE_GROUPED)
            return nullptr;
        if (p->workspace != c->workspace)
            return nullptr;
        return p->leaf;
    }

    static void bsp_add_client_clear_rejoin_hints(Client* c) {
        if (!c)
            return;
        c->rejoin_group_peer_win      = 0;
        c->rejoin_group_slot_plus_one = 0;
        c->rejoin_bsp                 = ClientBspRejoinHint{};
    }

    static Client* grouped_leaf_rep_tiled_client(LayoutNode* gl) {
        if (!gl || gl->type != NODE_GROUPED)
            return nullptr;
        {
            Client* u = lt_grouped_active(gl);
            if (u && !u->isfloating && !u->isdock && client_is_visible(u))
                return u;
        }
        for (size_t i = 0; i < gl->grouped.clients.size(); i++) {
            Client* x = gl->grouped.clients[i];
            if (x && !x->isfloating && !x->isdock && client_is_visible(x))
                return x;
        }
        return nullptr;
    }

    static Client* layout_first_rep_tiled(LayoutNode* n) {
        if (!n)
            return nullptr;
        if (n->type == NODE_GROUPED)
            return grouped_leaf_rep_tiled_client(n);
        if (Client* a = layout_first_rep_tiled(n->split.first.get()))
            return a;
        return layout_first_rep_tiled(n->split.second.get());
    }

    /* Re-splice sole detached client next to former sibling tile using saved split axis/ratio. */
    static int bsp_add_client_try_split_rejoin(Monitor* m, Client* c, const WorkspaceRef& ws) {
        Client* anchor;

        if (!m || !c || !c->rejoin_bsp.valid || c->rejoin_group_peer_win || c->rejoin_group_slot_plus_one)
            return 0;
        anchor = wintoclient(c->rejoin_bsp.neighbor_win);
        if (!anchor || anchor->mon != m || anchor->isfloating || anchor->isdock || !anchor->leaf || anchor->leaf->type != NODE_GROUPED || !(anchor->workspace == c->workspace)) {
            c->rejoin_bsp = ClientBspRejoinHint{};
            return 0;
        }
        LayoutNode* nleaf  = anchor->leaf;
        LayoutNode* parent = nleaf->parent;
        if (parent && parent->type != NODE_SPLIT) {
            c->rejoin_bsp = ClientBspRejoinHint{};
            return 0;
        }
        auto newleaf = lt_new_grouped();
        if (!newleaf || !lt_grouped_add(newleaf.get(), c)) {
            c->rejoin_bsp = ClientBspRejoinHint{};
            return 0;
        }
        LayoutNode* const           newleaf_obs     = newleaf.get();
        const bool                  nleaf_was_first = parent && parent->split.first.get() == nleaf;
        std::unique_ptr<LayoutNode> nleaf_owned;
        if (parent)
            nleaf_owned = lt_steal_child(parent, nleaf);
        else
            nleaf_owned = BspWorkspaceStore(*m).take(ws);
        if (!nleaf_owned || nleaf_owned.get() != nleaf) {
            c->rejoin_bsp = ClientBspRejoinHint{};
            return 0;
        }
        const ClientBspRejoinHint   bh    = c->rejoin_bsp;
        std::unique_ptr<LayoutNode> split = bh.was_second_child ? lt_new_split(bh.split_axis, bh.split_ratio, std::move(nleaf_owned), std::move(newleaf)) :
                                                                  lt_new_split(bh.split_axis, bh.split_ratio, std::move(newleaf), std::move(nleaf_owned));
        if (!split) {
            c->rejoin_bsp = ClientBspRejoinHint{};
            return 0;
        }
        if (!parent) {
            BspRootHandoff handoff(*m);
            if (ws.is_special())
                handoff.handoff_special_root(ws, std::move(split));
            else if (ws.is_normal())
                handoff.handoff_normal_root(ws, std::move(split));
        } else if (nleaf_was_first) {
            lt_attach_first(parent, std::move(split));
        } else {
            lt_attach_second(parent, std::move(split));
        }
        c->leaf = newleaf_obs;
        bsp_add_client_clear_rejoin_hints(c);
        return 1;
    }

    static LayoutNode* tree_fallback_leaf_same_workspace(Monitor* m, Client* c, const WorkspaceRef& ws) {
        Client* lf = wm::state::runtime_authority().ref_last_focused();
        if (lf && lf != c && !lf->isfloating && !lf->isdock && client_tree_member_on_workspace(lf, m, ws) && lf->leaf && lf->leaf->type == NODE_GROUPED)
            return lf->leaf;
        for (Client* x : m->clients) {
            if (x == c || x->isfloating || x->isdock || !x->leaf || x->leaf->type != NODE_GROUPED)
                continue;
            if (!client_tree_member_on_workspace(x, m, ws))
                continue;
            return x->leaf;
        }
        return nullptr;
    }

    static Client* treepointerclient_for_workspace(Monitor* m, const WorkspaceRef& ws) {
        const auto ptr = wm::x11::query_pointer(wm::x11::root_window());
        if (!m || ws.is_unset() || !ptr || !ptr->same_screen || ptr->child == None)
            return nullptr;
        Client* c = wintoclient(ptr->child);
        if (!c || c->mon != m || c->isfloating || !client_is_visible(c))
            return nullptr;
        if (!c->leaf || c->leaf->type != NODE_GROUPED)
            return nullptr;
        if (!client_tree_member_on_workspace(c, m, ws))
            return nullptr;
        return c;
    }

    static LayoutNode* tree_select_leaf_for_workspace(Monitor* m, Client* c, LayoutNode* troot, const WorkspaceRef& ws) {
        LayoutNode* leaf = tree_leaf_from_rejoin_peer(m, c);
        if (!g_config.dwindle_use_active_for_splits && !leaf) {
            if (Client* ref = treepointerclient_for_workspace(m, ws))
                leaf = ref->leaf;
        }
        if (!leaf && m->sel && m->sel->leaf && !m->sel->isfloating && client_tree_member_on_workspace(m->sel, m, ws))
            leaf = m->sel->leaf;
        if (!leaf)
            leaf = tree_fallback_leaf_same_workspace(m, c, ws);
        if (!leaf)
            leaf = BspTreeOps::leftmost_grouped_leaf(troot);
        return leaf;
    }

    static LayoutNode* tree_resolve_grouped_target_leaf(Monitor* m, Client* c, LayoutNode* leaf) {
        if (!leaf || leaf->type != NODE_GROUPED)
            return nullptr;
        leaf = tree_resolve_leaf_for_saved_slot(m, c, leaf);
        if (!leaf || leaf->type != NODE_GROUPED)
            return nullptr;
        return leaf;
    }

    static BspTreeOps::InsertSplitResult bsp_add_client_insert_split_leaf(Monitor* m, LayoutNode* leaf, std::unique_ptr<LayoutNode> newleaf) {
        Client* const               ref = (m->sel && m->sel->leaf == leaf) ? m->sel : lt_grouped_active(leaf);
        std::unique_ptr<LayoutNode> root_take;
        WorkspaceRef                taken_ws = WorkspaceRef::unset();
        if (leaf && !leaf->parent) {
            WorkspaceRef ws = WorkspaceRef::unset();
            if (!leaf->grouped.clients.empty() && leaf->grouped.clients[0])
                ws = leaf->grouped.clients[0]->workspace;
            else if (newleaf && !newleaf->grouped.clients.empty() && newleaf->grouped.clients[0])
                ws = newleaf->grouped.clients[0]->workspace;
            if (!ws.is_unset()) {
                root_take = BspWorkspaceStore(*m).take(ws);
                taken_ws  = ws;
            }
            if (root_take.get() != leaf) {
                /* Restore taken root so a failed identity check cannot destroy the workspace tree. */
                if (!taken_ws.is_unset() && root_take)
                    BspWorkspaceStore(*m).set(taken_ws, std::move(root_take));
                return {};
            }
        }
        return BspTreeOps::insert_split_with_policy(leaf, std::move(newleaf), std::move(root_take), ref, treeinsertsplitaxis, treeinsertnewfirst, bsp_default_split_ratio);
    }
} // namespace

void bsp_set_preselect_dir(int dir) {
    g_tree_preselect_dir = dir;
}

void bsp_add_client(Client* c, Monitor* m) {
    LayoutNode*        leaf = nullptr;
    const WorkspaceRef ws   = c ? c->workspace : WorkspaceRef::unset();

    if (!m || !c)
        return;
    BspRootHandoff root_handoff(*m);
    if (c->isfloating) {
        bsp_remove_client(c);
        c->leaf = nullptr;
        return;
    }
    BspGroupFlow                 attach_flow(*m);
    std::unique_ptr<LayoutNode>& troot_owned = BspWorkspaceStore(*m).owned_slot(ws);
    if (!troot_owned)
        troot_owned = lt_new_grouped();
    LayoutNode* troot = troot_owned.get();
    if (ws.is_normal())
        MonitorWorldState(*m).sync_viewed_from_active_workspace();
    if (c->rejoin_bsp.valid && !c->rejoin_group_peer_win && !c->rejoin_group_slot_plus_one && bsp_add_client_try_split_rejoin(m, c, ws))
        return;
    leaf = tree_select_leaf_for_workspace(m, c, troot, ws);
    leaf = tree_resolve_grouped_target_leaf(m, c, leaf);
    if (!leaf)
        return;
    auto attach = attach_flow.attach(c, leaf, monitor_arrange_fn(m) == tree, g_config.group_insert_after_current != 0, bsp_add_client_insert_split_leaf);
    if (!attach.attached)
        return;
    if (attach.split_root_candidate) {
        if (ws.is_special())
            root_handoff.handoff_special_root(ws, std::move(attach.split_root_candidate));
        else if (ws.is_normal())
            root_handoff.handoff_normal_root(ws, std::move(attach.split_root_candidate));
    }
    bsp_add_client_clear_rejoin_hints(c);
}

void bsp_remove_client(Client* c) {
    LayoutNode* leaf;
    Monitor*    m;

    if (!c)
        return;
    leaf = c->leaf;
    if (!leaf || leaf->type != NODE_GROUPED)
        return;
    ClientBspRejoinHint bsp_cap{};

    m = c->mon;
    if (leaf->grouped.clients.size() == 1U && leaf->parent && leaf->parent->type == NODE_SPLIT) {
        LayoutNode* const par = leaf->parent;
        LayoutNode* const sib = (par->split.first.get() == leaf) ? par->split.second.get() : par->split.first.get();
        Client* const     rep = layout_first_rep_tiled(sib);
        if (rep && rep->win) {
            bsp_cap.valid            = 1;
            bsp_cap.neighbor_win     = rep->win;
            bsp_cap.split_ratio      = par->split.ratio;
            bsp_cap.split_axis       = par->split.axis;
            bsp_cap.was_second_child = (par->split.second.get() == leaf) ? 1 : 0;
        }
    }
    Window       rejoin_peer   = 0;
    unsigned int slot_plus_one = 0;
    for (size_t i = 0; i < leaf->grouped.clients.size(); i++) {
        Client* oc = leaf->grouped.clients[i];
        if (!oc)
            continue;
        if (oc == c)
            slot_plus_one = static_cast<unsigned int>(i) + 1U;
        else if (!rejoin_peer)
            rejoin_peer = oc->win;
    }
    if (!lt_grouped_remove(leaf, c)) {
        c->leaf = nullptr;
        return;
    }
    if (!rejoin_peer) {
        slot_plus_one = 0;
        c->rejoin_bsp = bsp_cap;
    } else
        c->rejoin_bsp = ClientBspRejoinHint{};
    c->rejoin_group_peer_win      = rejoin_peer;
    c->rejoin_group_slot_plus_one = slot_plus_one;
    c->leaf                       = nullptr;
    if (!m)
        return;
    BspRemoveFlow(*m).collapse_empty_leaf(leaf, c->workspace);
    MonitorWorldState(*m).sync_viewed_from_active_workspace();
}

void bsp_focus_client(Client* c) {
    LayoutNode* leaf;

    if (!c)
        return;
    leaf = c->leaf;
    if (!leaf || leaf->type != NODE_GROUPED)
        return;
    for (size_t i = 0; i < leaf->grouped.clients.size(); i++) {
        if (leaf->grouped.clients[i] == c) {
            if (leaf->grouped.active != i)
                leaf->grouped.prev_active = leaf->grouped.active;
            leaf->grouped.active = i;
            return;
        }
    }
    c->leaf = nullptr;
}

float bsp_default_split_ratio(void) {
    float v = g_config.dwindle_default_split_ratio;

    if (v < 0.1f)
        v = 0.1f;
    else if (v > 1.9f)
        v = 1.9f;
    return v / 2.0f;
}

void bsp_untab_leaf(LayoutNode* leaf) {
    if (!leaf || leaf->type != NODE_GROUPED || leaf->grouped.clients.size() < 2 || !leaf->grouped.clients[0] || !leaf->grouped.clients[0]->mon)
        return;
    BspGroupFlow(*leaf->grouped.clients[0]->mon).detach(leaf, treeinsertsplitaxis, treeinsertnewfirst, bsp_default_split_ratio);
}

LayoutNode* bsp_active_split(Client* c) {
    LayoutNode* node;

    if (!c)
        return nullptr;
    node = c->leaf;
    if (!node)
        return nullptr;
    while (node->parent) {
        node = node->parent;
        if (node->type == NODE_SPLIT)
            return node;
    }
    return nullptr;
}

int bsp_set_split_ratio(LayoutNode* split, float ratio) {
    if (!split || split->type != NODE_SPLIT)
        return 0;
    ratio = std::clamp(ratio, kSplitRatioMinF, kSplitRatioMaxF);
    if (split->split.ratio == ratio)
        return 0;
    split->split.ratio = ratio;
    return 1;
}

int bsp_swap_clients(Client* a, Client* b) {
    LayoutNode *la, *lb;
    size_t      ia, ib;

    if (!a || !b || a == b)
        return 0;
    la = a->leaf;
    lb = b->leaf;
    if (!la || !lb || la->type != NODE_GROUPED || lb->type != NODE_GROUPED)
        return 0;
    if (!groupedindex(la, a, &ia))
        return 0;
    if (!groupedindex(lb, b, &ib))
        return 0;

    la->grouped.clients[ia] = b;
    lb->grouped.clients[ib] = a;
    a->leaf                 = lb;
    b->leaf                 = la;
    bsp_focus_client(a);
    return 1;
}

std::unique_ptr<LayoutNode> bsp_compact_tree(std::unique_ptr<LayoutNode> node) {
    if (!node)
        return nullptr;
    if (node->type == NODE_GROUPED) {
        if (node->grouped.clients.empty())
            return nullptr;
        return node;
    }

    auto left  = bsp_compact_tree(std::move(node->split.first));
    auto right = bsp_compact_tree(std::move(node->split.second));
    if (!left && !right)
        return nullptr;
    if (!left || !right) {
        auto keep    = left ? std::move(left) : std::move(right);
        keep->parent = node->parent;
        return keep;
    }
    left->parent       = node.get();
    right->parent      = node.get();
    node->split.first  = std::move(left);
    node->split.second = std::move(right);
    return node;
}
