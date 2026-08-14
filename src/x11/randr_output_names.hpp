/*
 * Shared RandR connected-output name enumeration.
 *
 * Role:
 * - Single traversal of the RandR screen-resource output list mapping monitor
 *   index -> connected output name.
 * - Included by both the WM (`monitor_select`) and `zestctl` so the two binaries
 *   cannot drift on monitor-name resolution.
 */
#pragma once

#include <string>
#include <vector>

#include <xcb/randr.h>
#include <xcb/xcb.h>

#include "x11/reply_ptr.hpp"

/* Return connected RandR output names in screen-resource output order (index i is
 * monitor num i). Disconnected outputs and those without a CRTC are skipped. */
[[nodiscard]] inline std::vector<std::string> randr_connected_output_names(xcb_connection_t* c, xcb_window_t root) {
    std::vector<std::string> out;
    if (!c)
        return out;
    const xcb_randr_get_screen_resources_current_cookie_t resc_cookie = xcb_randr_get_screen_resources_current(c, root);
    auto                                                  resc_reply  = make_xcb_reply_ptr(xcb_randr_get_screen_resources_current_reply(c, resc_cookie, nullptr));
    if (!resc_reply)
        return out;
    const int                 noutputs = xcb_randr_get_screen_resources_current_outputs_length(resc_reply.get());
    const xcb_randr_output_t* outputs  = xcb_randr_get_screen_resources_current_outputs(resc_reply.get());
    for (int i = 0; i < noutputs; ++i) {
        const xcb_randr_get_output_info_cookie_t info_cookie = xcb_randr_get_output_info(c, outputs[i], resc_reply->config_timestamp);
        auto                                     info_reply  = make_xcb_reply_ptr(xcb_randr_get_output_info_reply(c, info_cookie, nullptr));
        if (!info_reply)
            continue;
        if (info_reply->connection != XCB_RANDR_CONNECTION_CONNECTED || info_reply->crtc == XCB_NONE)
            continue;
        const int nlen = xcb_randr_get_output_info_name_length(info_reply.get());
        if (nlen > 0) {
            const char* name = reinterpret_cast<const char*>(xcb_randr_get_output_info_name(info_reply.get()));
            out.emplace_back(name, static_cast<std::size_t>(nlen));
        }
    }
    return out;
}
