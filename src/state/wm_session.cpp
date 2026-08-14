/* Session-owned status strings (defined in wm_state.cpp translation unit bundle). */
#include "state/wm_session.hpp"

namespace wm::state {

    namespace {
        WmSession g_session{};
    }

    WmSession& session() noexcept {
        return g_session;
    }

} // namespace wm::state
