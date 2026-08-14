/* Focus-cycle helper declarations shared by actions modules. */
#pragma once

#include "types.hpp"

/*
 * Helper: return next eligible client after `sel` in monitor stack order,
 * with wrap-around to head if none found.
 *
 * Invariant:
 * - `sel` is expected to belong to `m->clients` ring.
 * - Returns nullptr when ring/invariant validation fails.
 *
 * @param m monitor to traverse
 * @param sel current selected client (must belong to m->clients)
 * @return eligible Client* or nullptr if none found / invalid state
 */
[[nodiscard]] Client* cyclefocus_next_visible_after(Monitor* m, Client* sel);

/*
 * Helper: return previous eligible client before `sel` in monitor stack order,
 * with wrap-around to tail if none found.
 *
 * Invariant/return contract matches `cyclefocus_next_visible_after(...)`.
 *
 * @param m monitor to traverse
 * @param sel current selected client (must belong to m->clients)
 * @return eligible Client* or nullptr if none found / invalid state
 */
[[nodiscard]] Client* cyclefocus_prev_visible_before(Monitor* m, Client* sel);