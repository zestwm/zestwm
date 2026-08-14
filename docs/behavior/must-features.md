# ZestWM MUST Features

This document defines non-negotiable runtime behavior contracts ("MUST features").
Any change touching related code paths MUST preserve these behaviors or explicitly update this file and corresponding tests.

## Frozen baseline (Phase 1)

For workspace migration **Phase 1**, we do **not** maintain a separate “behavior baseline” document. The **frozen** behavioral surface is exactly:

- this `MUST-*` list (contracts + scenarios + expected outcomes), and
- the integration tests named in each item (see `tests/integration/README.md` for `MUST-* -> meson test name`).

New critical behaviors get a new `MUST-*` id, a test (or extension of an existing probe), and a row in the README mapping before we treat the behavior as protected.

## MUST List

- `MUST-RELOAD-001` Workspace ownership survives reload.
  - Scenario: clients distributed across workspaces; trigger `reload`.
  - Expected: each client keeps its workspace ownership after reload.
  - Covered by: `reload-workspace-restore`, `reload-multi-workspace`.

- `MUST-RELOAD-002` Workspace ownership survives consecutive reloads.
  - Scenario: same as above, but perform two consecutive reloads.
  - Expected: no collapse to workspace 1; ownership remains stable after each reload.
  - Covered by: `reload-multi-workspace`.

- `MUST-RELOAD-003` Single-client workspace keeps full tile geometry after reload.
  - Scenario: workspace has exactly one non-dock client after reload.
  - Expected: no stale centered slot; client fills normal tile area.
  - Covered by: `reload-multi-workspace` (geometry sanity assertion).

- `MUST-RELOAD-004` Special workspace ownership survives reload.
  - Scenario: client belongs to declared `special:<tag>` and `reload` is triggered.
  - Expected: client remains on the same special tag after reload.
  - Covered by: `reload-special-workspace`.

- `MUST-RELOAD-005` Special scratchpad tag removed from configuration is not resurrected from persistence.
  - Scenario: client was on `special:<tag>` with that tag declared; replace config so the tag is no longer declared; trigger `reload` (`exec` restart).
  - Expected: client is on a normal workspace for that monitor (`zestctl clients` shows `special_tag:-`); WM stays alive.
  - Covered by: `reload-special-workspace-dropped-tag`.

- `MUST-RELOAD-006` Multi-special ownership remains stable across reload and workspace move.
  - Scenario: multiple special tags exist; clients are distributed across two special tags; one client is moved to a normal workspace; two reloads are triggered.
  - Expected: clients keep final ownership (`special:<tag>` or normal `ws:<id>`) and special workspace rows stay exported.
  - Covered by: `reload-multi-special-workspace`.

- `MUST-RELOAD-007` Tree-state normal ownership wins when conflicting persisted ownership exists for same window.
  - Scenario: before startup restore, same XID appears with conflicting ownership sources; `_NET_ZEST_TREE_STATE` states a normal workspace.
  - Expected: ownership resolves to normal workspace (`ws:<id>`, `special_tag:-`) deterministically.
  - Covered by: `reload-persistence-conflict`.

- `MUST-RELOAD-008` Legacy special-clients cache cannot override tree-state ownership after reload.
  - Scenario: window starts on `special:<tag>`, then `_NET_ZEST_TREE_STATE` is seeded with `w<id>` before reload.
  - Expected: tree-state wins on reload (`ws:<id>`, `special_tag:-`) even when legacy `_NET_ZESTWM_SPECIAL_CLIENTS` data exists.
  - Covered by: `reload-persistence-conflict-special-vs-tree`.

- `MUST-RELOAD-009` Floating client geometry survives reload via tree-state `|F(...)` suffix.
  - Scenario: a managed client is toggled floating, moved to a known off-tile position/size, then `reload` is triggered.
  - Expected: after reload the client is still floating with the same `x/y/w/h` geometry (not retiled).
  - Covered by: `reload-floating-geometry`.

- `MUST-RELOAD-010` Autostart does not rerun on WM reload.
  - Scenario: config `exec-once` runs once at session start; trigger `reload`.
  - Expected: `exec-once` and XDG autostart are not launched again (no duplicate apps).
  - Covered by: `reload-skip-autostart`.

- `MUST-RELOAD-011` Special overlay open/closed state survives reload.
  - Scenario: a non-silent `special:<tag>` client exists with other clients on workspace `1`; close overlay and `reload`; then reopen and `reload` again.
  - Expected: closed stays closed and open stays open across reload; special client keeps `special_tag`; workspace `1` clients stay on `ws:1` with `special_tag:-` (no normal-to-special contamination).
  - Covered by: `reload-special-overlay-state`.

- `MUST-RELOAD-012` Reload with an active group on the current workspace does not force-group special clients.
  - Scenario: workspace `1` has an active tab group (`groupmode`); two silent `special:<tag>` clients exist as separate tiles; trigger `reload` while workspace `1` is current.
  - Expected: special clients keep `special_tag` and remain ungrouped with each other (`group_size` stays `1`); the workspace `1` group remains size `2`.
  - Covered by: `reload-groupmode-special-ungrouped`.

- `MUST-EWMH-001` EWMH desktop export includes non-contiguous workspace ids.
  - Scenario: used workspaces include gaps (example: `1`, `2`, `4` with `3` empty).
  - Expected: `_NET_NUMBER_OF_DESKTOPS` / `_NET_DESKTOP_NAMES` still include `4`.
  - Covered by: `ewmh-dock-gap`.

- `MUST-FOCUS-001` `focusurgent` switches only when an urgent target exists.
  - Scenario: no urgent clients, then run `focusurgent`.
  - Expected: no unintended workspace switch/focus jump.
  - Covered by: `focusurgent-no-urgent`.

- `MUST-FOCUS-002` `focusurgent` switches to urgent target when urgency is set.
  - Scenario: urgent client exists on another workspace.
  - Expected: workspace/focus switches to that client.
  - Covered by: `focusurgent`.

- `MUST-PARSER-001` Workspace name parsing is registry-first.
  - Scenario: parser resolves action args with workspace names.
  - Expected: name resolution uses workspace registry, not raw `workspace_names` vector indexing alone.
  - Covered by: `parser-workspace-name-dispatch` (`zestctl dispatch workspace <name>` vs EWMH desktop names from registry); unit coverage in `config-workspace` (`workspace_registry_find_by_name`); bind-time resolution in `parse_action` / rule workspace tokens.

- `MUST-GROUP-001` Single client in workspace can still enter group flow.
  - Scenario: workspace has one managed client; trigger grouping action/path.
  - Expected: grouping is allowed and does not regress due to single-client workspace state.
  - Covered by: `group-single-client`.

- `MUST-GROUP-002` Grouped workspace remains stable when opening special workspace overlay.
  - Scenario: enable grouping with one client, add a second grouped client, then open `workspace special:<tag>` via IPC.
  - Expected: `cyclegroup` still switches to second grouped client and WM stays alive/responsive after special workspace open.
  - Covered by: `group-special-workspace-regression`.

- `MUST-GROUP-003` Grouped single-client special toggle path remains crash-free.
  - Scenario: with one grouped client active, trigger keyboard `workspace special:` and `togglespecialworkspace` paths.
  - Expected: WM stays alive/responsive across open/close toggles; no crash/hang.
  - Covered by: `group-single-special-toggle-crash`.

- `MUST-GROUP-004` Closing second client does not crash WM.
  - Scenario: open client A and client B, then close B.
  - Expected: closed window disappears from client list and WM remains alive.
  - Covered by: `close-second-client-crash`.

- `MUST-IPC-001` `zestctl dispatch` special workspace aliases remain equivalent.
  - Scenario: use `dispatch workspace special:<tag>`, `dispatch view special:<tag>`, and `dispatch movetoworkspace special:<tag> [window-id]`.
  - Expected: all commands route through same special-workspace dispatch semantics (`special_tag` assignment for move; special row remains available in `zestctl workspaces`), and undeclared special tags are rejected (no tag-only fallback path).
  - Covered by: `dispatch-special-aliases`.

## Change Rule

When changing behavior that touches any MUST feature:

1. Update implementation.
2. Update or add test coverage for the corresponding MUST id.
3. Update this document if behavior contract changes.
4. Do not merge with a failing MUST test.
