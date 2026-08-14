/* XCB-native keyboard modifier and keysym helpers. */
#pragma once

#include "x11/icccm_types.hpp"

#include <optional>

#include <xcb/xcb.h>

namespace wm::x11 {

    [[nodiscard]] std::optional<ModifierMap>     read_modifier_map();
    [[nodiscard]] std::optional<KeyboardMapping> read_keyboard_mapping(xcb_keycode_t first_keycode, int keycode_count);
    [[nodiscard]] xcb_keysym_t                   keysym_for_keycode(xcb_keycode_t keycode, int index);
    [[nodiscard]] xcb_keycode_t                  keycode_for_keysym(xcb_keysym_t keysym);
    void                                         display_keycode_range(int* min_keycode, int* max_keycode);

    [[nodiscard]] bool grab_button(unsigned int button, unsigned int modifiers, xcb_window_t grab_window, bool owner_events, unsigned int event_mask, int pointer_mode,
                                   int keyboard_mode, xcb_window_t confine_to, xcb_cursor_t cursor);
    [[nodiscard]] bool ungrab_button(unsigned int button, unsigned int modifiers, xcb_window_t grab_window);
    [[nodiscard]] bool grab_key(xcb_keycode_t keycode, unsigned int modifiers, xcb_window_t grab_window, bool owner_events, int pointer_mode, int keyboard_mode);
    [[nodiscard]] bool ungrab_key(int keycode, unsigned int modifiers, xcb_window_t grab_window);

} // namespace wm::x11
