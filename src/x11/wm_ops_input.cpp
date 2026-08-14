/*
 * XCB-native keyboard modifier and keysym helpers.
 *
 * Implements wm::x11::* from wm_input.hpp using connection() with global fallback.
 */
#include "x11/wm_input.hpp"
#include "x11/connection.hpp"
#include "x11/reply_ptr.hpp"
#include "x11/wm_ops.hpp"

#include <cstdint>

#include <X11/keysym.h>
#include <xcb/xproto.h>

namespace wm::x11 {

    namespace {

        constexpr int kModifierSlots = 8;

    } // namespace

    std::optional<ModifierMap> read_modifier_map() {
        xcb_connection_t* conn = connection();
        if (!conn)
            return std::nullopt;

        const xcb_get_modifier_mapping_cookie_t cookie = xcb_get_modifier_mapping(conn);
        auto                                    reply  = make_xcb_reply_ptr(xcb_get_modifier_mapping_reply(conn, cookie, nullptr));
        if (!reply)
            return std::nullopt;

        ModifierMap modmap{};
        modmap.max_keypermod = reply->keycodes_per_modifier;
        const int total      = modmap.max_keypermod * kModifierSlots;
        if (total > 0) {
            const auto* keycodes = xcb_get_modifier_mapping_keycodes(reply.get());
            modmap.keycodes.assign(keycodes, keycodes + total);
        }
        return modmap;
    }

    std::optional<KeyboardMapping> read_keyboard_mapping(xcb_keycode_t first_keycode, int keycode_count) {
        xcb_connection_t* conn = connection();
        if (!conn || keycode_count <= 0)
            return std::nullopt;

        const xcb_get_keyboard_mapping_cookie_t cookie = xcb_get_keyboard_mapping(conn, first_keycode, static_cast<std::uint8_t>(keycode_count));
        auto                                    reply  = make_xcb_reply_ptr(xcb_get_keyboard_mapping_reply(conn, cookie, nullptr));
        if (!reply)
            return std::nullopt;

        KeyboardMapping mapping{};
        mapping.keysyms_per_keycode = reply->keysyms_per_keycode;
        const int len               = xcb_get_keyboard_mapping_keysyms_length(reply.get());
        if (len > 0) {
            const auto* syms = xcb_get_keyboard_mapping_keysyms(reply.get());
            mapping.keysyms.assign(syms, syms + len);
        }
        return mapping;
    }

    xcb_keysym_t keysym_for_keycode(xcb_keycode_t keycode, int index) {
        if (index < 0)
            return NoSymbol;

        const auto mapping = read_keyboard_mapping(keycode, 1);
        if (!mapping || index >= mapping->keysyms_per_keycode)
            return NoSymbol;
        return mapping->keysyms[static_cast<std::size_t>(index)];
    }

    xcb_keycode_t keycode_for_keysym(xcb_keysym_t keysym) {
        int min = 0;
        int max = 0;
        display_keycode_range(&min, &max);
        if (min <= 0 || max < min)
            return 0;

        const int  count   = max - min + 1;
        const auto mapping = read_keyboard_mapping(static_cast<xcb_keycode_t>(min), count);
        if (!mapping || mapping->keysyms_per_keycode <= 0)
            return 0;

        for (int i = 0; i < count; i++) {
            for (int j = 0; j < mapping->keysyms_per_keycode; j++) {
                const std::size_t idx = static_cast<std::size_t>(i) * static_cast<std::size_t>(mapping->keysyms_per_keycode) + static_cast<std::size_t>(j);
                if (mapping->keysyms[idx] == keysym)
                    return static_cast<xcb_keycode_t>(min + i);
            }
        }
        return 0;
    }

    void display_keycode_range(int* min_keycode, int* max_keycode) {
        xcb_connection_t* conn = connection();
        if (!conn)
            return;

        const xcb_setup_t* setup = xcb_get_setup(conn);
        if (!setup)
            return;
        if (min_keycode)
            *min_keycode = setup->min_keycode;
        if (max_keycode)
            *max_keycode = setup->max_keycode;
    }

    bool grab_button(unsigned int button, unsigned int modifiers, xcb_window_t grab_window, bool owner_events, unsigned int event_mask, int pointer_mode, int keyboard_mode,
                     xcb_window_t confine_to, xcb_cursor_t cursor) {
        xcb_connection_t* conn = connection();
        if (!conn)
            return false;

        xcb_grab_button(conn, owner_events ? 1 : 0, grab_window, static_cast<std::uint16_t>(event_mask), static_cast<std::uint8_t>(pointer_mode),
                        static_cast<std::uint8_t>(keyboard_mode), confine_to, cursor, static_cast<std::uint8_t>(button), static_cast<std::uint16_t>(modifiers));
        zestwm_flush_connection();
        return true;
    }

    bool ungrab_button(unsigned int button, unsigned int modifiers, xcb_window_t grab_window) {
        xcb_connection_t* conn = connection();
        if (!conn)
            return false;

        xcb_ungrab_button(conn, static_cast<std::uint8_t>(button), grab_window, static_cast<std::uint16_t>(modifiers));
        zestwm_flush_connection();
        return true;
    }

    bool grab_key(xcb_keycode_t keycode, unsigned int modifiers, xcb_window_t grab_window, bool owner_events, int pointer_mode, int keyboard_mode) {
        xcb_connection_t* conn = connection();
        if (!conn)
            return false;

        xcb_grab_key(conn, owner_events ? 1 : 0, grab_window, static_cast<std::uint16_t>(modifiers), keycode, static_cast<std::uint8_t>(pointer_mode),
                     static_cast<std::uint8_t>(keyboard_mode));
        zestwm_flush_connection();
        return true;
    }

    bool ungrab_key(int keycode, unsigned int modifiers, xcb_window_t grab_window) {
        xcb_connection_t* conn = connection();
        if (!conn)
            return false;
        xcb_ungrab_key(conn, static_cast<xcb_keycode_t>(keycode), grab_window, static_cast<std::uint16_t>(modifiers));
        zestwm_flush_connection();
        return true;
    }

} // namespace wm::x11
