/*
 * Runtime WM state root container.
 * Provides a single explicit owner/view of mutable runtime domains without introducing new global authority copies.
 */
#pragma once

#include "state/runtime_authority.hpp"
#include "workspace_id.hpp"

#ifndef NDEBUG
#include <cstdint>
#include <ostream>
#endif

namespace wm::state {

    /* Monitor runtime substate view.
     * References active `WMRuntimeAuthority` (default in wm_state.cpp or installed from `main`). */
    struct MonitorState {
        Monitor*&              current;

        [[nodiscard]] Monitor* first() const noexcept {
            return first_monitor();
        }
    };

    struct WorkspaceTree {
        [[nodiscard]] bool is_valid_ref(const WorkspaceRef& ref) const noexcept {
            if (ref.is_normal())
                return ref.normal_id >= kWorkspaceIdMin;
            if (ref.is_special())
                return !ref.special_tag.empty();
            return false;
        }
    };

    struct FocusState {
        Client*& lastfocused;
    };

    struct LayoutState {};

    struct WMState {
        MonitorState  monitors;
        WorkspaceTree workspaces;
        FocusState    focus;
        LayoutState   layout;
    };

    [[nodiscard]] inline WMState build_runtime_state_root() noexcept {
        WMRuntimeAuthority& a = runtime_authority();
        return WMState{
            .monitors =
                MonitorState{
                    .current = a.ref_current_monitor(),
                },
            .workspaces = WorkspaceTree{},
            .focus =
                FocusState{
                    .lastfocused = a.ref_last_focused(),
                },
            .layout = LayoutState{},
        };
    }

    [[nodiscard]] inline Monitor* monitor_or_fallback(Monitor* current, Monitor* head) noexcept {
        return current ? current : head;
    }

    [[nodiscard]] inline Monitor* monitor_or_fallback(const MonitorState& monitors) noexcept {
        return monitor_or_fallback(monitors.current, monitors.first());
    }

    [[nodiscard]] inline Monitor* monitor_or_fallback(const WMState& state) noexcept {
        return monitor_or_fallback(state.monitors);
    }

#ifndef NDEBUG
    [[nodiscard]] inline std::uint64_t wm_state_checksum(const WMState& state) noexcept {
        std::uint64_t h   = 14695981039346656037ULL;
        auto          mix = [&h](std::uintptr_t p) noexcept {
            h ^= static_cast<std::uint64_t>(p);
            h *= 1099511628211ULL;
        };
        mix(reinterpret_cast<std::uintptr_t>(state.monitors.first()));
        mix(reinterpret_cast<std::uintptr_t>(state.monitors.current));
        mix(reinterpret_cast<std::uintptr_t>(state.focus.lastfocused));
        return h;
    }

    inline void debug_dump_wm_state(const WMState& state, std::ostream& os) {
        os << "WMState{first=" << static_cast<void*>(state.monitors.first()) << " current=" << static_cast<void*>(state.monitors.current)
           << " lastfocused=" << static_cast<void*>(state.focus.lastfocused) << '}';
    }
#endif

} // namespace wm::state
