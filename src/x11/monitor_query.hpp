/*
 * Monitor geometry query helpers for X11 RandR.
 *
 * Responsibilities:
 * - Read active monitor rectangles (and output names) from RandR monitor API.
 * - Keep X11 reply traversal and ownership isolated from WM policy code.
 */
#pragma once

#include <string>
#include <vector>

namespace wm::x11 {

    /* Immutable monitor rectangle in root coordinates, with optional RandR name. */
    struct MonitorRect {
        int         x{};
        int         y{};
        int         w{};
        int         h{};
        std::string name;
    };

    /* Return active monitor rectangles from RandR; empty when unavailable. */
    [[nodiscard]] std::vector<MonitorRect> query_active_monitor_rects() noexcept;

} // namespace wm::x11
