/* Session-owned bar/status strings (replaces global stext/broken buffers). */
#pragma once

#include <string>
#include <string_view>

namespace wm::state {

    struct WmSession {
        std::string                       status_bar_text;
        static constexpr std::string_view kBrokenClientLabel{"broken"};
    };

    /* Single session instance; X11 connection globals remain in wm_state until backend threading lands. */
    WmSession& session() noexcept;

} // namespace wm::state
