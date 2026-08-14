---
weight: 7
title: Window Rules
---

`zestwm` now uses only niri-style `window-rule { ... }` blocks.

## Syntax

```ini
window-rule {
    match app-id="^firefox$"
    exclude app-id="^firefox$" title="^Media viewer$"

    open-floating = true
    size = 80% 50%
    move = 10% 24
    center = true
    open-fullscreen = false
    open-on-output = "DP-1"
    open-on-workspace = special:magic silent
}
```

## Matching

- `match ...` directives are OR-combined.
- `exclude ...` directives are OR-combined.
- Matchers inside one directive are AND-combined.
- Rule applies when: `(any match) && (no exclude)`.

Supported matcher keys:

- `title=<regex>`
- `app-id=<regex>` (X11 `WM_CLASS` class string)
- `is-active=<bool>`
- `is-focused=<bool>`
- `is-floating=<bool>`
- `is-urgent=<bool>`

Rules are split into two categories of parameters: **props** and **effects**. Props are the `match:` parts used to decide if a window gets the rule. Effects are what is applied.

## Supported Properties

Supported properties in `window-rule` blocks:

- `open-floating = true|false`
- `open-focused = true|false`
- `open-fullscreen = true|false`
- `open-maximized = true|false`
- `size = <width> <height>` (`px` or `%`)
- `move = <x> <y>` (`px` or `%`, percent relative to monitor work area)
- `center = true|false`
- `open-on-output = "<monitor-selector>"`
- `open-on-workspace = <workspace-token> [silent]`

`open-on-workspace` accepts numeric id, configured workspace name, or `special:<tag>` (`special:` empty tag allowed).

## Ordering

- Rules are evaluated top-to-bottom in config order.
- For overlapping effects, later matching rules override earlier ones.

## Notes

- Legacy `windowrule = ...` and `windowrule { ... }` are removed.
- Unknown matcher keys and unknown properties are parser errors.
- Compositor-only visual effects (blur, shadows, layer rules, etc.) are out of scope in X11 WM core.
