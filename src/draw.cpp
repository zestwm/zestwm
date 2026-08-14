#include "draw.hpp"

#include <cairo/cairo-xcb.h>
#include <glib-object.h>
#include <pango/pangocairo.h>
#include <xcb/xcb.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <numbers>
#include <memory>
#include <string>

namespace wm::draw {

    namespace {

#ifndef NDEBUG
        /* Debug-only flush ownership guard: only present() may flush the XCB connection. */
        thread_local bool g_present_flush_scope = false;

        class ScopedPresentFlush final {
          public:
            ScopedPresentFlush() noexcept {
                g_present_flush_scope = true;
            }
            ~ScopedPresentFlush() {
                g_present_flush_scope = false;
            }
        };

        /* Assert flush policy in debug builds. */
        void debug_flush_guard() {
            assert(g_present_flush_scope && "wm::draw::Canvas: xcb_flush outside present()");
        }
#else
        class ScopedPresentFlush final {
          public:
            ScopedPresentFlush() noexcept = default;
        };

        void debug_flush_guard() {}
#endif

        /* Clamp color channels so cairo always receives valid normalized values. */
        [[nodiscard]] double clamp_unit(double value) noexcept {
            return std::clamp(value, 0.0, 1.0);
        }

        /* Set cairo source from normalized RGBA color structure. */
        void set_source_rgba(cairo_t* cr, Color color) noexcept {
            cairo_set_source_rgba(cr, clamp_unit(color.r), clamp_unit(color.g), clamp_unit(color.b), clamp_unit(color.a));
        }

        /* Resolve root visual entry from xcb_screen root_visual id. */
        [[nodiscard]] xcb_visualtype_t* find_root_visual(xcb_screen_t& screen) noexcept {
            xcb_depth_iterator_t dit = xcb_screen_allowed_depths_iterator(&screen);
            for (; dit.rem; xcb_depth_next(&dit)) {
                xcb_visualtype_iterator_t vit = xcb_depth_visuals_iterator(dit.data);
                for (; vit.rem; xcb_visualtype_next(&vit)) {
                    if (vit.data->visual_id == screen.root_visual)
                        return vit.data;
                }
            }
            return nullptr;
        }

        /* Build a readable error from cairo status code. */
        [[nodiscard]] std::string cairo_error(const char* prefix, cairo_status_t status) {
            std::string out(prefix);
            out.append(": ");
            out.append(cairo_status_to_string(status));
            return out;
        }

    } // namespace

    struct Canvas::CanvasState {
        xcb_connection_t*     conn   = nullptr;
        xcb_screen_t*         screen = nullptr;
        xcb_window_t          target = XCB_WINDOW_NONE;
        xcb_colormap_t        cmap   = XCB_COLORMAP_NONE;
        xcb_visualtype_t*     visual = nullptr;
        std::uint8_t          depth  = 0;

        unsigned              width  = 0;
        unsigned              height = 0;

        xcb_pixmap_t          pixmap  = XCB_PIXMAP_NONE;
        xcb_gcontext_t        gc      = XCB_NONE;
        cairo_surface_t*      surface = nullptr;
        cairo_t*              cairo   = nullptr;

        PangoContext*         pango_ctx = nullptr;
        PangoLayout*          layout    = nullptr;
        PangoFontDescription* font_desc = nullptr;
    };

    /* Recreate backing pixmap and cairo drawing surface for current dimensions. */
    template <typename State>
    [[nodiscard]] static std::expected<void, std::string> recreate_surface(State& state) {
        if (!state.conn || !state.visual)
            return std::unexpected("draw: invalid canvas state");
        if (state.width == 0 || state.height == 0)
            return std::unexpected("draw: canvas dimensions must be > 0");
        if (state.width > static_cast<unsigned>(std::numeric_limits<std::uint16_t>::max()) || state.height > static_cast<unsigned>(std::numeric_limits<std::uint16_t>::max()))
            return std::unexpected("draw: canvas dimensions exceed X pixmap limit");

        if (state.cairo) {
            cairo_destroy(state.cairo);
            state.cairo = nullptr;
        }
        if (state.surface) {
            cairo_surface_destroy(state.surface);
            state.surface = nullptr;
        }
        if (state.pixmap != XCB_PIXMAP_NONE) {
            xcb_free_pixmap(state.conn, state.pixmap);
            state.pixmap = XCB_PIXMAP_NONE;
        }

        state.pixmap = xcb_generate_id(state.conn);
        xcb_create_pixmap(state.conn, state.depth, state.pixmap, state.target, static_cast<std::uint16_t>(state.width), static_cast<std::uint16_t>(state.height));

        state.surface = cairo_xcb_surface_create(state.conn, state.pixmap, state.visual, static_cast<int>(state.width), static_cast<int>(state.height));
        if (!state.surface)
            return std::unexpected("draw: failed to create cairo xcb surface");

        const cairo_status_t surface_status = cairo_surface_status(state.surface);
        if (surface_status != CAIRO_STATUS_SUCCESS)
            return std::unexpected(cairo_error("draw: cairo surface error", surface_status));

        state.cairo = cairo_create(state.surface);
        if (!state.cairo)
            return std::unexpected("draw: failed to create cairo context");

        const cairo_status_t cr_status = cairo_status(state.cairo);
        if (cr_status != CAIRO_STATUS_SUCCESS)
            return std::unexpected(cairo_error("draw: cairo context error", cr_status));

        return {};
    }

    Canvas::Canvas(std::unique_ptr<CanvasState> state) : state_(std::move(state)) {}

    Canvas::~Canvas() {
        if (!state_)
            return;

        if (state_->font_desc) {
            pango_font_description_free(state_->font_desc);
            state_->font_desc = nullptr;
        }
        if (state_->layout) {
            g_object_unref(state_->layout);
            state_->layout = nullptr;
        }
        if (state_->pango_ctx) {
            g_object_unref(state_->pango_ctx);
            state_->pango_ctx = nullptr;
        }
        if (state_->cairo) {
            cairo_destroy(state_->cairo);
            state_->cairo = nullptr;
        }
        if (state_->surface) {
            cairo_surface_destroy(state_->surface);
            state_->surface = nullptr;
        }
        if (state_->pixmap != XCB_PIXMAP_NONE) {
            xcb_free_pixmap(state_->conn, state_->pixmap);
            state_->pixmap = XCB_PIXMAP_NONE;
        }
        if (state_->gc != XCB_NONE) {
            xcb_free_gc(state_->conn, state_->gc);
            state_->gc = XCB_NONE;
        }
    }

    std::expected<std::unique_ptr<Canvas>, std::string> Canvas::create(xcb_connection_t& conn, xcb_screen_t& screen, xcb_window_t target, unsigned width, unsigned height) {
        if (target == XCB_WINDOW_NONE)
            return std::unexpected("draw: target window is invalid");
        if (width == 0 || height == 0)
            return std::unexpected("draw: canvas dimensions must be > 0");

        xcb_visualtype_t* visual = find_root_visual(screen);
        if (!visual)
            return std::unexpected("draw: unable to resolve root visual");

        auto state    = std::make_unique<CanvasState>();
        state->conn   = &conn;
        state->screen = &screen;
        state->target = target;
        state->cmap   = screen.default_colormap;
        state->visual = visual;
        state->depth  = screen.root_depth;
        state->width  = width;
        state->height = height;

        state->gc                       = xcb_generate_id(&conn);
        const std::uint32_t gc_values[] = {0, 1};
        xcb_create_gc(&conn, state->gc, target, XCB_GC_FOREGROUND | XCB_GC_BACKGROUND, gc_values);

        if (auto surface_res = recreate_surface(*state); !surface_res.has_value())
            return std::unexpected(surface_res.error());

        PangoFontMap* font_map = pango_cairo_font_map_get_default();
        state->pango_ctx       = pango_font_map_create_context(font_map);
        if (!state->pango_ctx)
            return std::unexpected("draw: failed to create pango context");

        state->layout = pango_layout_new(state->pango_ctx);
        if (!state->layout)
            return std::unexpected("draw: failed to create pango layout");

        state->font_desc = pango_font_description_from_string("monospace 10");
        if (!state->font_desc)
            return std::unexpected("draw: failed to create default font description");
        pango_layout_set_font_description(state->layout, state->font_desc);

        return std::unique_ptr<Canvas>(new Canvas(std::move(state)));
    }

    std::expected<void, std::string> Canvas::resize(unsigned width, unsigned height) {
        if (width == 0 || height == 0)
            return std::unexpected("draw: resize dimensions must be > 0");
        state_->width  = width;
        state_->height = height;
        return recreate_surface(*state_);
    }

    std::expected<void, std::string> Canvas::clear(Color color) {
        if (!state_ || !state_->cairo)
            return std::unexpected("draw: clear on uninitialized canvas");
        set_source_rgba(state_->cairo, color);
        cairo_rectangle(state_->cairo, 0.0, 0.0, static_cast<double>(state_->width), static_cast<double>(state_->height));
        cairo_fill(state_->cairo);
        if (const cairo_status_t status = cairo_status(state_->cairo); status != CAIRO_STATUS_SUCCESS)
            return std::unexpected(cairo_error("draw: clear failed", status));
        return {};
    }

    std::expected<void, std::string> Canvas::fill_rect(int x, int y, unsigned w, unsigned h, Color color) {
        if (!state_ || !state_->cairo)
            return std::unexpected("draw: fill_rect on uninitialized canvas");
        if (w == 0 || h == 0)
            return {};
        set_source_rgba(state_->cairo, color);
        cairo_rectangle(state_->cairo, static_cast<double>(x), static_cast<double>(y), static_cast<double>(w), static_cast<double>(h));
        cairo_fill(state_->cairo);
        if (const cairo_status_t status = cairo_status(state_->cairo); status != CAIRO_STATUS_SUCCESS)
            return std::unexpected(cairo_error("draw: fill_rect failed", status));
        return {};
    }

    std::expected<void, std::string> Canvas::draw_text(int x, int y, unsigned w, unsigned h, unsigned lpad, std::string_view text, Color fg, Color bg, TextAlign align) {
        if (!state_ || !state_->cairo || !state_->layout)
            return std::unexpected("draw: draw_text on uninitialized canvas");
        if (w == 0 || h == 0)
            return {};

        set_source_rgba(state_->cairo, bg);
        cairo_rectangle(state_->cairo, static_cast<double>(x), static_cast<double>(y), static_cast<double>(w), static_cast<double>(h));
        cairo_fill(state_->cairo);

        pango_layout_set_text(state_->layout, text.data(), static_cast<int>(text.size()));
        if (w > lpad)
            pango_layout_set_width(state_->layout, static_cast<int>(w - lpad) * PANGO_SCALE);
        else
            pango_layout_set_width(state_->layout, -1);
        pango_layout_set_ellipsize(state_->layout, PANGO_ELLIPSIZE_END);
        pango_layout_set_wrap(state_->layout, PANGO_WRAP_WORD_CHAR);

        PangoRectangle logical{};
        pango_layout_get_pixel_extents(state_->layout, nullptr, &logical);

        int text_x = x + static_cast<int>(lpad);
        if (align == TextAlign::Center)
            text_x = x + (static_cast<int>(w) - logical.width) / 2 - logical.x;
        else if (align == TextAlign::Right)
            text_x = x + static_cast<int>(w) - logical.width - logical.x - static_cast<int>(lpad);
        const double text_y = static_cast<double>(y) + (static_cast<double>(static_cast<int>(h) - logical.height) / 2.0) - static_cast<double>(logical.y);

        set_source_rgba(state_->cairo, fg);
        cairo_move_to(state_->cairo, static_cast<double>(text_x), text_y);
        pango_cairo_show_layout(state_->cairo, state_->layout);

        if (const cairo_status_t status = cairo_status(state_->cairo); status != CAIRO_STATUS_SUCCESS)
            return std::unexpected(cairo_error("draw: draw_text failed", status));
        return {};
    }

    std::expected<unsigned, std::string> Canvas::text_width(std::string_view text) const {
        if (!state_ || !state_->layout)
            return std::unexpected("draw: text_width on uninitialized canvas");
        pango_layout_set_text(state_->layout, text.data(), static_cast<int>(text.size()));
        pango_layout_set_width(state_->layout, -1);
        int width_px  = 0;
        int height_px = 0;
        pango_layout_get_pixel_size(state_->layout, &width_px, &height_px);
        if (width_px < 0)
            return std::unexpected("draw: negative text width reported");
        return static_cast<unsigned>(width_px);
    }

    std::expected<unsigned, std::string> Canvas::text_height() const {
        if (!state_ || !state_->layout)
            return std::unexpected("draw: text_height on uninitialized canvas");
        pango_layout_set_text(state_->layout, "Mg", -1);
        pango_layout_set_width(state_->layout, -1);
        int width_px  = 0;
        int height_px = 0;
        pango_layout_get_pixel_size(state_->layout, &width_px, &height_px);
        if (height_px <= 0)
            return std::unexpected("draw: invalid text height reported");
        return static_cast<unsigned>(height_px);
    }

    std::expected<void, std::string> Canvas::set_font(std::string_view description) {
        if (!state_ || !state_->layout)
            return std::unexpected("draw: set_font on uninitialized canvas");
        if (description.empty())
            return std::unexpected("draw: font description is empty");

        PangoFontDescription* font_desc = pango_font_description_from_string(std::string(description).c_str());
        if (!font_desc)
            return std::unexpected("draw: invalid font description");

        if (state_->font_desc)
            pango_font_description_free(state_->font_desc);
        state_->font_desc = font_desc;
        pango_layout_set_font_description(state_->layout, state_->font_desc);
        return {};
    }

    std::expected<void, std::string> Canvas::set_target(xcb_window_t target) {
        if (!state_)
            return std::unexpected("draw: set_target on uninitialized canvas");
        if (target == XCB_WINDOW_NONE)
            return std::unexpected("draw: target window is invalid");
        state_->target = target;
        return {};
    }

    std::expected<void, std::string> Canvas::draw_text_rotate_90(int x, int y, unsigned w, unsigned h, unsigned lpad, std::string_view text, Color fg, Color bg, bool clockwise) {
        if (!state_ || !state_->cairo || !state_->layout)
            return std::unexpected("draw: draw_text_rotate_90 on uninitialized canvas");
        if (w == 0 || h == 0)
            return {};

        set_source_rgba(state_->cairo, bg);
        cairo_rectangle(state_->cairo, static_cast<double>(x), static_cast<double>(y), static_cast<double>(w), static_cast<double>(h));
        cairo_fill(state_->cairo);

        pango_layout_set_text(state_->layout, text.data(), static_cast<int>(text.size()));
        if (h > lpad)
            pango_layout_set_width(state_->layout, static_cast<int>(h - lpad) * PANGO_SCALE);
        else
            pango_layout_set_width(state_->layout, -1);
        pango_layout_set_ellipsize(state_->layout, PANGO_ELLIPSIZE_END);
        pango_layout_set_wrap(state_->layout, PANGO_WRAP_WORD_CHAR);

        PangoRectangle logical{};
        pango_layout_get_pixel_extents(state_->layout, nullptr, &logical);

        cairo_save(state_->cairo);
        cairo_rectangle(state_->cairo, static_cast<double>(x), static_cast<double>(y), static_cast<double>(w), static_cast<double>(h));
        cairo_clip(state_->cairo);
        set_source_rgba(state_->cairo, fg);

        const int tx = x + (static_cast<int>(w) - logical.height) / 2 - logical.y;
        const int ty = clockwise ? y + static_cast<int>(lpad) : y + static_cast<int>(h) - static_cast<int>(lpad);
        cairo_translate(state_->cairo, static_cast<double>(tx), static_cast<double>(ty));
        cairo_rotate(state_->cairo, clockwise ? std::numbers::pi / 2.0 : -std::numbers::pi / 2.0);
        pango_cairo_show_layout(state_->cairo, state_->layout);
        cairo_restore(state_->cairo);

        if (const cairo_status_t status = cairo_status(state_->cairo); status != CAIRO_STATUS_SUCCESS)
            return std::unexpected(cairo_error("draw: draw_text_rotate_90 failed", status));
        return {};
    }

    std::expected<void, std::string> Canvas::present(int x, int y, unsigned w, unsigned h) {
        if (!state_ || !state_->cairo || !state_->surface)
            return std::unexpected("draw: present on uninitialized canvas");
        if (w == 0 || h == 0)
            return {};

        cairo_surface_flush(state_->surface);
        xcb_copy_area(state_->conn, state_->pixmap, state_->target, state_->gc, static_cast<std::int16_t>(x), static_cast<std::int16_t>(y), static_cast<std::int16_t>(x),
                      static_cast<std::int16_t>(y), static_cast<std::uint16_t>(w), static_cast<std::uint16_t>(h));
        ScopedPresentFlush flush_scope;
        debug_flush_guard();
        xcb_flush(state_->conn);
        return {};
    }

} // namespace wm::draw
