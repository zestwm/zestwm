/* Monitor allocation, teardown, and RandR geometry sync API. */
#pragma once

#include "state/wm_state_root.hpp"
#include "types.hpp"

Monitor* createmon(void);
void     monitor_cleanup(Monitor* mon);
/* Reconcile monitor chain with RandR; mutates `monitors` head/current refs in place. */
int updategeom(wm::state::MonitorState& monitors);
