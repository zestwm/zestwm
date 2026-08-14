# Workspace Configuration (Current Support)

This document describes the workspace configuration syntax currently supported by this `zestwm` fork.

## Supported syntax

Top-level directive:

```ini
workspace = <name>
workspace = <id>, <name>
workspace = <id>, <name>, <rule>, ...
workspace = <name>, <rule>, ...
workspace = name:<name>, <rule>, ...
```

### Basic semantics (registry)

- `workspace = <name>`
  - Ensures a workspace with that display name exists (appends if new). Re-declaring the same name is a no-op.
- `workspace = <id>`
  - Ensures the workspace with that id exists (fills default numeric names for gaps).
- `workspace = <id>, <name>`
  - Same as before: sets display name for that id.
- `workspace = name:<name>` (single line)
  - Ensures a workspace with that name exists (append if missing).

### Dynamic workspace selectors (subset)

Workspace lines may target **existing** workspaces via bracket selectors instead of a fixed id/name:

```ini
workspace = r[1-3], gapsin:0
workspace = w[t1], gapsout:40
workspace = f[-1], border:false
workspace = r[2-4] w[tv1], gapsin:8
```

Supported selector clauses (AND across clauses on one line):

| Clause | Meaning |
| --- | --- |
| `r[A-B]` | Workspace id in inclusive range |
| `w[(flags)N]` / `w[(flags)A-B]` | Window count on workspace (`t` tiled, `f` floating, `v` visible only) |
| `f[-1]` / `f[0]` | No fullscreen client / at least one fullscreen client on workspace |
| `s[bool]` | Special workspace flag (normal desktops match `s[false]`) |
| `n[true]` / `n[s:prefix]` / `n[e:suffix]` | Named workspace checks |
| `m[monitor]` | Matches when arranging/viewing on the resolved monitor |

Rules merge at runtime when a workspace matches (later lines win). `p`/`g` window-count flags are not supported yet.

### Workspace policy rules (subset)

After the workspace selector, tokens are either a **plain display name** (only allowed once, and only for the `workspace = <id>, ...` form) or **`key:value`** rules.

Implemented keys:

| Rule | Description |
| --- | --- |
| `gapsin:<n>` | Inner gap between tiled splits (unsigned). |
| `gapsout:<n>` | Outer inset from the monitor work area. If omitted but `gapsin` is set, outer matches `gapsin` for that workspace. |
| `bordersize:<n>` | Border width for clients on this workspace. |
| `border:<bool>` | `true` / `false` — when `false`, tiled clients use zero border width on this workspace. |
| `monitor:<selector>` | Workspace monitor selector for **viewing** this workspace. Accepts numeric id (`Monitor.num`) or RandR output name (for example `DP-1`). |
| `default:<bool>` | Marks this workspace as default for its bound monitor (`monitor:<index>`). On monitor focus switch, default workspace is selected if configured. |
| `persistent:<bool>` | `false` drops saved tree state when the workspace becomes empty; `true` (or unset) keeps state. |
| `defaultName:<name>` | Fallback display name used when the workspace still has its numeric/default-generated name. |
| `layout:<name>` | When this workspace is shown, set primary layout slot to `tree`, `dwindle`, or `monocle` if the name matches built-in layouts. |
| `on-created-empty:<command>` | When the workspace is **viewed** with zero clients and the hook is armed, runs `/bin/sh -c '<command>'` once. The hook rearms when the last client leaves that workspace. |

Unknown `key:value` pairs produce a parser warning and are skipped.

### Examples

```ini
workspace = web
workspace = 3, code
workspace = 3, gapsin:0, gapsout:0, border:false
workspace = name:gaming, monitor:DP-1, layout:monocle
workspace = 2, monitor:0, default:true
workspace = 7, defaultName:media
workspace = 9, persistent:false
workspace = 5, on-created-empty:foot
```

## Interaction with rules

> [!WARNING]
> Rule evaluation is top to bottom. Keep specific rules after generic ones.

Window rule interaction:

- See `docs/config/window-rules.md` for niri-style `window-rule { ... }` blocks.
- `open-on-workspace` / `open-on-output` inside those rules follow the same workspace/monitor selector resolution rules.
- Workspace-level `monitor:<...>` (from `workspace = ...`) accepts numeric id or output name and is resolved before regular focus/update flow.

When both apply:

- `window-rule ... open-on-workspace` decides where a new window is assigned.
- `workspace ... monitor:<id>` decides which monitor is selected when that workspace is viewed.
- They operate on different moments (window-open assignment vs workspace-view routing), so they can be combined safely.

## Special workspaces (`special:<tag>`)

**Scratchpad** special workspaces are overlays on a monitor: each `(monitor, tag)` has its own tiling tree; clients keep `WorkspaceRef::special` when the overlay is hidden. They are **not** the same as numbered desktops.

- **Identity:** `WorkspaceRef` (`src/workspace_ref.hpp`) is **unset**, a **normal** `WorkspaceId`, or **special** with a string **tag** after the `special:` prefix. The tag may be **empty** (`special:` default scratchpad); you should still declare it with `workspace = special:` if you rely on rules referencing that form.
- **Registry:** `special_workspace_registry` holds up to **97** tags, metadata (e.g. per-tag `dim_special`), and is rebuilt whenever the workspace registry is reset (e.g. `wmconf_load` / full config parse).

### End-to-end usage (config)

1. **Declare** each tag you use (so the registry and `workspace = special:…, …` rules apply):

   ```ini
   workspace = special:scratch
   workspace = special:notes, dim_special:0.25
   ```

2. **Place windows** with a window rule or a bind (see **`docs/config/window-rules.md`** and **`docs/config/binds.md`**):

   ```ini
   window-rule {
       match app-id="^MyApp$"
       open-on-workspace = special:scratch
   }
   binds {
       SUPER+grave { workspace special:scratch; }
       SUPER+Shift+grave { movetoworkspace special:scratch; }
   }
   ```

3. **Toggle visibility** on the focused monitor: `view` / `workspace` with `special:<tag>`, `togglespecialworkspace <tag>` or **`togglespecialworkspace special:`** for the **default** (empty) tag, or `zestctl dispatch special <tag>`. IPC special dispatch writes root **`_NET_ZESTWM_SPECIAL`** (tag) and, when available, **`_NET_ZESTWM_SPECIAL_HIDDEN_ID`** (CARDINAL hidden id bridge); WM consumes hidden-id first and falls back to tag for compatibility. `zestctl dispatch workspace special:<tag>` and `zestctl dispatch view special:<tag>` are equivalent aliases. `zestctl dispatch movetoworkspace special:<tag> [window-id]` moves the focused/selected target window into that special workspace (silent move semantics, no view switch).

At most **one** special overlay **tag** is visible per monitor; choosing a **different** tag switches the overlay. **Same** tag while the overlay is already open: **closes** if focus is already on a visible client on that tag; otherwise **focuses** the first client on that tag (so a map with `workspace special:… silent` can show the overlay without stealing focus, and the next toggle brings keyboard focus into the scratchpad instead of hiding it).

While a special overlay is open, unmatched new clients inherit that tag (terminals, dialogs, splash, and other maps). Window rules, `movetoworkspace special:<tag>`, and transients of a **managed** special client still override. Maps onto the normal desktop underneath do not steal overlay focus. New clients on the open scratchpad may take focus (including splash/dialog). A modal-like map (`DIALOG` or usable transient) that lands on a **normal** workspace while the overlay is open closes the overlay so the dialog is usable.

`DIALOG` and `WM_TRANSIENT_FOR` windows float (and use the existing center heuristic) on every workspace.

### `workspace = special:<tag>` directives

Declare a tag and optional per-tag rules (same comma form as normal workspaces, but **only** `key:value` tokens after the selector—no bare display name):

```ini
workspace = special:magic
workspace = special:magic, dim_special:0.35
```

| Rule | Description |
| --- | --- |
| `dim_special:<f>` | Override global `dim_special` (see `general { }`) for this tag when its overlay is open. Range `0.0`–`1.0`; `0` disables the dim layer for that tag. |

Global default: `dim_special` in the **`general`** block (default **0.2**). Per-tag `dim_special` wins when set.

Unknown `key:value` keys on a special line log a warning.

### EWMH and external tools

Scratchpad clients are still X11 windows; legacy **EWMH desktop indices** do **not** encode the scratchpad tag.

| Source | Meaning for `special:` clients |
| --- | --- |
| `_NET_WM_DESKTOP` | Often **0** for non-dock special clients; **do not** treat this as “first numeric desktop”. |
| `_NET_ZEST_TREE_STATE` | Authoritative persistence ownership stream (`w<id>` / `s<hidden_id>`); also carries floating geometry via the `\|F(...)` suffix. |
| `_NET_ZESTWM_SPECIAL_HIDDEN_ID` (root, CARDINAL) | Optional single-value IPC payload for special dispatch (`special:<tag>` hidden-id bridge, consumed per dispatch and removed). |
| `_NET_ZESTWM_SPECIAL_OVERLAY` (root, UTF-8) | Per-monitor overlay open/closed and tag (`mon<TAB>tag<TAB>0|1` rows). |
| `_NET_ZEST_FLOATING_CLIENTS` (root, WINDOW) | Live list of managed clients with `Client::isfloating`; `zestctl clients` exposes `floating:yes|no` / JSON `floating`. |

Use **`zestctl clients`** (fields `special_tag` / JSON `special_tag`, and `floating` / JSON `floating`) and **`zestctl workspaces`** (synthetic negative ids for special rows, counts, visibility, plus `hidden_id` for `special:*` rows) for scripts and bars. **`zestctl activeworkspace`** reflects **normal** `_NET_CURRENT_DESKTOP` only—not which special overlay is open.

### Reload (`exec`) and removed tags

On **SIGHUP / reload** (`exec` restart), client ownership for `special:<tag>` is persisted in `_NET_ZEST_TREE_STATE` (special key `s<hidden_id>`), so windows are not re-adopted onto the active numeric desktop via `_NET_WM_DESKTOP` alone (index **0** would otherwise look like workspace **1**). Regression: **`meson test reload-special-workspace`**.

Per-monitor overlay open/closed and tag are restored from `_NET_ZESTWM_SPECIAL_OVERLAY` after scan/tree restore. Remapping non-silent special clients during startup must **not** force-open an overlay that was closed before reload. Regression: **`meson test reload-special-overlay-state`**.

Tree restore must not force-merge clients missing from a serialized leaf into `first_grouped()` (that created spurious tab groups on `special:<tag>` when the current workspace had an active group). Orphans reattach via normal BSP policy after the root is installed. Regression: **`meson test reload-groupmode-special-ungrouped`**.

If the **new** configuration does **not** re-declare a persisted tag, those windows are **moved to the monitor’s active normal workspace**; `zestctl clients` shows **`special_tag:-`**. Persisted tree blobs for unknown special tags are skipped. Regression: **`meson test reload-special-workspace-dropped-tag`**.

### Reload precedence for conflicting persistence sources

When persisted sources disagree on the same X11 window id, workspace ownership is resolved with this precedence chain (highest first):

1. `_NET_ZEST_TREE_STATE` (`tree_state_find_workspace_for_window`), where a normal workspace match wins over a special match.
2. `_NET_WM_DESKTOP` restore fallback.

This order is centralized in `apply_workspace_from_persistence` (`src/wm_state.cpp`) to avoid conflicting restore branches.
Regression: **`meson test reload-persistence-conflict`**.

### Floating geometry persistence (`|F(...)` suffix)

Floating clients live outside the BSP tree, so the BSP serializer (`wm::bsp::serialize_tree` over a `SerializedNode` built by `serialized_from_layout`) never serializes them. To let them survive reload, `savezesttreestate` appends an optional `|F(win:x:y:w:h,...)` suffix to each tree-state entry (one record per floating client on that workspace). On restore, `apply_floating_suffix` (via `wm::bsp::parse_floating_suffix`) marks the matching clients `isfloating`, restores their geometry, and pulls them out of the tree before the tiled-leaf sweep runs. A workspace holding **only** floating clients emits a tree-state entry with no tree node, just the `|F(...)` suffix. Regression: **`meson test reload-floating-geometry`**.

### Integration tests (regression)

Relevant Meson tests under `tests/integration/`: `windowrule-special-workspace`, `windowrule-scratchpad-magic-silent`, `reload-special-workspace`, `reload-special-workspace-dropped-tag`, and related window-rule probes (see **`tests/integration/README.md`**).

Open special-workspace follow-ups are tracked in `todo.md`.

## Compatibility notes

- Internal modeling is workspace-first: workspace ids are positive integers with no hardcoded cap, backed by the dynamic workspace registry, which is also the source EWMH reads.
- `zestctl monitors` exposes monitor id and RandR output name (`output:DP-1`); workspace monitor selectors can use both forms.
