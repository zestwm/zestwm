/* Client keyboard focus and X11 stacking order on a monitor. */
#pragma once

#include "types.hpp"

struct X11Backend;

void focus(Client* c);
void restack(Monitor* m, X11Backend& backend);
void restack(Monitor* m);
