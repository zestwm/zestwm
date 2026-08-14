/*
 * X11 cursor creation/free helpers.
 *
 * Notes:
 * - Prefer theme cursor names first; fallback to legacy aliases.
 * - Keep create/free pair aligned on C++ allocation primitives.
 */
#include "x11/cursor_utils.hpp"

#include <X11/cursorfont.h>

#include <array>
#include <memory>

namespace wm::x11 {

    /* Ordered cursor-name fallbacks for each symbolic shape. */
    static constexpr std::array<const char*, 2> kNormalCursorNames  = {"default", "left_ptr"};
    static constexpr std::array<const char*, 3> kMoveCursorNames    = {"move", "fleur", "grabbing"};
    static constexpr std::array<const char*, 4> kResizeCursorNames  = {"se-resize", "nwse-resize", "bottom_right_corner", "bd_double_arrow"};
    static constexpr std::array<const char*, 3> kResizeHCursorNames = {"ew-resize", "col-resize", "sb_h_double_arrow"};
    static constexpr std::array<const char*, 3> kResizeVCursorNames = {"ns-resize", "row-resize", "sb_v_double_arrow"};

    /* Return list of themed cursor names to try for given shape. */
    static const std::array<const char*, 2>& normal_cursor_names() noexcept {
        return kNormalCursorNames;
    }

    static const std::array<const char*, 3>& move_cursor_names() noexcept {
        return kMoveCursorNames;
    }

    static const std::array<const char*, 4>& resize_cursor_names() noexcept {
        return kResizeCursorNames;
    }

    static const std::array<const char*, 3>& resize_h_cursor_names() noexcept {
        return kResizeHCursorNames;
    }

    static const std::array<const char*, 3>& resize_v_cursor_names() noexcept {
        return kResizeVCursorNames;
    }

    static xcb_cursor_t load_cursor_with_fallback(xcb_cursor_context_t* cursor_ctx, int shape) noexcept {
        if (!cursor_ctx)
            return XCB_NONE;
        switch (shape) {
            case XC_left_ptr:
                for (const char* name : normal_cursor_names()) {
                    const xcb_cursor_t cursor = xcb_cursor_load_cursor(cursor_ctx, name);
                    if (cursor != XCB_NONE)
                        return cursor;
                }
                break;
            case XC_fleur:
                for (const char* name : move_cursor_names()) {
                    const xcb_cursor_t cursor = xcb_cursor_load_cursor(cursor_ctx, name);
                    if (cursor != XCB_NONE)
                        return cursor;
                }
                break;
            case XC_sizing:
                for (const char* name : resize_cursor_names()) {
                    const xcb_cursor_t cursor = xcb_cursor_load_cursor(cursor_ctx, name);
                    if (cursor != XCB_NONE)
                        return cursor;
                }
                break;
            case XC_sb_h_double_arrow:
                for (const char* name : resize_h_cursor_names()) {
                    const xcb_cursor_t cursor = xcb_cursor_load_cursor(cursor_ctx, name);
                    if (cursor != XCB_NONE)
                        return cursor;
                }
                break;
            case XC_sb_v_double_arrow:
                for (const char* name : resize_v_cursor_names()) {
                    const xcb_cursor_t cursor = xcb_cursor_load_cursor(cursor_ctx, name);
                    if (cursor != XCB_NONE)
                        return cursor;
                }
                break;
            default: break;
        }
        return xcb_cursor_load_cursor(cursor_ctx, "default");
    }

    static const char* cursor_name_for_shape(int shape) noexcept {
        switch (shape) {
            case XC_left_ptr: return "default";
            case XC_fleur: return "fleur";
            case XC_sizing: return "bottom_right_corner";
            case XC_sb_h_double_arrow: return "sb_h_double_arrow";
            case XC_sb_v_double_arrow: return "sb_v_double_arrow";
            default: return "default";
        }
    }

    /* Create a cursor object from X cursor context and symbolic shape id. */
    Cur* create_cursor(xcb_cursor_context_t* cursor_ctx, int shape) {
        if (!cursor_ctx)
            return nullptr;
        auto cur    = std::make_unique<Cur>();
        cur->cursor = XCB_NONE;
        cur->cursor = load_cursor_with_fallback(cursor_ctx, shape);
        if (cur->cursor == XCB_NONE)
            cur->cursor = xcb_cursor_load_cursor(cursor_ctx, cursor_name_for_shape(shape));
        return cur.release();
    }

    /* Release cursor resources allocated through create_cursor. */
    void free_cursor(xcb_connection_t* conn, Cur* cursor) {
        if (!cursor)
            return;
        if (conn && cursor->cursor != XCB_NONE)
            xcb_free_cursor(conn, cursor->cursor);
        delete cursor;
    }

} // namespace wm::x11
