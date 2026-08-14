/*
 * RandR-backed monitor geometry queries.
 *
 * Responsibilities:
 * - Translate XCB RandR monitor replies into plain rectangle structs.
 * - Resolve monitor name atoms into UTF-8 strings for stable identity.
 * - Hide XCB iterator/reply details from higher-level WM geometry logic.
 */
#include "x11/monitor_query.hpp"

#include "x11/connection.hpp"
#include "x11/reply_ptr.hpp"

#include <cstdlib>
#include <string>

#include <xcb/randr.h>
#include <xcb/xproto.h>

namespace wm::x11 {
    namespace {

        /* Resolve an atom to its name string; empty when lookup fails. */
        [[nodiscard]] std::string atom_name_string(xcb_connection_t* conn, xcb_atom_t atom) noexcept {
            if (!conn || atom == XCB_ATOM_NONE)
                return {};
            const xcb_get_atom_name_cookie_t cookie = xcb_get_atom_name(conn, atom);
            auto                             reply  = make_xcb_reply_ptr(xcb_get_atom_name_reply(conn, cookie, nullptr));
            if (!reply)
                return {};
            const int   len  = xcb_get_atom_name_name_length(reply.get());
            const char* name = xcb_get_atom_name_name(reply.get());
            if (!name || len <= 0)
                return {};
            return std::string(name, static_cast<std::size_t>(len));
        }

    } // namespace

    std::vector<MonitorRect> query_active_monitor_rects() noexcept {
        std::vector<MonitorRect> rects;
        rects.reserve(8U);

        xcb_connection_t* const conn = connection();
        if (!conn)
            return rects;

        const xcb_randr_get_monitors_cookie_t cookie = xcb_randr_get_monitors(conn, static_cast<xcb_window_t>(root_window()), 1);
        auto                                  reply  = make_xcb_reply_ptr(xcb_randr_get_monitors_reply(conn, cookie, nullptr));
        if (!reply)
            return rects;

        const int                         count = xcb_randr_get_monitors_monitors_length(reply.get());
        xcb_randr_monitor_info_iterator_t it    = xcb_randr_get_monitors_monitors_iterator(reply.get());
        for (int i = 0; i < count; ++i, xcb_randr_monitor_info_next(&it)) {
            if (!it.data)
                continue;
            const xcb_randr_monitor_info_t& info = *it.data;
            if (info.width == 0U || info.height == 0U)
                continue;
            rects.push_back(MonitorRect{
                .x    = static_cast<int>(info.x),
                .y    = static_cast<int>(info.y),
                .w    = static_cast<int>(info.width),
                .h    = static_cast<int>(info.height),
                .name = atom_name_string(conn, info.name),
            });
        }
        return rects;
    }

} // namespace wm::x11
