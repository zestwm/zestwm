/*
 * Selected-monitor switching helper.
 *
 * Mutations go through explicit `WMState::monitors.current` (or other `Monitor*&` slots)
 * wired to `WMRuntimeAuthority`; this module only hosts shared unfocus/assign logic.
 */
#pragma once

#include "types.hpp"

/* Switch selected monitor via an explicit slot (e.g. `WMState::monitors.current`).
 * Return true when selection changed; false when `next` is null or already selected. */
[[nodiscard]] bool switch_selected_monitor(Monitor*& current_ref, Monitor* next, bool unfocus_old) noexcept;
