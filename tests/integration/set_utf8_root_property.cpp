/* Integration helper: set a UTF-8 root window property via XCB. */
#include "x11/xcb_props.hpp"

#include <cstdio>
#include <string>

#include <xcb/xcb.h>

namespace {

    /* Resolve default root window for the connected screen index. */
    xcb_window_t root_for_screen(xcb_connection_t* conn, int screen_num) {
        const xcb_setup_t*    setup = xcb_get_setup(conn);
        xcb_screen_iterator_t it    = xcb_setup_roots_iterator(setup);
        for (int i = 0; i < screen_num && it.rem; ++i)
            xcb_screen_next(&it);
        if (!it.data)
            return XCB_WINDOW_NONE;
        return it.data->root;
    }

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: set-utf8-root-property <PROP_NAME> <VALUE>\n");
        return 2;
    }
    const char* const prop_name = argv[1];
    const std::string value     = argv[2];

    int               screen_num = 0;
    xcb_connection_t* conn       = xcb_connect(nullptr, &screen_num);
    if (!conn || xcb_connection_has_error(conn)) {
        std::fprintf(stderr, "set-utf8-root-property: cannot open display\n");
        return 1;
    }

    const xcb_window_t root = root_for_screen(conn, screen_num);
    if (root == XCB_WINDOW_NONE) {
        std::fprintf(stderr, "set-utf8-root-property: cannot resolve root window\n");
        xcb_disconnect(conn);
        return 1;
    }

    const int rc = wm::x11::set_root_utf8_string(conn, root, prop_name, value);
    xcb_disconnect(conn);
    return rc == 0 ? 0 : 1;
}
