/*
 * Selected-monitor switching: unfocus old selection then assign explicit slot.
 */

#include "context/monitor_context.hpp"
#include "client/client_props.hpp"

bool switch_selected_monitor(Monitor*& current_ref, Monitor* next, const bool unfocus_old) noexcept {
    if (!next || next == current_ref)
        return false;
    if (unfocus_old && current_ref && current_ref->sel)
        unfocus(current_ref->sel, 0);
    current_ref = next;
    return true;
}
