#include "x11/wm_ops.hpp"
#include "x11/wm_props.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

#include <xcb/xcb.h>

xcb_connection_t* xcb = nullptr;

/* Create tiny input/output window for WM_CLASS property probing. */
static Window create_probe_window(xcb_connection_t* conn, xcb_window_t root) {
    const xcb_window_t win    = xcb_generate_id(conn);
    const uint32_t     mask   = XCB_CW_EVENT_MASK;
    const uint32_t     values = XCB_EVENT_MASK_PROPERTY_CHANGE;
    xcb_create_window(conn, XCB_COPY_FROM_PARENT, win, root, 0, 0, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT, mask, &values);
    return static_cast<Window>(win);
}

/* Write arbitrary WM_CLASS payload bytes to probe window. */
static void set_wm_class_payload(xcb_connection_t* conn, Window win, const char* payload, uint32_t payload_size) {
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, static_cast<xcb_window_t>(win), XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8, payload_size, payload);
    xcb_flush(conn);
}

/* Run one parser case and print parser result + parsed values. */
static bool run_case(const char* label, Window win, const char* payload, uint32_t payload_size, bool expect_ok, const char* expected_name, const char* expected_class) {
    set_wm_class_payload(xcb, win, payload, payload_size);

    const auto hint = wm::x11::read_class_hint(static_cast<xcb_window_t>(win));
    std::cout << label << ": status=" << (hint.has_value() ? 1 : 0);
    if (hint)
        std::cout << " name=\"" << hint->instance << "\" class=\"" << hint->res_class << "\"";
    std::cout << '\n';

    const bool status_ok = expect_ok ? hint.has_value() : !hint.has_value();
    bool       names_ok  = true;
    if (expect_ok && hint)
        names_ok = hint->instance == expected_name && hint->res_class == expected_class;

    return status_ok && names_ok;
}

/* Connect X11, probe WM_CLASS payload edge cases, return non-zero on mismatch. */
int main() {
    int screen_num = 0;
    xcb            = xcb_connect(nullptr, &screen_num);
    if (!xcb || xcb_connection_has_error(xcb)) {
        std::cerr << "Failed to connect to X server\n";
        return 2;
    }

    const xcb_setup_t* setup = xcb_get_setup(xcb);
    if (!setup) {
        std::cerr << "Failed to read X setup\n";
        xcb_disconnect(xcb);
        xcb = nullptr;
        return 3;
    }

    xcb_screen_iterator_t it = xcb_setup_roots_iterator(setup);
    for (int i = 0; i < screen_num && it.rem; ++i)
        xcb_screen_next(&it);
    if (!it.data) {
        std::cerr << "Failed to resolve X screen\n";
        xcb_disconnect(xcb);
        xcb = nullptr;
        return 4;
    }

    const Window win = create_probe_window(xcb, it.data->root);
    xcb_map_window(xcb, static_cast<xcb_window_t>(win));
    xcb_flush(xcb);

    const char valid_payload[]       = {'p', 'r', 'o', 'b', 'e', '\0', 'P', 'r', 'o', 'b', 'e', 'C', 'l', 'a', 's', 's', '\0'};
    const char truncated_payload[]   = {'p', 'r', 'o', 'b', 'e', '\0'};
    const char empty_name_payload[]  = {'\0', 'P', 'r', 'o', 'b', 'e', 'C', 'l', 'a', 's', 's', '\0'};
    const char empty_class_payload[] = {'p', 'r', 'o', 'b', 'e', '\0', '\0'};
    const char no_nul_payload[]      = {'p', 'r', 'o', 'b', 'e'};

    const bool ok_valid       = run_case("valid", win, valid_payload, static_cast<uint32_t>(sizeof(valid_payload)), true, "probe", "ProbeClass");
    const bool ok_truncated   = run_case("truncated", win, truncated_payload, static_cast<uint32_t>(sizeof(truncated_payload)), false, nullptr, nullptr);
    const bool ok_empty_name  = run_case("empty-name", win, empty_name_payload, static_cast<uint32_t>(sizeof(empty_name_payload)), true, "", "ProbeClass");
    const bool ok_empty_class = run_case("empty-class", win, empty_class_payload, static_cast<uint32_t>(sizeof(empty_class_payload)), true, "probe", "");
    const bool ok_no_nul      = run_case("no-terminator", win, no_nul_payload, static_cast<uint32_t>(sizeof(no_nul_payload)), false, nullptr, nullptr);

    xcb_destroy_window(xcb, static_cast<xcb_window_t>(win));
    xcb_disconnect(xcb);
    xcb = nullptr;

    if (!ok_valid) {
        std::cerr << "Valid payload mismatch\n";
        return 5;
    }
    if (!ok_truncated) {
        std::cerr << "Truncated payload mismatch\n";
        return 6;
    }
    if (!ok_empty_name) {
        std::cerr << "Empty-name payload mismatch\n";
        return 7;
    }
    if (!ok_empty_class) {
        std::cerr << "Empty-class payload mismatch\n";
        return 8;
    }
    if (!ok_no_nul) {
        std::cerr << "No-terminator payload mismatch\n";
        return 9;
    }

    std::cout << "WM_CLASS parser probe passed\n";
    return 0;
}
