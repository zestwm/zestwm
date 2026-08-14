/* Integration probe client: map a test window, optional urgency, WM_CLASS routing hooks. */
#include "x11/reply_ptr.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <unistd.h>

#include <xcb/xcb.h>
#include <xcb/xproto.h>

namespace {

    constexpr long    kInputHint   = (1L << 0);
    constexpr long    kUrgencyHint = (1L << 8);

    std::atomic<bool> g_running{true};
    std::atomic<bool> g_request_urgent{false};

    void              on_signal(int) {
        g_running.store(false);
    }

    void on_urgent_signal(int) {
        g_request_urgent.store(true);
    }

    xcb_atom_t intern_atom(xcb_connection_t* conn, const char* name) {
        if (!conn || !name || !*name)
            return XCB_ATOM_NONE;
        const auto len    = static_cast<uint16_t>(std::strlen(name));
        const auto cookie = xcb_intern_atom(conn, 0, len, name);
        auto       reply  = make_xcb_reply_ptr(xcb_intern_atom_reply(conn, cookie, nullptr));
        if (!reply)
            return XCB_ATOM_NONE;
        return reply->atom;
    }

    xcb_screen_t* screen_at(xcb_connection_t* conn, int screen_num) {
        const xcb_setup_t*    setup = xcb_get_setup(conn);
        xcb_screen_iterator_t it    = xcb_setup_roots_iterator(setup);
        for (int i = 0; i < screen_num && it.rem; ++i)
            xcb_screen_next(&it);
        return it.data;
    }

    void set_wm_class(xcb_connection_t* conn, xcb_window_t win, const char* instance, const char* klass) {
        const std::string payload = std::string(instance) + '\0' + klass + '\0';
        xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8, static_cast<uint32_t>(payload.size()), payload.data());
    }

    void set_wm_name(xcb_connection_t* conn, xcb_window_t win, const char* title) {
        const auto len = static_cast<uint32_t>(std::strlen(title));
        xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, len, title);
    }

    void set_urgency(xcb_connection_t* conn, xcb_window_t win) {
        const xcb_atom_t wm_hints = intern_atom(conn, "WM_HINTS");
        if (wm_hints == XCB_ATOM_NONE)
            return;
        const uint32_t vals[9] = {static_cast<uint32_t>(kUrgencyHint | kInputHint), 1U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
        xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, wm_hints, wm_hints, 32, 9, vals);
        xcb_flush(conn);
    }

    bool write_ready_file(const std::string& path, xcb_window_t win) {
        FILE* fp = std::fopen(path.c_str(), "w");
        if (fp == nullptr)
            return false;
        const int rc = std::fprintf(fp, "%u\n", static_cast<unsigned>(win));
        std::fclose(fp);
        return rc > 0;
    }

} // namespace

int main(int argc, char** argv) {
    std::string  title           = "focusurgent-client";
    int          urgent_after_ms = -1;
    int          width           = 360;
    int          height          = 220;
    std::string  ready_file_path;
    int          desktop_index  = -1;
    bool         dock_window    = false;
    std::string  wm_instance    = "focusurgent-probe";
    std::string  wm_class       = "FocusurgentProbe";
    bool         dialog_window  = false;
    bool         set_net_wm_pid = false;
    xcb_window_t transient_for  = XCB_WINDOW_NONE;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--title") == 0 && i + 1 < argc) {
            title = argv[++i];
        } else if (std::strcmp(argv[i], "--wm-instance") == 0 && i + 1 < argc) {
            wm_instance = argv[++i];
        } else if (std::strcmp(argv[i], "--wm-class") == 0 && i + 1 < argc) {
            wm_class = argv[++i];
        } else if (std::strcmp(argv[i], "--urgent-after-ms") == 0 && i + 1 < argc) {
            urgent_after_ms = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            width = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            height = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--ready-file") == 0 && i + 1 < argc) {
            ready_file_path = argv[++i];
        } else if (std::strcmp(argv[i], "--desktop-index") == 0 && i + 1 < argc) {
            desktop_index = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--dock") == 0) {
            dock_window = true;
        } else if (std::strcmp(argv[i], "--dialog") == 0) {
            dialog_window = true;
        } else if (std::strcmp(argv[i], "--net-wm-pid") == 0) {
            set_net_wm_pid = true;
        } else if (std::strcmp(argv[i], "--transient-for") == 0 && i + 1 < argc) {
            transient_for = static_cast<xcb_window_t>(std::strtoul(argv[++i], nullptr, 0));
        } else {
            std::fprintf(stderr,
                         "usage: %s [--title NAME] [--wm-instance NAME] [--wm-class NAME] [--urgent-after-ms N] [--ready-file PATH] [--width N] "
                         "[--height N] [--desktop-index N] [--dock] [--dialog] [--net-wm-pid] [--transient-for WINDOW]\n",
                         argv[0]);
            return 2;
        }
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    std::signal(SIGUSR1, on_urgent_signal);

    int               screen_num = 0;
    xcb_connection_t* conn       = xcb_connect(nullptr, &screen_num);
    if (!conn || xcb_connection_has_error(conn)) {
        std::fprintf(stderr, "focusurgent-client: cannot open X display\n");
        return 1;
    }

    xcb_screen_t* screen = screen_at(conn, screen_num);
    if (!screen) {
        std::fprintf(stderr, "focusurgent-client: cannot resolve screen\n");
        xcb_disconnect(conn);
        return 1;
    }

    const xcb_window_t win           = xcb_generate_id(conn);
    const uint32_t     event_mask    = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE;
    const uint32_t     value_mask    = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    const uint32_t     value_list[2] = {screen->white_pixel, event_mask};

    xcb_create_window(conn, screen->root_depth, win, screen->root, 40, 40, static_cast<uint16_t>(width), static_cast<uint16_t>(height), 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      screen->root_visual, value_mask, value_list);

    set_wm_name(conn, win, title.c_str());
    set_wm_class(conn, win, wm_instance.c_str(), wm_class.c_str());

    if (desktop_index >= 0) {
        const xcb_atom_t net_wm_desktop = intern_atom(conn, "_NET_WM_DESKTOP");
        const uint32_t   desktop        = static_cast<uint32_t>(desktop_index);
        if (net_wm_desktop != XCB_ATOM_NONE)
            xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, net_wm_desktop, XCB_ATOM_CARDINAL, 32, 1, &desktop);
    }
    if (dock_window) {
        const xcb_atom_t net_wm_window_type      = intern_atom(conn, "_NET_WM_WINDOW_TYPE");
        const xcb_atom_t net_wm_window_type_dock = intern_atom(conn, "_NET_WM_WINDOW_TYPE_DOCK");
        if (net_wm_window_type != XCB_ATOM_NONE && net_wm_window_type_dock != XCB_ATOM_NONE)
            xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, net_wm_window_type, XCB_ATOM_ATOM, 32, 1, &net_wm_window_type_dock);
    }
    if (dialog_window) {
        const xcb_atom_t net_wm_window_type        = intern_atom(conn, "_NET_WM_WINDOW_TYPE");
        const xcb_atom_t net_wm_window_type_dialog = intern_atom(conn, "_NET_WM_WINDOW_TYPE_DIALOG");
        if (net_wm_window_type != XCB_ATOM_NONE && net_wm_window_type_dialog != XCB_ATOM_NONE)
            xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, net_wm_window_type, XCB_ATOM_ATOM, 32, 1, &net_wm_window_type_dialog);
    }
    if (set_net_wm_pid) {
        const xcb_atom_t net_wm_pid = intern_atom(conn, "_NET_WM_PID");
        const uint32_t   pid        = static_cast<uint32_t>(getpid());
        if (net_wm_pid != XCB_ATOM_NONE)
            xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, net_wm_pid, XCB_ATOM_CARDINAL, 32, 1, &pid);
    }
    if (transient_for != XCB_WINDOW_NONE)
        xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, XCB_ATOM_WM_TRANSIENT_FOR, XCB_ATOM_WINDOW, 32, 1, &transient_for);

    xcb_map_window(conn, win);
    {
        const uint32_t mask  = XCB_CONFIG_WINDOW_STACK_MODE;
        const uint32_t above = XCB_STACK_MODE_ABOVE;
        xcb_configure_window(conn, win, mask, &above);
    }
    xcb_flush(conn);

    if (!ready_file_path.empty() && !write_ready_file(ready_file_path, win)) {
        std::fprintf(stderr, "focusurgent-client: cannot write ready file '%s'\n", ready_file_path.c_str());
        xcb_destroy_window(conn, win);
        xcb_disconnect(conn);
        return 1;
    }

    auto start       = std::chrono::steady_clock::now();
    bool urgency_set = false;

    while (g_running.load()) {
        while (xcb_generic_event_t* ev = xcb_poll_for_event(conn)) {
            const uint8_t type = static_cast<uint8_t>(ev->response_type & 0x7FU);
            if (type == XCB_DESTROY_NOTIFY)
                g_running.store(false);
            std::free(ev);
        }

        if (!urgency_set && g_request_urgent.load()) {
            set_urgency(conn, win);
            urgency_set = true;
        }

        if (!urgency_set && urgent_after_ms >= 0) {
            const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            if (elapsed_ms >= urgent_after_ms) {
                set_urgency(conn, win);
                urgency_set = true;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    xcb_destroy_window(conn, win);
    xcb_flush(conn);
    xcb_disconnect(conn);
    return 0;
}
