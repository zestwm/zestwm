/*
 * Minimal WMState builders for unit tests (no zestwm link).
 * Constructs substates from caller-owned monitor/client slots for dispatch/state API checks.
 */
#pragma once

#include "state/wm_state_root.hpp"

namespace wm::test {

    /* Build WMState wired to caller-provided current/lastfocused slots. */
    [[nodiscard]] inline wm::state::WMState make_wm_state(Monitor*& current, Client*& lastfocused) noexcept {
        return wm::state::WMState{
            .monitors =
                wm::state::MonitorState{
                    .current = current,
                },
            .workspaces = {},
            .focus =
                wm::state::FocusState{
                    .lastfocused = lastfocused,
                },
            .layout = {},
        };
    }

} // namespace wm::test
