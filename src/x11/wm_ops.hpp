/* WM-level XCB helpers. Implementations live in src/x11/wm_ops_*.cpp; call sites include focused wm_*.hpp headers. */
#pragma once

#include "x11/backend.hpp"

#include <cstdint>

/* Defined in zestwm.cpp; shared with x11/wm_ops_*.cpp for flush after grabs/sync. */
void zestwm_flush_connection(void);
void zestwm_flush_connection(X11Backend& backend);

/* Convert Bool shim (`int`) to XCB wire bool (`uint8_t`) explicitly. */
[[nodiscard]] constexpr inline std::uint8_t xcb_bool(Bool value) noexcept {
    return value ? 1U : 0U;
}
