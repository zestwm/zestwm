/* Client geometry, arrange, and monitor hit-test API. */
#pragma once

#include "types.hpp"

void tiling_gaps_for_monitor_workspace(Monitor* m, unsigned* outer, unsigned* inner_split);
int  applysizehints(Client* c, int* x, int* y, int* w, int* h, int interact);
void arrangemon(Monitor* m);
void configure(Client* c);
void resizeclient(Client* c, int x, int y, int w, int h);
void arrange_docks(Monitor* m);
void resizeclient_fullscreen_target(Client* c);
void showhide(Client* c);
