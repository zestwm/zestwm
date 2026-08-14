/*
 * Focus cycling actions.
 * Provides ring traversal for focus changes (`cyclefocus`), vi-style directional
 * focus (`movefocus`), and convenience wrappers (`cyclenext` / `cycleprev`).
 */
#include "actions.hpp"
#include "actions/focus_cycle.hpp"
#include "direction_keys.hpp"

#include "config.hpp"
#include "client/client_focus.hpp"
#include "intern.hpp"
#include "state/wm_state_root.hpp"

#include <optional>

/*
 * Return true only for clients that can participate in cyclefocus ring order:
 * visible, focusable, non-dock, and not an inactive tab in grouped groupmode.
 */
[[nodiscard]] static inline bool client_ok_cyclefocus_ring(const Client* c) noexcept {
    if (!client_is_visible(c) || c->neverfocus)
        return false;

    // Docks are visible by policy, but never part of the focus-cycle ring.
    if (c->isdock)
        return false;

    if (c->leaf && lt_grouped_groupmode_enabled(c->leaf)) {
        const Client* active = lt_grouped_active(c->leaf);
        if (active && active != c)
            return false;
    }

    return true;
}

/* Direction selector for cyclefocus ring traversal helper. */
enum class CyclefocusDirection : signed char {
    Next = 1,
    Prev = -1
};

[[nodiscard]] static inline std::optional<DirectionKey> parse_movefocus_key_int(const int key_int) noexcept {
    const auto dir = direction_key_from_int(key_int);
    if (!dir.has_value()) [[unlikely]]
        return std::nullopt;

    /* Movefocus intentionally accepts only l/r/u/d dispatch keys.
     * t/b are valid direction keys in BSP preselect policy, but not in this UX action. */
    switch (*dir) {
        case DirectionKey::Left:
        case DirectionKey::Right:
        case DirectionKey::Up:
        case DirectionKey::Down: return dir;
        case DirectionKey::Top:
        case DirectionKey::Bottom: [[unlikely]] return std::nullopt;
    }
    return std::nullopt;
}

/*
 * Find next/previous eligible client in monitor ring order with wrap-around.
 * Uses `m->clients` singly-linked order as the canonical traversal ring.
 *
 * Return contract:
 * - nullptr on invalid input or invariant violation (`sel` not found in ring).
 * - otherwise next/prev eligible client with wrap-around semantics.
 */
[[nodiscard]] static inline Client* cyclefocus_find_relative(Monitor* m, Client* sel, CyclefocusDirection direction) noexcept {
    if (!m || !sel)
        return nullptr;

    bool    found_sel    = false;
    Client* first_after  = nullptr;
    Client* last_after   = nullptr;
    Client* first_before = nullptr;
    Client* last_before  = nullptr;

    for (Client* c : m->clients) {
        if (c == sel) [[unlikely]] {
            found_sel = true;
            continue;
        }
        if (!client_ok_cyclefocus_ring(c))
            continue;

        if (!found_sel) {
            if (!first_before)
                first_before = c;
            last_before = c;
        } else {
            if (!first_after)
                first_after = c;
            last_after = c;
        }
    }

    // Invariant violation: selected client is not present in this monitor ring.
    if (!found_sel) [[unlikely]]
        return nullptr;

    if (direction == CyclefocusDirection::Next) [[likely]]
        return first_after ? first_after : first_before;
    return last_before ? last_before : last_after;
}

/*
 * Return next eligible client after `sel` in monitor stack order.
 * Wraps to monitor head when reaching stack tail.
 */
[[nodiscard]] Client* cyclefocus_next_visible_after(Monitor* m, Client* sel) {
    return cyclefocus_find_relative(m, sel, CyclefocusDirection::Next);
}

/*
 * Return previous eligible client before `sel` in monitor stack order.
 * Wraps to monitor tail when no earlier eligible client exists.
 */
[[nodiscard]] Client* cyclefocus_prev_visible_before(Monitor* m, Client* sel) {
    return cyclefocus_find_relative(m, sel, CyclefocusDirection::Prev);
}

/* Apply one ring-focus step; `current` must already be monitor_or_fallback(product). */
static inline void cyclefocus_on_resolved_monitor(Monitor* current, CyclefocusDirection direction) noexcept {
    if (!current || !current->sel || (current->sel->isfullscreen && g_config.lockfullscreen))
        return;
    Client* c = nullptr;
    if (direction == CyclefocusDirection::Next)
        c = cyclefocus_next_visible_after(current, current->sel);
    else
        c = cyclefocus_prev_visible_before(current, current->sel);
    if (c && c != current->sel) {
        focus(c);
        restack(current);
    }
}

/*
 * Move focus along monitor ring using integer payload sign:
 *  - `> 0`: forward
 *  - `<= 0`: backward
 * Respects fullscreen lock and keeps restack in sync after focus changes.
 */
void cyclefocus(wm::state::WMState& state, const int direction_sign) {
    const auto dir = (direction_sign > 0) ? CyclefocusDirection::Next : CyclefocusDirection::Prev;
    cyclefocus_on_resolved_monitor(wm::state::monitor_or_fallback(state), dir);
}

/*
 * Vi-style directional focus dispatcher:
 *  - `l`/`r`: split-neighbor focus via `focussplit`
 *  - `u`/`d`: ring focus via `cyclefocus`
 */
void movefocus(wm::state::WMState& state, const int direction_key_int) {
    Monitor* const current = wm::state::monitor_or_fallback(state);
    const auto     dir     = parse_movefocus_key_int(direction_key_int);
    if (!dir)
        return;

    const int split_dir = (*dir == DirectionKey::Left) ? -1 : 1;
    switch (*dir) {
        case DirectionKey::Left:
        case DirectionKey::Right: focussplit(state, split_dir); break;
        case DirectionKey::Up:
        case DirectionKey::Down: {
            const auto cycle_dir = (*dir == DirectionKey::Up) ? CyclefocusDirection::Prev : CyclefocusDirection::Next;
            cyclefocus_on_resolved_monitor(current, cycle_dir);
            break;
        }
        case DirectionKey::Top:
        case DirectionKey::Bottom: break;
    }
}

/*
 * UX wrapper for "next window" semantics.
 * Uses reverse sign because client stack is newest-first; this preserves
 * expected Alt+Tab-like forward traversal.
 */
void cyclenext(wm::state::WMState& state) {
    cyclefocus_on_resolved_monitor(wm::state::monitor_or_fallback(state), CyclefocusDirection::Prev);
}

/*
 * UX wrapper for "previous window" semantics.
 * Counterpart of `cyclenext`: applies forward sign so traversal moves toward
 * the opposite direction in the newest-first client stack.
 */
void cycleprev(wm::state::WMState& state) {
    cyclefocus_on_resolved_monitor(wm::state::monitor_or_fallback(state), CyclefocusDirection::Next);
}
