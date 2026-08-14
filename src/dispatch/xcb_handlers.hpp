/* XCB event dispatch API extracted from zestwm.cpp. */
#pragma once

#include "state/wm_state_root.hpp"
#include "x11/backend.hpp"

#include <cstdint>
#include <xcb/xcb.h>

/* Leaf XCB event switch; `backend` is threaded into every *_with_ctx handler. */
void dispatch_event(xcb_generic_event_t* ev, wm::state::WMState& state, X11Backend& backend);
void grabkeys(void);
void xcb_handlers_set_randr_event_base(uint8_t base);
