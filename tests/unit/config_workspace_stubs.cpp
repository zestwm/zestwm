/* Link stubs for config-workspace unit test (avoids pulling full WM runtime). */
#include "intern.hpp"
#include "monitor_select.hpp"
#include "state/runtime_authority.hpp"

static wm::state::WMRuntimeAuthority g_unit_runtime{};

Monitor*                             monitor_select_resolve(std::string_view) {
    return nullptr;
}

int client_is_visible_on_monitor(const Client*, const Monitor*) {
    return 0;
}

namespace wm::state {

    WMRuntimeAuthority& runtime_authority() noexcept {
        return g_unit_runtime;
    }

    void     install_runtime_authority(WMRuntimeAuthority*) noexcept {}

    Client*& lastfocused_slot() noexcept {
        return g_unit_runtime.ref_last_focused();
    }

} /* namespace wm::state */
