/* XCB-native window property helpers for ICCCM/EWMH interop. */
#pragma once

#include "x11/icccm_types.hpp"

#include <optional>
#include <string_view>
#include <vector>

#include <xcb/xcb.h>

namespace wm::x11 {

    /* Query parent/root and child windows for a window subtree. */
    [[nodiscard]] std::optional<QueryTree> query_tree(xcb_window_t window);

    /* Read/write WM_HINTS (urgency, input, icons). */
    [[nodiscard]] std::optional<WmHints> read_wm_hints(xcb_window_t window);
    [[nodiscard]] bool                   write_wm_hints(xcb_window_t window, const WmHints& hints);

    /* Read WM_NORMAL_HINTS size constraints. */
    [[nodiscard]] std::optional<SizeHints> read_size_hints(xcb_window_t window);

    /* Read WM_PROTOCOLS atom list. */
    [[nodiscard]] std::vector<xcb_atom_t> read_wm_protocols(xcb_window_t window);

    /* Read/write WM_CLASS instance and class strings. */
    [[nodiscard]] std::optional<ClassHint> read_class_hint(xcb_window_t window);
    [[nodiscard]] bool                     write_class_hint(xcb_window_t window, std::string_view instance, std::string_view res_class);

    /* Read a text-oriented property payload (UTF-8/STRING/Latin1). */
    [[nodiscard]] std::optional<TextProperty> read_text_property(xcb_window_t window, xcb_atom_t property);

    /* Read raw property bytes with explicit type/offset/length. */
    [[nodiscard]] std::optional<PropertyBytes> read_property(xcb_window_t window, xcb_atom_t property, std::uint32_t offset, std::uint32_t length, bool delete_after = false,
                                                             xcb_atom_t req_type = XCB_GET_PROPERTY_TYPE_ANY);

    /* Send a ClientMessage or synthetic ConfigureNotify to a window. */
    void               send_client_message(xcb_window_t window, xcb_atom_t type, int format, long data0, long data1, long data2);
    [[nodiscard]] bool send_configure_notify(xcb_window_t window, int x, int y, unsigned width, unsigned height, unsigned border_width, xcb_window_t above, bool override_redirect);

} // namespace wm::x11
