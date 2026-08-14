/*
 * Single runtime authority for owned monitors, selected monitor, last-focused client,
 * and Window-keyed Client ownership.
 *
 * Default storage lives in wm_state.cpp; `main()` may install stack-owned authority via
 * `install_runtime_authority`.
 */
#pragma once

#include "types.hpp"

#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace wm::state {

    struct WMRuntimeAuthority {
        /* Sole owner of monitors (RandR order). */
        std::vector<std::unique_ptr<Monitor>> monitors;
        Monitor*                              current_monitor = nullptr;
        Client*                               last_focused    = nullptr;
        /* Sole owner of managed clients; observers elsewhere hold Client*. */
        std::unordered_map<Window, std::unique_ptr<Client>> clients_by_win;

        [[nodiscard]] Monitor*&                             ref_current_monitor() noexcept {
            return current_monitor;
        }
        [[nodiscard]] Client*& ref_last_focused() noexcept {
            return last_focused;
        }

        /* First monitor in RandR order, or nullptr when none. */
        [[nodiscard]] Monitor* first_monitor() const noexcept {
            return monitors.empty() ? nullptr : monitors.front().get();
        }

        [[nodiscard]] Client* find_client(Window w) const noexcept {
            const auto it = clients_by_win.find(w);
            return it != clients_by_win.end() ? it->second.get() : nullptr;
        }

        Client* register_client(std::unique_ptr<Client> client) {
            if (!client)
                return nullptr;
            const Window w            = client->win;
            const auto [it, inserted] = clients_by_win.emplace(w, std::move(client));
            if (!inserted)
                return nullptr;
            return it->second.get();
        }

        void erase_client(Window w) noexcept {
            clients_by_win.erase(w);
        }
    };

    [[nodiscard]] WMRuntimeAuthority& runtime_authority() noexcept;
    void                              install_runtime_authority(WMRuntimeAuthority* authority) noexcept;

    /* Borrowed Monitor* list in current RandR order (empty when no monitors). */
    [[nodiscard]] inline std::vector<Monitor*> all_monitors() {
        std::vector<Monitor*> out;
        auto&                 owners = runtime_authority().monitors;
        out.reserve(owners.size());
        for (auto& owned : owners) {
            if (owned)
                out.push_back(owned.get());
        }
        return out;
    }

    [[nodiscard]] inline Monitor* first_monitor() noexcept {
        return runtime_authority().first_monitor();
    }

    [[nodiscard]] inline std::size_t monitor_count() noexcept {
        return runtime_authority().monitors.size();
    }

    /* Compat alias: first monitor (was intrusive-list head). */
    [[nodiscard]] inline Monitor* mons_slot() noexcept {
        return first_monitor();
    }

    [[nodiscard]] Client*&       lastfocused_slot() noexcept;

    [[nodiscard]] inline Client* find_client(Window w) noexcept {
        return runtime_authority().find_client(w);
    }

} // namespace wm::state
