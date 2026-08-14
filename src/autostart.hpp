/* XDG autostart runner declarations (system + user .desktop entries). */
#pragma once

#include <xcb/xcb.h>

/* Spawn detached child that resolves and launches eligible XDG autostart entries. */
void run_xdg_autostart(xcb_connection_t* xc);
