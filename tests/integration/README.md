# Integration Tests

This directory contains integration-level probes for real X11 behavior paths.

## Scope

- Validate behavior that depends on a live X server and runtime protocol data.

## Scratchpad / special contracts

- Runtime contract source: [`docs/behavior/must-features.md`](../behavior/must-features.md).
- Current model is workspace-first (`WorkspaceRef::special`) with strict dispatch/persistence behavior validated by probes listed here.
- Keep tests deterministic, minimal, and scriptable.
- Prefer behavior-preserving assertions over broad end-to-end scenarios.

## Nested X server (Xephyr vs Xvfb)

- Integration shell probes source `tests/integration/xserver_common.inc.sh` and call `zestwm_start_nested_x11`.
- **Default:** **Xvfb** first when both are installed (historical behavior; some probes are timing-sensitive under Xephyr). If only Xephyr exists, it is used.
- **Optional:** `export ZESTWM_PREFER_XEPHYR=1` before `meson test` to prefer **Xephyr** (experimental; file issues if a probe regresses).
- Each probe picks a free `:${display}` under `/tmp/.X11-unix` and tears it down in `cleanup`; your host `DISPLAY` (e.g. `:0`) is not used for WM under test.
- `reload-*` probes **do** run `dispatch reload` — only on that probe’s private nested display, never on your login session or on another probe’s server.

## Conventions

- File naming: `<feature>_probe.cpp` for C++ probes.
- Meson test name: short, stable, kebab-case (example: `wmclass-property`).
- Test binary target: `<feature>-test` (example: `wmclass-property-test`).
- Console output: concise case-by-case status lines plus final pass/fail summary.
- Exit code:
  - `0` on success
  - non-zero on mismatch or setup/runtime failure

## Current tests

- `wmclass_property_probe.cpp`
  - verifies `WM_CLASS` parsing behavior in `x11_get_class_hint`
  - includes valid payload and malformed payload edge cases
- `focusurgent_probe.sh`
  - runs `zestwm` in isolated Xvfb display with probe-specific bind for `focusurgent`
  - spawns two real X11 probe clients (`focusurgent-urgent-client-test`) and moves target client to workspace 2
  - marks target urgent deterministically with `SIGUSR1` -> `XSetWMHints(XUrgencyHint)` inside probe client
  - triggers `focusurgent` through keyboard bind and verifies workspace switch + focused target window
  - includes `no-urgent` mode to verify `focusurgent` does not switch workspace/focus without urgency
- `reload_workspace_restore_probe.sh`
  - runs `zestwm` in isolated Xvfb display and spawns two real probe clients
  - moves one probe client to workspace 2, keeps the other on workspace 1, then triggers `zestctl dispatch reload`
  - verifies each client keeps its original workspace ownership after reload
- `reload_floating_geometry_probe.sh`
  - runs `zestwm` in isolated Xvfb display and spawns one real probe client
  - toggles it floating, moves it to a known off-tile position/size via `xdotool`, then triggers `zestctl dispatch reload`
  - verifies the floating geometry (`x/y/w/h`) is preserved after reload (not retiled)
- `reload_special_workspace_probe.sh`
  - runs `zestwm` in isolated Xvfb with a `window-rule` placing the probe client on `special:<tag>`
  - triggers `zestctl dispatch reload` and asserts the WM stays alive and `zestctl clients` still reports the same `special_tag` for the window
- `reload_special_overlay_state_probe.sh`
  - `MUST-RELOAD-011`: non-silent `special:dropdown` client + two clients on workspace `1`; close overlay; `reload`
  - asserts overlay stays closed, special client keeps `special_tag:dropdown`, and ws1 clients stay on `ws:1` / `special_tag:-` (no contamination)
- `reload_groupmode_special_ungrouped_probe.sh`
  - `MUST-RELOAD-012`: active `groupmode` pair on workspace `1` + two silent ungrouped `special:magic` clients; `reload` while on workspace `1`
  - asserts magic clients stay ungrouped with each other and ws1 group size remains `2`
- `reload_special_workspace_dropped_tag_probe.sh`
  - first config declares `workspace = special:<tag>` and a `window-rule` placing the probe on that tag; after `zestctl dispatch reload` the config file is replaced with a copy of `examples/zestwm.conf` that omits that special declaration
  - asserts the client remains managed and `zestctl clients` reports `special_tag:-` (normal workspace), not the removed tag
- `reload_multi_workspace_probe.sh`
  - runs `zestwm` in isolated Xvfb display and spawns four real probe clients
  - moves two clients to workspace 3 and two clients to workspace 2
  - switches back to workspace 1, triggers `zestctl dispatch reload`, then verifies all four clients keep workspace ownership (2/3) post-reload
- `reload_multi_special_workspace_probe.sh`
  - declares two `workspace = special:…` tags, `window-rule` blocks for clients A/B/C (A and C share tag `a`), `bind` for `groupmode` / `cyclegroup`, and runs **first reload** (dual-tag + workspaces export)
  - opens overlay with `dispatch special <tag>`, **groupmode** + third client + **cyclegroup** on scratchpad (needs `xdotool`)
  - validates `zestctl workspaces` special rows expose numeric `hidden_id` mapping for both tags
  - **`movetoworkspace 2 <win>`** moves B from special to numeric desktop 2; **second reload** asserts A/C still on special `a`, B still `ws:2` with `special_tag:-`
- `reload_persistence_conflict_probe.sh`
  - seeds startup persistence ownership via `_NET_ZEST_TREE_STATE` (`w2`) on a managed XID
  - asserts deterministic startup restore: `zestctl clients` reports `ws:2` and `special_tag:-`
- `reload_persistence_conflict_special_vs_tree_probe.sh`
  - starts client on `special:<tag>`, then seeds reload conflict via `_NET_ZEST_TREE_STATE` (`w2`) before dispatching `reload`
  - asserts reload ownership follows tree-state (`ws:2`, `special_tag:-`) even when legacy special cache data exists
- `reload_persistence_special_hidden_id_probe.sh`
  - seeds `_NET_ZEST_TREE_STATE` with special workspace key `s<hidden_id>` (`s32` for first declared special tag in probe config)
  - asserts startup restore resolves hidden-id token to configured `special:<tag>` and `zestctl clients` reports matching `special_tag`
- `dispatch_special_aliases_probe.sh`
  - validates IPC aliases for special workspaces: `dispatch movetoworkspace special:<tag>`, `dispatch workspace special:<tag>`, and `dispatch view special:<tag>`
  - asserts move-to-special assigns `special_tag:<tag>`, aliases keep `zestctl workspaces` special row available, and undeclared `special:<tag>` dispatch is rejected
- `parser_workspace_name_dispatch_probe.sh`
  - `MUST-PARSER-001`: declares `workspace = 7, WmstateNamedWs`, runs `zestctl dispatch workspace WmstateNamedWs` (name token vs `_NET_DESKTOP_NAMES` / registry export), asserts `zestctl -j activeworkspace` reports that display name
- `group_single_client_probe.sh`
  - runs `zestwm` in isolated Xvfb display and spawns two real probe clients
  - enables `groupmode` while only one client is present in workspace 1
  - verifies `cyclegroup 1` can switch focus to the second client (ensures second client joined grouped flow instead of split path)
- `group_single_special_toggle_crash_probe.sh`
  - reproduces single-client grouped state and exercises keyboard `workspace special:` / `togglespecialworkspace` paths
  - asserts WM stays alive/responding when toggling `workspace special:` with one grouped client active (no crash regression)
- `group_special_workspace_regression_probe.sh`
  - reproduces two regressions in one flow: create group with one client, toggle group off, open second client, then create group again
  - with regrouped clients alive, opens `workspace special:<tag>` via IPC and asserts WM remains responsive (no crash path)
- `special_group_toggle_focus_reload_probe.sh`
  - pins two clients to `special:magic` and keeps one unruled client on normal workspace `1`
  - forms a grouped pair on `special:magic`, cycles to second tab, toggles overlay closed/open, then reloads
  - asserts no normal->special contamination after reload and preserves grouped active-client focus when reopening `special:magic`
- `close_second_client_crash_probe.sh`
  - reproduces minimal close-crash flow: open app A, open app B, close app B
  - asserts closed window disappears from `zestctl clients` and WM remains alive/responding after the close event
- `ewmh_dock_gap_probe.sh`
  - runs `zestwm` in isolated Xvfb display with non-contiguous occupied workspaces (`1`, `2`, `4`)
  - spawns an explicit dock window pinned to desktop index `4` (workspace id `5`)
  - verifies `_NET_NUMBER_OF_DESKTOPS` stays `4` (dock clients must not inflate occupancy export)
- `workspace_defaultname_export_probe.sh`
  - verifies `defaultName` changes exported workspace display name (`zestctl workspaces`)
- `workspace_sparse_registry_probe.sh`
  - minimal config based on `examples/zestwm.conf` plus sparse `workspace = 10` and `workspace = 3, sparse-ws`; verifies registry export, gap default names, and name dispatch
- `workspace_monitor_selector_probe.sh`
  - verifies workspace declarations with `monitor` selectors (`0` and `DP-1`) keep workspace dispatch path healthy
- `workspace_policy_keys_probe.sh`
  - verifies parser accepts workspace policy keys (`default`, `persistent`, `defaultName`) without unsupported-key warnings
- `workspace_rules_runtime_probe.sh`
  - per-id `gapsout`/`border:false` rules apply at tile time via `xwininfo`
- `workspace_selector_wt1_probe.sh`
  - `workspace = w[t1], gapsout:40` applies when exactly one tiled client is present
- `xdg_autostart_probe.sh`
  - runs `zestwm` with temporary `HOME` and XDG desktop context, seeding `~/.config/autostart/*.desktop` fixtures
  - verifies positive launch paths (`Exec`, `OnlyShowIn`, field-code/quote sanitize) and negative filters (`Hidden`, `NotShowIn`, failing `TryExec`)
- `windowrule_keyword_probe.sh`
  - verifies niri-style `window-rule` routing to workspace 2.
- `windowrule_anon_block_probe.sh`
  - verifies anonymous `window-rule { ... }` with `open-floating` parses and the WM manages the client
- `windowrule_named_block_probe.sh`
  - verifies `window-rule { match ... open-floating = true }` parses and the WM manages the client
- `windowrule_monitor_selector_probe.sh`
  - verifies `monitor` window-rule effect with a RandR output name (`DP-1` under Xvfb) parses and a matching client is managed
- `windowrule_special_workspace_probe.sh`
  - verifies `window-rule` with `open-on-workspace = special:<tag>` and `zestctl dispatch special <tag>` (overlay reopen focus check)
- `special_overlay_dialog_routing_probe.sh`
  - opens `special:<tag>` via window-rule, then maps a `_NET_WM_WINDOW_TYPE_DIALOG` with no rule
  - asserts the dialog inherits the open special tag and the overlay stays open
  - maps a second dialog forced to a normal workspace by window-rule and asserts the overlay closes
- `floating_dialog_onscreen_probe.sh`
  - maps a floating `_NET_WM_WINDOW_TYPE_DIALOG` on a normal workspace and on an open special overlay
  - asserts Absolute X/Y stay on-screen (guards against legacy `x + 2 * sw` pre-map placement)
  - asserts `_NET_WM_WINDOW_OPACITY` is absent (floating dialogs stay fully opaque for compositors)
- `clients_floating_export_probe.sh`
  - asserts `zestctl clients` reports `floating:no` for a tiled map, `floating:yes` after `togglefloating`, then `floating:no` after toggle back
  - covers root `_NET_ZEST_FLOATING_CLIENTS` runtime export
- `windowrule_workspace_occupancy_export_probe.sh`
  - verifies client routed by `window-rule` to non-viewed workspace updates EWMH export immediately (`zestctl workspaces` reports `windows:1` for target workspace)
  - asserts active workspace stays unchanged while occupancy export refreshes
- `windowrule_special_empty_tag_probe.sh`
  - declares `workspace = special:` and assigns the probe to **`workspace special:`** (default tag)
  - asserts `zestctl clients` reports `special_tag:(empty)` for the mapped client
- `windowrule_scratchpad_magic_silent_probe.sh`
  - maps two silent `special:magic` clients and asserts both keep `special_tag:magic` (overlay closed), then after toggle
  - asserts `workspaces` reports `special:magic windows:2` and a special client is focused
  - guards BSP split handoff dropping the taken special root on the second silent map

## MUST mapping

- `MUST-RELOAD-001` -> `reload-workspace-restore`
- `MUST-RELOAD-002` -> `reload-multi-workspace`
- `MUST-RELOAD-003` -> `reload-multi-workspace` (single-client geometry sanity check)
- `MUST-RELOAD-004` -> `reload-special-workspace`
- `MUST-RELOAD-005` -> `reload-special-workspace-dropped-tag`
- `MUST-RELOAD-006` -> `reload-multi-special-workspace`
- `MUST-RELOAD-007` -> `reload-persistence-conflict`
- `MUST-RELOAD-008` -> `reload-persistence-conflict-special-vs-tree`
- `MUST-RELOAD-009` -> `reload-floating-geometry`
- `MUST-RELOAD-010` -> `reload-skip-autostart`
- `MUST-FOCUS-001` -> `focusurgent-no-urgent`
- `MUST-FOCUS-002` -> `focusurgent`
- `MUST-GROUP-001` -> `group-single-client`
- `MUST-GROUP-002` -> `group-special-workspace-regression`
- `MUST-GROUP-003` -> `group-single-special-toggle-crash`
- `MUST-GROUP-004` -> `close-second-client-crash`
- `MUST-EWMH-001` -> `ewmh-dock-gap`
- `MUST-IPC-001` -> `dispatch-special-aliases`
- `MUST-PARSER-001` -> `parser-workspace-name-dispatch` (+ `config-workspace` unit registry lookup)

## Run

Run the integration suite (single command, same mode for all probes):

```sh
tests/integration/run_xephyr_suite.sh --build-dir build
```

Default behavior is **headless** (Xvfb only). The explicit flag is optional:

```sh
tests/integration/run_xephyr_suite.sh --build-dir build --headless
```

If you need on-screen debug, force headed mode:

```sh
tests/integration/run_xephyr_suite.sh --build-dir build --headed
```

Build only this test target:

```sh
meson compile -C build wmclass-property-test
```

Run this single test:

```sh
meson test -C build wmclass-property
```

Run focusurgent probe:

```sh
meson test -C build focusurgent
```

Run negative focusurgent probe:

```sh
meson test -C build focusurgent-no-urgent
```

Run reload workspace restore probe:

```sh
meson test -C build reload-workspace-restore
```

Run multi-workspace reload ownership probe:

```sh
meson test -C build reload-multi-workspace
```

Run single-client grouping probe:

```sh
meson test -C build group-single-client
```

Run grouped + special workspace regression probe:

```sh
meson test -C build group-special-workspace-regression
```

Run special-group toggle/reload focus probe:

```sh
meson test -C build special-group-toggle-focus-reload
```

Run single-client grouped special-toggle crash probe:

```sh
meson test -C build group-single-special-toggle-crash
```

Run EWMH dock-gap occupancy probe:

```sh
meson test -C build ewmh-dock-gap
```

Run workspace `defaultName` export probe:

```sh
meson test -C build workspace-defaultname-export
```

Run workspace monitor selector probe:

```sh
meson test -C build workspace-monitor-selector
```

Run workspace policy key acceptance probe:

```sh
meson test -C build workspace-policy-keys
```

Run workspace sparse registry probe (`workspace = <id>` fill + named override):

```sh
meson test -C build workspace-sparse-registry
```

Run workspace rules runtime probe (per-workspace `gapsin`/`gapsout` tile inset + `border:false`):

```sh
meson test -C build workspace-rules-runtime
```

Run `w[t1]` workspace selector probe:

```sh
meson test -C build workspace-selector-wt1
```

Run XDG autostart integration probe:

```sh
meson test -C build xdg-autostart
```

Run `window-rule` keyword probe:

```sh
meson test -C build windowrule-keyword
```

Run niri-style anonymous-equivalent window-rule probe:

```sh
meson test -C build windowrule-anon-block
```

Run `window-rule { }` block probe:

```sh
meson test -C build windowrule-named-block
```

Run window-rule `open-on-output` probe:

```sh
meson test -C build windowrule-monitor-selector
```

Run all registered tests:

```sh
meson test -C build
```

## Adding a new integration test

1. Add new probe source under `tests/integration/`.
2. Register executable + `test()` in `meson.build` (guarded by `integration_tests` option).
3. Keep dependencies minimal and reuse existing project deps where possible.
4. Verify:
   - `meson compile -C build <new-target>`
   - `meson test -C build <new-test-name>`
