/*
 * Runtime dispatcher boundary implementation.
 * Provides centralized event/command queueing and re-entrancy guards around explicit WMState dispatch boundaries.
 *
 * Threading: only this thread (main event loop) may mutate WMState; other threads must queue work, never touch WMState.
 */
#include "dispatch/runtime_dispatch.hpp"

#include "dispatch/runtime_dispatch_commands.hpp"
#include "dispatch/runtime_dispatch_xcb_event.hpp"
#include "dispatch/xcb_handlers.hpp"
#include "x11/reply_ptr.hpp"

#include <cassert>
#include <vector>

namespace wm::dispatch::runtime {

    namespace {
        /* Dispatcher re-entrancy guard and deferred command queue.
 * Prevents recursive mutation-in-mutation flows by draining queued commands after active dispatch completes. */
        bool                                          g_command_dispatching = false;
        std::vector<RuntimeCommandPayload>            g_pending_payloads;
        std::vector<RuntimeCommandId>                 g_pending_ids;
        bool                                          g_event_dispatching = false;
        std::vector<XcbReplyPtr<xcb_generic_event_t>> g_pending_events;
    } // namespace

    void dispatch_event(state::WMState& state, X11Backend& backend, xcb_generic_event_t* ev) {
        /* Re-entrancy guard: queue nested events and drain with the same WMState + backend. */
        if (g_event_dispatching) {
            auto queued = xcb_clone_generic_event(ev);
            if (!queued)
                return;
            g_pending_events.push_back(std::move(queued));
            return;
        }
        assert(!g_event_dispatching);
        g_event_dispatching = true;
        ::dispatch_event(ev, state, backend);
        for (std::size_t i = 0; i < g_pending_events.size(); ++i)
            ::dispatch_event(g_pending_events[i].get(), state, backend);
        g_pending_events.clear();
        g_event_dispatching = false;
        assert(g_pending_events.empty());
        assert(!g_event_dispatching);
    }

    void dispatch_command(state::WMState& state, const RuntimeCommandId id, const RuntimeCommandPayload& payload) {
        /* Re-entrancy guard: queue nested commands and drain through runtime_dispatch_commands. */
        if (g_command_dispatching) {
            g_pending_ids.push_back(id);
            g_pending_payloads.push_back(payload);
            assert(g_pending_ids.size() == g_pending_payloads.size());
            return;
        }
        assert(!g_command_dispatching);
        g_command_dispatching = true;
        dispatch_command_execute(state, id, payload);
        assert(g_pending_ids.size() == g_pending_payloads.size());
        for (std::size_t i = 0; i < g_pending_ids.size(); ++i) {
            dispatch_command_execute(state, g_pending_ids[i], g_pending_payloads[i]);
        }
        g_pending_ids.clear();
        g_pending_payloads.clear();
        g_command_dispatching = false;
        assert(g_pending_ids.empty());
        assert(g_pending_payloads.empty());
        assert(!g_command_dispatching);
    }

} // namespace wm::dispatch::runtime
