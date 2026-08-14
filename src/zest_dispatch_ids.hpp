/*
 * Shared `_NET_ZEST_DISPATCH` command IDs.
 *
 * These numeric values are the IPC wire contract between `zestwm` (WM side)
 * and `zestctl` (CLI side). They must stay identical across both processes.
 * Defined once here so changes can never desync the two sides.
 */
#pragma once

enum {
    ZEST_DISPATCH_FOCUSMON           = 3,
    ZEST_DISPATCH_KILLCLIENT         = 4,
    ZEST_DISPATCH_TOGGLEFLOATING     = 5,
    ZEST_DISPATCH_TOGGLEFULLSCREEN   = 6,
    ZEST_DISPATCH_RELOAD             = 8,
    ZEST_DISPATCH_SETLAYOUT          = 9,
    ZEST_DISPATCH_VIEW_WORKSPACE_ID  = 10,
    ZEST_DISPATCH_MOVETOWORKSPACE_ID = 11,
    ZEST_DISPATCH_SPLITRATIO_DELTA   = 12,
    ZEST_DISPATCH_SPLITRATIO_EXACT   = 13,
    ZEST_DISPATCH_SWAPSPLIT          = 14,
    ZEST_DISPATCH_TOGGLESPLIT        = 15,
    ZEST_DISPATCH_PRESELECT          = 16,
    ZEST_DISPATCH_MOVETOROOT         = 17,
    ZEST_DISPATCH_FOCUSURGENT        = 18,
    ZEST_DISPATCH_FOCUSWINDOW        = 19,
    ZEST_DISPATCH_QUIT               = 20,
    ZEST_DISPATCH_TOGGLE_SPECIAL     = 21,
    ZEST_DISPATCH_MOVETOSPECIAL      = 22
};
