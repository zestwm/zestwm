/*
 * Runtime dispatcher command execution helpers.
 * Isolates command-to-action routing logic from queue/re-entrancy plumbing.
 */
#pragma once

#include "dispatch/runtime_dispatch.hpp"

namespace wm::dispatch::runtime {

    /* Execute one decoded runtime command against explicit WMState.
 * This function performs command routing only; queue/re-entrancy is handled by runtime_dispatch.cpp. */
    void dispatch_command_execute(state::WMState& state, RuntimeCommandId id, const RuntimeCommandPayload& payload);

} // namespace wm::dispatch::runtime
