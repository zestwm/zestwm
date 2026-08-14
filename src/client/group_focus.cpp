/* Group focus-after-remove policy implementation. */
#include "client/group_focus.hpp"

#include "config.hpp"
#include "intern.hpp"
#include "layout_tree.hpp"

#include <limits>

namespace {

    [[nodiscard]] Client* first_visible_other(LayoutNode* leaf, Client* removing) noexcept {
        for (Client* c : leaf->grouped.clients) {
            if (c && c != removing && client_is_visible(c))
                return c;
        }
        return nullptr;
    }

    [[nodiscard]] Client* last_visible_other(LayoutNode* leaf, Client* removing) noexcept {
        Client* last = nullptr;
        for (Client* c : leaf->grouped.clients) {
            if (c && c != removing && client_is_visible(c))
                last = c;
        }
        return last;
    }

    [[nodiscard]] Client* adjacent_before(LayoutNode* leaf, int idx, Client* removing) noexcept {
        for (int i = idx - 1; i >= 0; --i) {
            Client* p = leaf->grouped.clients[static_cast<std::size_t>(i)];
            if (p && p != removing && client_is_visible(p))
                return p;
        }
        return nullptr;
    }

    [[nodiscard]] Client* adjacent_after(LayoutNode* leaf, int idx, Client* removing) noexcept {
        for (int i = idx + 1; i < static_cast<int>(leaf->grouped.clients.size()); ++i) {
            Client* n = leaf->grouped.clients[static_cast<std::size_t>(i)];
            if (n && n != removing && client_is_visible(n))
                return n;
        }
        return nullptr;
    }

    [[nodiscard]] Client* history_candidate(LayoutNode* leaf, int idx, Client* removing) noexcept {
        constexpr std::size_t kPrevNone = std::numeric_limits<std::size_t>::max();
        const std::size_t     pv        = leaf->grouped.prev_active;
        if (pv != kPrevNone && pv < leaf->grouped.clients.size()) {
            Client* const pref = leaf->grouped.clients[pv];
            if (pref && pref != removing && client_is_visible(pref))
                return pref;
        }
        if (Client* p = adjacent_before(leaf, idx, removing))
            return p;
        return adjacent_after(leaf, idx, removing);
    }

} // namespace

/* Resolve focus target among remaining visible tabs using g_config.group_focus_removed_window. */
Client* group_focus_client_after_remove(Client* removing) noexcept {
    if (!removing)
        return nullptr;
    LayoutNode* const leaf = removing->leaf;
    if (!leaf || leaf->type != NODE_GROUPED || leaf->grouped.clients.size() < 2U)
        return nullptr;
    if (g_config.group_focus_removed_window == wm::config::GroupFocusRemovedPolicy::Leave)
        return nullptr;

    int idx = -1;
    for (int i = 0; i < static_cast<int>(leaf->grouped.clients.size()); ++i) {
        if (leaf->grouped.clients[static_cast<std::size_t>(i)] == removing) {
            idx = i;
            break;
        }
    }
    if (idx < 0)
        return nullptr;

    switch (g_config.group_focus_removed_window) {
        case wm::config::GroupFocusRemovedPolicy::History: return history_candidate(leaf, idx, removing);
        case wm::config::GroupFocusRemovedPolicy::Previous: {
            if (Client* p = adjacent_before(leaf, idx, removing))
                return p;
            return adjacent_after(leaf, idx, removing);
        }
        case wm::config::GroupFocusRemovedPolicy::Next: {
            if (Client* n = adjacent_after(leaf, idx, removing))
                return n;
            return adjacent_before(leaf, idx, removing);
        }
        case wm::config::GroupFocusRemovedPolicy::First: return first_visible_other(leaf, removing);
        case wm::config::GroupFocusRemovedPolicy::Last: return last_visible_other(leaf, removing);
        case wm::config::GroupFocusRemovedPolicy::Leave: return nullptr;
    }
    return history_candidate(leaf, idx, removing);
}
