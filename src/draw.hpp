#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>

struct xcb_connection_t;
struct xcb_screen_t;
using xcb_window_t = std::uint32_t;

namespace wm::draw {

    struct Color final {
        double                               r{0.0};
        double                               g{0.0};
        double                               b{0.0};
        double                               a{1.0};

        [[nodiscard]] static constexpr Color from_rgba(double red, double green, double blue, double alpha) noexcept {
            return Color{red, green, blue, alpha};
        }
    };

    enum class TextAlign : std::uint8_t {
        Left,
        Center,
        Right,
    };

    class Canvas final {
      public:
        /* Build a drawing canvas for a target X window with offscreen backing storage. */
        [[nodiscard]] static std::expected<std::unique_ptr<Canvas>, std::string> create(xcb_connection_t& conn, xcb_screen_t& screen, xcb_window_t target, unsigned width,
                                                                                        unsigned height);

        ~Canvas();

        Canvas(const Canvas&)            = delete;
        Canvas& operator=(const Canvas&) = delete;
        Canvas(Canvas&&)                 = delete;
        Canvas& operator=(Canvas&&)      = delete;

        /* Recreate backing surfaces/pixmap for the new dimensions. */
        [[nodiscard]] std::expected<void, std::string> resize(unsigned width, unsigned height);
        /* Fill the whole canvas with a solid color. */
        [[nodiscard]] std::expected<void, std::string> clear(Color color);
        /* Fill a rectangle in canvas coordinates. */
        [[nodiscard]] std::expected<void, std::string> fill_rect(int x, int y, unsigned w, unsigned h, Color color);
        /* Draw clipped text with background fill and optional horizontal alignment. */
        [[nodiscard]] std::expected<void, std::string> draw_text(int x, int y, unsigned w, unsigned h, unsigned lpad, std::string_view text, Color fg, Color bg,
                                                                 TextAlign align = TextAlign::Left);
        /* Draw text rotated by 90 degrees inside clipped bounds. */
        [[nodiscard]] std::expected<void, std::string> draw_text_rotate_90(int x, int y, unsigned w, unsigned h, unsigned lpad, std::string_view text, Color fg, Color bg,
                                                                           bool clockwise);

        /* Return rendered text width in pixels using current font settings. */
        [[nodiscard]] std::expected<unsigned, std::string> text_width(std::string_view text) const;
        /* Return current font pixel height. */
        [[nodiscard]] std::expected<unsigned, std::string> text_height() const;
        /* Update the active font description (Pango format string). */
        [[nodiscard]] std::expected<void, std::string> set_font(std::string_view description);
        /* Change the current presentation target window. */
        [[nodiscard]] std::expected<void, std::string> set_target(xcb_window_t target);
        /* Copy the offscreen pixmap to target window and flush exactly once. */
        [[nodiscard]] std::expected<void, std::string> present(int x, int y, unsigned w, unsigned h);

      private:
        struct CanvasState;
        explicit Canvas(std::unique_ptr<CanvasState> state);

        std::unique_ptr<CanvasState> state_;
    };

} // namespace wm::draw
