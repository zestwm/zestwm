#pragma once

#include <cstdint>
#include <xcb/xcb.h>

#include "draw.hpp"

using Clr = wm::draw::Color;

namespace wm::x11 {

    /*
     * Color conversion helpers bridging renderer RGBA and X11/XCB color APIs.
     *
     * Notes:
     * - Clr channels are normalized doubles in [0, 1].
     * - X11 color allocation uses 16-bit channels; helpers convert and query
     *   colormap-backed pixel IDs for legacy X11 attribute paths.
     */

    /* Resolve RGBA color into packed X11 pixel for XCB attribute APIs. */
    [[nodiscard]] uint32_t resolve_x11_pixel(xcb_connection_t* conn, xcb_colormap_t cmap, const Clr& color);

    /*
     * Parse color string into RGBA.
     * Accepts:
     * - "#RRGGBB" literal hex (fast path)
     * - X11 named colors via alloc/lookup fallback.
     */
    bool parse_color_rgba(xcb_connection_t* conn, xcb_colormap_t cmap, const char* clrname, Clr& out);

} // namespace wm::x11
