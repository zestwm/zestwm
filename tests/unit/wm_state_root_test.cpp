/*
 * Unit tests for WMState root / WorkspaceTree validation (header-only, no full WM link).
 */
#include "state/wm_state_root.hpp"
#include "tests/unit/state_fixtures.hpp"

#include <cassert>
#include <cstdint>
#include <sstream>

namespace wm::state {
    namespace {
        WMRuntimeAuthority g_test_authority{};
    }
    WMRuntimeAuthority& runtime_authority() noexcept {
        return g_test_authority;
    }
    void     install_runtime_authority(WMRuntimeAuthority*) noexcept {}
    Client*& lastfocused_slot() noexcept {
        return g_test_authority.ref_last_focused();
    }
} // namespace wm::state

int main() {
    wm::state::WorkspaceTree wt;
    assert(wt.is_valid_ref(WorkspaceRef::normal(1)));
    assert(!wt.is_valid_ref(WorkspaceRef::normal(0)));
    assert(!wt.is_valid_ref(WorkspaceRef::unset()));
    assert(!wt.is_valid_ref(WorkspaceRef::special("")));
    assert(wt.is_valid_ref(WorkspaceRef::special("a")));

    Monitor* cur = nullptr;
    Client*  lf  = nullptr;
    {
        const wm::state::WMState s = wm::test::make_wm_state(cur, lf);
        assert(s.monitors.current == nullptr);
        assert(s.focus.lastfocused == nullptr);
    }

    auto* const fake_m = reinterpret_cast<Monitor*>(static_cast<std::uintptr_t>(0x8U));
    auto* const fake_c = reinterpret_cast<Client*>(static_cast<std::uintptr_t>(0x10U));
    cur                = fake_m;
    lf                 = fake_c;
    {
        const wm::state::WMState s = wm::test::make_wm_state(cur, lf);
#ifndef NDEBUG
        assert(wm::state::wm_state_checksum(s) != 0ULL);
        std::ostringstream oss;
        wm::state::debug_dump_wm_state(s, oss);
        assert(oss.str().find("WMState{") != std::string::npos);
#endif
        assert(s.monitors.current == fake_m);
        assert(wm::state::monitor_or_fallback(s) == fake_m);
    }

    Monitor                  stack_mon{};
    Monitor*                 mc2 = &stack_mon;
    Client*                  lf2 = nullptr;
    const wm::state::WMState st2 = wm::test::make_wm_state(mc2, lf2);
    assert(st2.monitors.current == &stack_mon);
    assert(wm::state::monitor_or_fallback(st2) == &stack_mon);

    return 0;
}
