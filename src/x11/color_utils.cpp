/*
 * X11 color conversion helpers (RGBA <-> packed pixel).
 *
 * Responsibilities:
 * - Parse user/config color strings into normalized RGBA values.
 * - Resolve normalized RGB channels to X11 pixel IDs through colormap APIs.
 * - Keep allocation/reply handling RAII-safe via shared reply_ptr wrapper.
 */
#include "x11/color_utils.hpp"
#include "x11/reply_ptr.hpp"

#include <cstring>

namespace wm::x11 {

    static constexpr double kColorChannelScale = 65535.0;

    /*
 * Parse strict "#RRGGBB" literal into normalized RGB channels.
 * Invalid input returns black (0,0,0), leaving caller with deterministic base.
 */
    static void parse_color_rgb(const char* clrname, double* r, double* g, double* b) {
        unsigned int ir = 0U;
        unsigned int ig = 0U;
        unsigned int ib = 0U;

        if (clrname && clrname[0] == '#') {
            const char* hex            = clrname + 1;
            auto        parse_hex_byte = [](const char* s, unsigned int* out) -> bool {
                if (!s[0] || !s[1])
                    return false;
                unsigned int v = 0U;
                for (int i = 0; i < 2; ++i) {
                    const unsigned char ch = static_cast<unsigned char>(s[i]);
                    v <<= 4U;
                    if (ch >= '0' && ch <= '9')
                        v |= static_cast<unsigned int>(ch - '0');
                    else if (ch >= 'a' && ch <= 'f')
                        v |= static_cast<unsigned int>(ch - 'a' + 10U);
                    else if (ch >= 'A' && ch <= 'F')
                        v |= static_cast<unsigned int>(ch - 'A' + 10U);
                    else
                        return false;
                }
                *out = v;
                return true;
            };
            if (parse_hex_byte(hex, &ir) && parse_hex_byte(hex + 2, &ig) && parse_hex_byte(hex + 4, &ib)) {
                *r = ir / 255.0;
                *g = ig / 255.0;
                *b = ib / 255.0;
                return;
            }
        }
        *r = 0.0;
        *g = 0.0;
        *b = 0.0;
    }

    /*
 * Convert normalized RGB channels to 16-bit X11 channels and allocate pixel.
 * Returns zero when connection/reply fails (caller treats as fallback-safe).
 */
    uint32_t resolve_x11_pixel(xcb_connection_t* conn, xcb_colormap_t cmap, const Clr& color) {
        if (!conn)
            return 0U;

        const uint16_t           red16 = static_cast<uint16_t>(static_cast<int>(color.r * kColorChannelScale) & 0xffff);
        const uint16_t           grn16 = static_cast<uint16_t>(static_cast<int>(color.g * kColorChannelScale) & 0xffff);
        const uint16_t           blu16 = static_cast<uint16_t>(static_cast<int>(color.b * kColorChannelScale) & 0xffff);

        xcb_alloc_color_cookie_t ac = xcb_alloc_color(conn, cmap, red16, grn16, blu16);
        auto                     ar = make_xcb_reply_ptr(xcb_alloc_color_reply(conn, ac, nullptr));
        if (!ar)
            return 0U;
        const uint32_t pixel = ar->pixel;
        return pixel;
    }

    /*
 * Parse color input into RGBA.
 * Flow:
 * 1) Default out to opaque black.
 * 2) Try hex fast path (#RRGGBB).
 * 3) Resolve named color via alloc_named reply.
 * 4) Fallback to lookup_color exact values.
 */
    bool parse_color_rgba(xcb_connection_t* conn, xcb_colormap_t cmap, const char* clrname, Clr& out) {
        if (!conn || !clrname)
            return false;

        out = Clr::from_rgba(0.0, 0.0, 0.0, 1.0);
        parse_color_rgb(clrname, &out.r, &out.g, &out.b);
        if (clrname[0] == '#')
            return true;

        const auto                     name_len = static_cast<uint16_t>(std::strlen(clrname));

        xcb_alloc_named_color_cookie_t nc = xcb_alloc_named_color(conn, cmap, name_len, clrname);
        auto                           nr = make_xcb_reply_ptr(xcb_alloc_named_color_reply(conn, nc, nullptr));
        if (nr) {
            out.r = nr->visual_red / kColorChannelScale;
            out.g = nr->visual_green / kColorChannelScale;
            out.b = nr->visual_blue / kColorChannelScale;
            return true;
        }

        xcb_lookup_color_cookie_t lc = xcb_lookup_color(conn, cmap, name_len, clrname);
        auto                      lr = make_xcb_reply_ptr(xcb_lookup_color_reply(conn, lc, nullptr));
        if (!lr)
            return false;
        out.r = lr->exact_red / kColorChannelScale;
        out.g = lr->exact_green / kColorChannelScale;
        out.b = lr->exact_blue / kColorChannelScale;
        return true;
    }

} // namespace wm::x11
