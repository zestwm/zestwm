/* Client property reads and EWMH / WM-hints sync helpers. */
#pragma once

#include "types.hpp"

void apply_client_workspace_border_policy(Client* c);
Atom getatomprop(Client* c, Atom prop);
long getstate(Window w);
void grabbuttons(Client* c, int focused);
void set_client_window_opacity(Client* c, double opacity);
void setfocus(Client* c);
void seturgent(Client* c, int urg);
void unfocus(Client* c, int setfocus);
void updateclientlist(void);
/* Rewrite root `_NET_ZEST_FLOATING_CLIENTS` from live `Client::isfloating`. */
void    updatefloatingclientlist(void);
void    updatesizehints(Client* c);
void    updatestatus(void);
void    updatetitle(Client* c);
void    updatewindowtype(Client* c);
void    updatewmhints(Client* c);
Client* wintoclient(Window w);
