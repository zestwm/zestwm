---
title: Binds
---

## Basic

```ini
binds {
    MOD+KEY [repeat=true|false] [cooldown-ms=<int>] { dispatcher [args...]; }
}
```

## Niri-Style Binds Block

zestwm uses a niri-style `binds {}` block syntax:

```ini
binds {
    Mod+Left { movefocus l; }
    Mod+T repeat=false { spawn alacritty; }
    Mod+J cooldown-ms=150 { cyclefocus 1; }
}
```

Format:

```ini
binds {
    HOTKEY [repeat=true|false] [cooldown-ms=<int>] { DISPATCHER [PARAMS...]; }
}
```

Rules:

- `HOTKEY` is `MOD+KEY` (last `+` segment is key token).
- Action body must be one dispatcher per line, inside `{ ... }`.
- Trailing `;` inside the action body is optional.
- `repeat` defaults to `true` for `binds {}` entries (niri-like behavior).
- `cooldown-ms` defaults to `0` (disabled).
- compatibility properties accepted as no-op on X11: `allow-when-locked`, `allow-inhibiting`, `hotkey-overlay-title`.
- Unknown per-bind properties are rejected for that entry with a warning.

Compatibility note:

- `binds {}` is the canonical syntax for key, mouse click, wheel, and touchpad binds.

### Pointer Hotkeys in `binds {}`

Niri-style pointer key tokens are accepted in `HOTKEY`:

- Mouse clicks: `MouseLeft`, `MouseRight`, `MouseMiddle`, `MouseForward`, `MouseBack`
- Wheel ticks: `WheelScrollUp`, `WheelScrollDown`, `WheelScrollLeft`, `WheelScrollRight`
- Touchpad ticks: `TouchpadScrollUp`, `TouchpadScrollDown`, `TouchpadScrollLeft`, `TouchpadScrollRight`

Example:

```ini
binds {
    Mod+MouseLeft { killclient; }
    Mod+WheelScrollDown cooldown-ms=150 { cyclefocus 1; }
    Mod+TouchpadScrollUp { cyclefocus -1; }
}
```

On X11, touchpad scroll tokens currently map to wheel-button compatibility.

## Syntax Rules

### MODS

Modifier tokens are case-insensitive and can be separated by `+`, `_`, `|`, or spaces.

Supported tokens:

- `SUPER`, `MOD4`, `WIN`
- `ALT`, `MOD1`
- `SHIFT`
- `CTRL`, `CONTROL`
- `MOD2`, `MOD3`, `MOD5`
- `AltGr` (alias), `ISO_Level3_Shift` (mapped to `MOD5`)
- `ISO_Level5_Shift` (mapped to `MOD3` on X11 backend)
- `MOD` (mapped to `SUPER` on zestwm/X11)
- `0`, `NONE`

No modifiers:

```ini
binds {
    Print { spawn grim; }
}
```

AltGr note:

- Prefer `AltGr` in user config for readability.
- `AltGr` and `ISO_Level3_Shift` are equivalent in zestwm (`Mod5` mapping).

### KEY

Supported key forms:

- Single character (`a`, `,`, `;`)
- Named keysyms (`Return`, `Escape`, `Tab`, `Left`, `Right`, `F1`..`F12`, `XF86Audio*`, `XF86MonBrightness*`, ...)
- Raw keycode: `code:N` (`8..255`)
- Numeric keysym literals: `0x...` or `#...`

### DISPATCHER and PARAMS

Dispatcher is case-sensitive and should be lowercase.

Common dispatchers:

- `spawn` / `exec`
- `quit`
- `cyclefocus`
- `cyclenext` / `cycleprev` (wrap monitor ring; these wrappers intentionally map to the opposite `cyclefocus` direction for typical Alt+Tab order on this client list). Skips docks, `neverfocus`, inactive **groupmode** tabs; `cyclegroup` for tabs inside a group.
- `splitratio`
- `killclient` / `killactive`
- `setlayout` / `cyclelayout`
- `togglefloating`
- `togglefullscreen` / `fullscreen`
- `focusmonitor` / `movetomonitor`
- `movegroup` / `moveoutofgroup` / `movewindoworgroup` / `sendtogroup` / `groupmode`
- `movefocus` / `swapwindow`
- `layoutmsg`
- `view` / `workspace` (numeric id, configured workspace name, or **`special:<tag>`**)
- `movetoworkspace` / `movetoworkspacesilent` (same token forms as `view`; **`special:`** is only valid on move/view/toggle-special dispatchers)
- `togglespecialworkspace` (tag, e.g. `magic` or `special:magic`)

Parameter parsing depends on dispatcher:

- `spawn` / `exec`: shell or direct argv parsing
- `splitratio`: float
- `groupmode`: toggle only (`groupmode`, `groupmode toggle`, or `groupmode -1`)
- `movefocus` / `swapwindow`: first char direction (`l/r/u/d`)
- `movewindoworgroup`: first char direction (`l/r/u/d`)
- `movetoworkspace`: workspace token (`1`..`31`, registry **name**, or **`special:<tag>`**), then optional trailing ` silent` (case-insensitive). For **normal** workspaces: without ` silent`, the WM **views** the target workspace after the move; with ` silent`, or when using `movetoworkspacesilent`, the **numeric** active workspace is unchanged. For **`special:`** moves: without ` silent`, the special overlay for that tag is **opened** (or switched) so the moved client is visible; with ` silent`, the overlay state is left unchanged.
- `view` / `workspace`: same token forms; **`special:<tag>`** toggles that special overlay on the focused monitor. Optional trailing ` silent` is accepted and ignored for `view` / `workspace`.
- `togglespecialworkspace`: takes a tag (`magic`), `special:magic`, or **`special:`** alone for the **default** scratchpad (empty tag, after `workspace = special:`); toggles that overlay on the focused monitor.
- many other dispatchers: integer parameter

Group dispatchers:

- `moveoutofgroup`: detach selected client from its current group into a new split sibling (group stays intact when it has other members).
- `movewindoworgroup <dir>`:
  - if directional target is a group, move selected client into that group;
  - else if selected client is in a group, behave like `moveoutofgroup`;
  - otherwise fallback to directional `swapwindow`.

## Examples

```ini
binds {
    ALT+Shift+Return { spawn alacritty; }
    ALT+p repeat=false { spawn notify-send "release"; }
    XF86AudioRaiseVolume { spawn wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%+; }
    ALT+MouseLeft { movewindow; }
    ALT+MouseRight { resizewindow; }
}
```


