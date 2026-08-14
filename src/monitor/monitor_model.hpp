/* Monitor client lists, stack order, and layout slot helpers. */
#pragma once

#include "types.hpp"

#include <algorithm>
#include <cstddef>

/* Returns the layout currently selected on `m`. */
[[nodiscard]] inline const Layout* monitor_active_layout(const Monitor* m) noexcept {
    if (!m)
        return nullptr;
    return m->layout_slots[m->active_layout_slot & 1U];
}

/* Returns the active layout arrange function, or nullptr when unset. */
[[nodiscard]] inline void (*monitor_arrange_fn(const Monitor* m))(Monitor*) {
    const Layout* const lt = monitor_active_layout(m);
    return lt ? lt->arrange : nullptr;
}

/* Sync `layout_label` from the active layout symbol. */
inline void monitor_sync_layout_label(Monitor* m) noexcept {
    if (!m)
        return;
    const Layout* const lt = monitor_active_layout(m);
    m->layout_label        = lt ? lt->symbol : std::string{};
}

/* Insert client at head of monitor client list (newest first). */
inline void monitor_prepend_client(Monitor* m, Client* c) noexcept {
    if (!m || !c)
        return;
    m->clients.insert(m->clients.begin(), c);
}

/* Remove client from monitor client list. */
inline void monitor_remove_client(Monitor* m, Client* c) noexcept {
    if (!m || !c)
        return;
    auto& list = m->clients;
    list.erase(std::remove(list.begin(), list.end(), c), list.end());
}

/* Insert client at head of stack order (newest first). */
inline void monitor_prepend_stack(Monitor* m, Client* c) noexcept {
    if (!m || !c)
        return;
    m->stack.insert(m->stack.begin(), c);
}

/* Remove client from stack order. */
inline void monitor_remove_stack(Monitor* m, Client* c) noexcept {
    if (!m || !c)
        return;
    auto& list = m->stack;
    list.erase(std::remove(list.begin(), list.end(), c), list.end());
}

/* Next older client in stack order after `c`. */
[[nodiscard]] inline Client* monitor_stack_older(const Monitor* m, const Client* c) noexcept {
    if (!m || !c)
        return nullptr;
    for (std::size_t i = 0; i < m->stack.size(); ++i) {
        if (m->stack[i] == c && i + 1U < m->stack.size())
            return m->stack[i + 1U];
    }
    return nullptr;
}

/* Apply layout selection using dual-slot toggle semantics on `layout_slots`. */
inline void monitor_apply_layout(Monitor* m, const Layout* selected) noexcept {
    if (!m)
        return;
    const Layout* const active = monitor_active_layout(m);
    if (!selected || selected != active)
        m->active_layout_slot ^= 1U;
    if (selected)
        m->layout_slots[m->active_layout_slot & 1U] = selected;
    monitor_sync_layout_label(m);
}
