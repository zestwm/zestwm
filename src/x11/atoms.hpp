/*
 * Atom index registry and read-only slot accessors used by the X11 layer.
 *
 * Why this file exists:
 * - Keep atom-slot order centralized so every translation unit indexes
 *   the same runtime atom arrays with identical semantics.
 * - Avoid string-based lookups in hot/runtime paths after startup intern.
 * - Expose net_atom/wm_atom helpers so call sites do not index arrays directly
 *   while ownership still lives in wm_state globals.
 *
 * Contract:
 * - Numeric order is ABI-like internal contract for any code storing atoms
 *   in arrays indexed by these enums.
 * - Append-only policy preferred; reordering breaks existing slot mapping.
 */
#pragma once

#include "x11/backend.hpp"

#include <cstdint>

enum AtomIndex : std::uint8_t {
    NetSupported,
    NetWMName,
    NetWMState,
    NetWMCheck,
    NetWMFullscreen,
    NetActiveWindow,
    NetWMWindowType,
    NetWMWindowTypeDialog,
    NetWMWindowTypeDock,
    NetWMWindowTypeSplash,
    NetWMWindowTypeUtility,
    NetWMStrutPartial,
    NetWMDesktop,
    NetClientList,
    NetWMWindowsOpacity,
    NetZestwmState,
    NetZestLayouts,
    NetZestLayoutList,
    NetZestTreeState,
    NetZestSelectionState,
    NetZestDispatch,
    /* CARDINAL hidden-id payload for `special` dispatch path (phase-4 bridge metadata). */
    NetZestSpecialDispatchHiddenId,
    /* UTF-8 per-monitor special overlay visibility (`savezestspecialoverlaystate`, see wm_state.cpp). */
    NetZestSpecialOverlayState,
    /* UTF-8 `tag<TAB>hidden_id` rows for special workspaces (debug/export bridge for hidden workspace migration). */
    NetZestSpecialHiddenIdState,
    /* Root WINDOW list of managed clients with `Client::isfloating` (zestctl `floating:`). */
    NetZestFloatingClients,
    NetNumberOfDesktops,
    NetCurrentDesktop,
    NetDesktopNames,
    NetLast
};

/* WM_PROTOCOLS-family slots used by ICCCM interaction paths. */
enum WmAtomIndex : std::uint8_t {
    WMProtocols,
    WMDelete,
    WMState,
    WMTakeFocus,
    WMLast
};

extern Atom wmatom[WMLast];
extern Atom netatom[NetLast];
extern Atom utf8_atom;

namespace wm::x11 {

    /* Return interned EWMH/zest atom for a stable AtomIndex slot. */
    [[nodiscard]] inline Atom net_atom(AtomIndex index) noexcept {
        return netatom[index];
    }

    /* Return interned ICCCM protocol atom for a stable WmAtomIndex slot. */
    [[nodiscard]] inline Atom wm_atom(WmAtomIndex index) noexcept {
        return wmatom[index];
    }

    /* Return interned UTF8_STRING atom used by text root properties. */
    [[nodiscard]] inline Atom utf8_string_atom() noexcept {
        return utf8_atom;
    }

} // namespace wm::x11
