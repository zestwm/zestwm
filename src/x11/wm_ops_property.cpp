/*
 * Property/atom helpers for ICCCM/EWMH interop paths.
 *
 * Responsibilities:
 * - Read/write atom-backed window properties through XCB.
 * - Return STL-owned payloads (`std::vector`, `std::optional`) instead of malloc buffers.
 */
#include "x11/connection.hpp"
#include "x11/icccm_types.hpp"
#include "x11/reply_ptr.hpp"
#include "x11/wm_props.hpp"
#include "x11/xcb_props.hpp"

#include <cstring>
#include <string>
#include <vector>

#include <xcb/xproto.h>

namespace wm::x11 {
    namespace {

        xcb_connection_t* conn() noexcept {
            return connection();
        }

        xcb_atom_t wm_atom(const char* name) {
            xcb_connection_t* c = conn();
            if (!c || !name)
                return XCB_ATOM_NONE;
            if (X11Backend* backend = x11_backend_peek_context(); backend && backend->conn)
                return backend->get_atom(name);
            return wm::x11::intern_atom(c, name);
        }

        std::optional<PropertyBytes> fetch_property(xcb_window_t window, xcb_atom_t property, std::uint32_t offset, std::uint32_t length, bool delete_after, xcb_atom_t req_type) {
            xcb_connection_t* c = conn();
            if (!c)
                return std::nullopt;
            const xcb_get_property_cookie_t cookie = xcb_get_property(c, delete_after ? 1 : 0, window, property, req_type, offset, length);
            auto                            reply  = make_xcb_reply_ptr(xcb_get_property_reply(c, cookie, nullptr));
            if (!reply)
                return std::nullopt;
            const int     len = xcb_get_property_value_length(reply.get());
            PropertyBytes out{};
            out.type   = reply->type;
            out.format = reply->format;
            if (len > 0) {
                const auto* bytes = static_cast<const std::uint8_t*>(xcb_get_property_value(reply.get()));
                out.data.assign(bytes, bytes + static_cast<std::size_t>(len));
            }
            return out;
        }

        bool parse_wm_class_payload(const std::uint8_t* data, std::size_t total, std::size_t* name_len, std::size_t* class_len) {
            if (!data || !name_len || !class_len || total == 0U)
                return false;
            const void* first_terminator = std::memchr(data, '\0', total);
            if (!first_terminator)
                return false;
            const std::size_t parsed_name_len = static_cast<std::size_t>(static_cast<const char*>(first_terminator) - reinterpret_cast<const char*>(data));
            const std::size_t class_offset    = parsed_name_len + 1U;
            if (class_offset >= total)
                return false;
            const void* second_terminator = std::memchr(data + class_offset, '\0', total - class_offset);
            if (!second_terminator)
                return false;
            const std::size_t parsed_class_len = static_cast<std::size_t>(static_cast<const char*>(second_terminator) - (reinterpret_cast<const char*>(data) + class_offset));
            *name_len                          = parsed_name_len;
            *class_len                         = parsed_class_len;
            return true;
        }

    } // namespace

    std::optional<QueryTree> query_tree(const xcb_window_t window) {
        xcb_connection_t* c = conn();
        if (!c)
            return std::nullopt;
        const xcb_query_tree_cookie_t cookie = xcb_query_tree(c, window);
        auto                          reply  = make_xcb_reply_ptr(xcb_query_tree_reply(c, cookie, nullptr));
        if (!reply)
            return std::nullopt;
        QueryTree out{};
        out.root    = reply->root;
        out.parent  = reply->parent;
        const int n = xcb_query_tree_children_length(reply.get());
        if (n > 0) {
            const xcb_window_t* children = xcb_query_tree_children(reply.get());
            out.children.assign(children, children + n);
        }
        return out;
    }

    std::optional<WmHints> read_wm_hints(const xcb_window_t window) {
        const xcb_atom_t wm_hints_atom = wm_atom("WM_HINTS");
        if (wm_hints_atom == XCB_ATOM_NONE)
            return std::nullopt;
        const auto prop = fetch_property(window, wm_hints_atom, 0U, 9U, false, wm_hints_atom);
        if (!prop || prop->format != 32 || prop->data.size() < sizeof(std::uint32_t))
            return std::nullopt;
        const auto* vals = reinterpret_cast<const std::uint32_t*>(prop->data.data());
        const int   n    = static_cast<int>(prop->data.size() / sizeof(std::uint32_t));
        WmHints     out{};
        if (n > 0)
            out.flags = vals[0];
        if (n > 1)
            out.input = vals[1] != 0U;
        if (n > 2)
            out.initial_state = static_cast<int>(vals[2]);
        if (n > 3)
            out.icon_pixmap = vals[3];
        if (n > 4)
            out.icon_window = vals[4];
        if (n > 5)
            out.icon_x = static_cast<int>(vals[5]);
        if (n > 6)
            out.icon_y = static_cast<int>(vals[6]);
        if (n > 7)
            out.icon_mask = vals[7];
        if (n > 8)
            out.window_group = vals[8];
        return out;
    }

    bool write_wm_hints(const xcb_window_t window, const WmHints& hints) {
        xcb_connection_t* c = conn();
        if (!c)
            return false;
        const xcb_atom_t wm_hints_atom = wm_atom("WM_HINTS");
        if (wm_hints_atom == XCB_ATOM_NONE)
            return false;
        const std::uint32_t vals[9] = {hints.flags,
                                       hints.input ? 1U : 0U,
                                       static_cast<std::uint32_t>(hints.initial_state),
                                       hints.icon_pixmap,
                                       hints.icon_window,
                                       static_cast<std::uint32_t>(hints.icon_x),
                                       static_cast<std::uint32_t>(hints.icon_y),
                                       hints.icon_mask,
                                       hints.window_group};
        xcb_change_property(c, XCB_PROP_MODE_REPLACE, window, wm_hints_atom, wm_hints_atom, 32, 9, vals);
        return true;
    }

    std::optional<SizeHints> read_size_hints(const xcb_window_t window) {
        const xcb_atom_t atom = wm_atom("WM_NORMAL_HINTS");
        if (atom == XCB_ATOM_NONE)
            return std::nullopt;
        const auto prop = fetch_property(window, atom, 0U, 18U, false, atom);
        if (!prop || prop->format != 32 || prop->data.empty())
            return std::nullopt;
        const auto* vals = reinterpret_cast<const std::uint32_t*>(prop->data.data());
        const int   n    = static_cast<int>(prop->data.size() / sizeof(std::uint32_t));
        SizeHints   out{};
        if (n > 0)
            out.flags = vals[0];
        if (n > 1)
            out.x = static_cast<int>(vals[1]);
        if (n > 2)
            out.y = static_cast<int>(vals[2]);
        if (n > 3)
            out.width = static_cast<int>(vals[3]);
        if (n > 4)
            out.height = static_cast<int>(vals[4]);
        if (n > 5)
            out.min_width = static_cast<int>(vals[5]);
        if (n > 6)
            out.min_height = static_cast<int>(vals[6]);
        if (n > 7)
            out.max_width = static_cast<int>(vals[7]);
        if (n > 8)
            out.max_height = static_cast<int>(vals[8]);
        if (n > 9)
            out.width_inc = static_cast<int>(vals[9]);
        if (n > 10)
            out.height_inc = static_cast<int>(vals[10]);
        if (n > 11)
            out.min_aspect.x = static_cast<int>(vals[11]);
        if (n > 12)
            out.min_aspect.y = static_cast<int>(vals[12]);
        if (n > 13)
            out.max_aspect.x = static_cast<int>(vals[13]);
        if (n > 14)
            out.max_aspect.y = static_cast<int>(vals[14]);
        if (n > 15)
            out.base_width = static_cast<int>(vals[15]);
        if (n > 16)
            out.base_height = static_cast<int>(vals[16]);
        if (n > 17)
            out.win_gravity = static_cast<int>(vals[17]);
        return out;
    }

    std::vector<xcb_atom_t> read_wm_protocols(const xcb_window_t window) {
        const xcb_atom_t wm_protocols = wm_atom("WM_PROTOCOLS");
        if (wm_protocols == XCB_ATOM_NONE)
            return {};
        const auto prop = fetch_property(window, wm_protocols, 0U, UINT32_MAX, false, XCB_ATOM_ATOM);
        if (!prop || prop->format != 32)
            return {};
        const std::size_t count = prop->data.size() / sizeof(xcb_atom_t);
        if (count == 0U)
            return {};
        const auto* vals = reinterpret_cast<const xcb_atom_t*>(prop->data.data());
        return std::vector<xcb_atom_t>(vals, vals + count);
    }

    std::optional<ClassHint> read_class_hint(const xcb_window_t window) {
        const auto prop = fetch_property(window, XCB_ATOM_WM_CLASS, 0U, 64U, false, XCB_ATOM_STRING);
        if (!prop || prop->format != 8U || prop->data.empty())
            return std::nullopt;
        std::size_t name_len  = 0U;
        std::size_t class_len = 0U;
        if (!parse_wm_class_payload(prop->data.data(), prop->data.size(), &name_len, &class_len))
            return std::nullopt;
        ClassHint out{};
        out.instance.assign(reinterpret_cast<const char*>(prop->data.data()), name_len);
        out.res_class.assign(reinterpret_cast<const char*>(prop->data.data() + name_len + 1U), class_len);
        return out;
    }

    bool write_class_hint(const xcb_window_t window, const std::string_view instance, const std::string_view res_class) {
        xcb_connection_t* c = conn();
        if (!c)
            return false;
        std::string payload;
        payload.reserve(instance.size() + res_class.size() + 2U);
        payload.append(instance);
        payload.push_back('\0');
        payload.append(res_class);
        payload.push_back('\0');
        xcb_change_property(c, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8, static_cast<uint32_t>(payload.size()), payload.data());
        return true;
    }

    std::optional<TextProperty> read_text_property(const xcb_window_t window, const xcb_atom_t property) {
        const auto prop = fetch_property(window, property, 0U, UINT32_MAX, false, XCB_GET_PROPERTY_TYPE_ANY);
        if (!prop)
            return std::nullopt;
        TextProperty out{};
        out.encoding = prop->type;
        out.format   = prop->format;
        out.value    = std::move(prop->data);
        return out;
    }

    std::optional<PropertyBytes> read_property(const xcb_window_t window, const xcb_atom_t property, const std::uint32_t offset, const std::uint32_t length,
                                               const bool delete_after, const xcb_atom_t req_type) {
        return fetch_property(window, property, offset, length, delete_after, req_type);
    }

    void send_client_message(xcb_window_t window, xcb_atom_t type, int format, long data0, long data1, long data2) {
        xcb_connection_t* c = conn();
        if (!c)
            return;
        xcb_client_message_event_t ev{};
        ev.response_type  = XCB_CLIENT_MESSAGE;
        ev.format         = static_cast<std::uint8_t>(format);
        ev.window         = window;
        ev.type           = type;
        ev.data.data32[0] = static_cast<std::uint32_t>(data0);
        ev.data.data32[1] = static_cast<std::uint32_t>(data1);
        ev.data.data32[2] = static_cast<std::uint32_t>(data2);
        xcb_send_event(c, 0, window, 0, reinterpret_cast<const char*>(&ev));
    }

    bool send_configure_notify(xcb_window_t window, int x, int y, unsigned width, unsigned height, unsigned border_width, xcb_window_t above, bool override_redirect) {
        xcb_connection_t* c = conn();
        if (!c)
            return false;
        xcb_configure_notify_event_t ev{};
        ev.response_type     = XCB_CONFIGURE_NOTIFY;
        ev.event             = window;
        ev.window            = window;
        ev.x                 = static_cast<std::int16_t>(x);
        ev.y                 = static_cast<std::int16_t>(y);
        ev.width             = static_cast<std::uint16_t>(width);
        ev.height            = static_cast<std::uint16_t>(height);
        ev.border_width      = static_cast<std::uint16_t>(border_width);
        ev.above_sibling     = above;
        ev.override_redirect = override_redirect ? 1U : 0U;
        xcb_send_event(c, 0, window, XCB_EVENT_MASK_STRUCTURE_NOTIFY, reinterpret_cast<const char*>(&ev));
        return true;
    }

} // namespace wm::x11
