/* Client adoption and teardown lifecycle API. */
#pragma once

#include "types.hpp"

namespace wm::x11 {
    struct WindowInfo;
}

namespace wm::state {
    struct WMState;
}

void applyrules(Client* c);
void sync_client_workspace_props(Client* c);
void client_link(Monitor* m, Client* c);
void client_link_stack(Monitor* m, Client* c);
void client_unlink(Monitor* m, Client* c);
void client_unlink_stack(Monitor* m, Client* c);
void adopt_client(Window w, const wm::x11::WindowInfo& wa);
void adopt_client(wm::state::WMState& state, Window w, const wm::x11::WindowInfo& wa);
/* Unlink all Client* aliases then erase from the Window-keyed registry (sole destroy path). */
void release_client(Client* c, int destroyed);
void release_client(wm::state::WMState& state, Client* c, int destroyed);

/* Suppress app-driven fullscreen for modal/transient windows (also used from clientmessage). */
int client_wm_should_suppress_application_fullscreen(Client* c);
