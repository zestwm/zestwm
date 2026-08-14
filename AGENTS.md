# AGENTS.md

Operational guide for AI agents and collaborators on zestwm.
Human-oriented setup and product overview live in `README.md`.

## Language and goals

- Docs and code comments: English, concise technical wording.
- Keep the project minimal, predictable, easy to patch; prefer small isolated changes.
- Direction: contemporary tiled WM UX (workspace-first, BSP, file-based `zestwm.conf`).

## Repo structure

```text
src/
  zestwm.cpp, wm_startup.*   # process entry / startup
  config/                    # load, parse, runtime ConfigState (g_config)
  actions/                   # typed action implementations
  dispatch/                  # XCB + runtime command dispatch
  bsp/                       # BSP tree ops, serialize, groups
  client/                    # client lifecycle, focus, transfer
  monitor/                   # monitors, arrange, world state
  state/                     # session / authority wiring
  x11/                       # XCB backend, atoms, props, ops
  zestctl/                   # control-plane CLI + queries
  draw/                      # bar rendering
  sys/                       # spawn helpers
tests/
  unit/                      # meson unit tests (no nested X)
  integration/               # nested X11 probes + MUST mapping README
docs/
  behavior/must-features.md  # MUST-* runtime contracts
  config/                    # workspaces, binds, rules, groups
examples/zestwm.conf         # reference config
```

## Key patterns

- **Workspace-first**: `Client.workspace` is `WorkspaceRef`. Dynamic registry
  (`workspace_registry_*`). Special tags are separate overlays (`special:<tag>`).
  Do **not** reintroduce tag masks / bitmask-as-primary addressing. Prefer id/name APIs.
  Preserve EWMH desktop count/names/current desktop. Details: `docs/config/workspaces.md`.
- **MUST contracts first**: changes to reload, workspace routing, EWMH, focus
  dispatch, or parser workspace resolution must preserve listed `MUST-*` items in
  `docs/behavior/must-features.md`, with matching integration coverage. A failing
  `MUST-*` test means the task is not done (fix or update contract + tests with rationale).
  Keep `tests/integration/README.md` mapped to current `MUST-*` tests.
- **Typed actions**: action dispatch stays on `ActionCommand` payloads at the parser boundary.
- **Config surface**: only `zestwm.conf` (XDG path or `-c`). Describe syntax as zestwm
  config keywords, not as imports from other projects (see branding rule below).
- **Control plane**: prefer `zestctl` queries/dispatch for runtime inspection in tests
  and agent workflows; do not poke the host session display.
- **`togglefloating`**: only via explicit user hotkey; never from mouse-drag or
  implicit float transitions.
- **Ownership**: Window-keyed clients, BSP roots/children, monitors in
  `WMRuntimeAuthority`. Runtime config in `g_config` (`ConfigState` in `config.hpp`).
  Observation pointers stay raw (`Client*`, `Monitor*`, layout `parent`/`leaf`).

## C++

- Baseline: C++23, simple systems style. No new external deps without an explicit ask.
- No C-style casts: `static_cast` (and `const_cast<char**>(...)` only for POSIX APIs like `execvp`).
- Prefer `enum class` with explicit underlying type for fixed option sets; convert with `static_cast`.
- Prefer RAII (`unique_ptr`, `vector`, `string`) over raw ownership.
- XCB replies/events: `XcbReplyPtr` / `make_xcb_reply_ptr`. Detached spawn: `wm::sys::spawn_*`.
- Prefer `string_view` / `span` only when lifetime is guaranteed; do not store borrowed views without an owner.
- Prefer `optional` / `expected` (`expected<T, string>` unless a module type exists) on cold paths.
- Never throw in event/render hot paths. Prefer `noexcept` on hot helpers.
- Guard before every `%` / `/` (non-zero divisor). No implicit narrowing; cast and bounds-check.
- No `std::format` on untrusted/config strings that may contain braces.
- No hidden heap in hot paths unless measured and justified in a comment.
- Prefer early returns; `auto` only when it does not hide ownership/lifetime.
- When asked to modernize/C++23-upgrade: list intended edits first, keep behavior-preserving, then verify build.

## Change conventions

- Touch only files needed for the task. No cosmetic refactors or global behavior changes unless required.
- Investigate root cause first; no trial-and-error. If a patch fails or worsens things, revert it before retrying.
- If the user is wrong, say so clearly and explain the better approach.
- New/changed source files: brief file-level role comment. New/changed logic and methods: concise purpose
  comments (no obvious narration). Refactors must keep or improve comment clarity.
- Temporary always-on debug logs OK during triage; remove (or return to opt-in) when fixed. Do **not** add
  `getenv` debug toggles; use code reading and `tests/integration/` instead.
- Prefer remove over deprecate for dead paths in this development tree.

## Build and verification

```text
meson setup build
meson compile -C build
```

Optional: `meson compile -C build cpp-format` / `cpp-tidy` / `cpp-format-check`.
Pre-commit (`.githooks/pre-commit`) runs `clang-format` on staged C++.

**Done ladder** (pick the deepest rung the change touches):

1. Cold path / docs only: `meson compile -C build`.
2. Parser, registry, serialize, unit-covered helpers: compile + relevant `meson test -C build --suite unit` (or named unit test).
3. Reload, workspace routing, special overlay, EWMH, focus dispatch, parser workspace resolution:
   run the matching nested probe(s) for the `MUST-*` ids you touched.

Never run `reload-*` probes or `zestctl dispatch reload` against the host session display. Use:

```text
tests/integration/run_xephyr_suite.sh --build-dir build --no-build --headless --test <name>
```

Isolated nested X11 + isolated `HOME` / `XDG_*`. Debug: `ZESTWM_NESTED_X11_DEBUG=1`, `ZESTWM_TEST_TRACE=1`.

Do not commit build artifacts (`zestwm`, `*.o`) unless asked. Config entry: `zestwm.conf` /
`examples/zestwm.conf` / `config.hpp` (`wmconf_load` / `wmconf_free`).

## Common gotchas

1. **Host display is off-limits** - never point WM under test or `reload` at the login `DISPLAY`. Probes use a private nested server via `zestwm_start_nested_x11`.
2. **Nested X default is Xvfb** - Xephyr is optional (`ZESTWM_PREFER_XEPHYR=1`). Some probes are timing-sensitive under Xephyr.
3. **Shell CWD does not persist** across tool calls - use `cd /path && cmd` in one command (including worktrees).
4. **Persistence conflict: tree-state wins** - `_NET_ZEST_TREE_STATE` ownership beats legacy special-client cache on reload/startup (`MUST-RELOAD-007` / `008`).
5. **Dropped special tags** - if config removes a declared `special:<tag>`, clients must land on a normal workspace (`special_tag:-`), not resurrect the tag (`MUST-RELOAD-005`).
6. **MUST mapping must stay in sync** - new protected behavior needs `MUST-*` id + probe + row in `tests/integration/README.md` before calling it done.
7. **Format vs compile** - compile success does not imply format gate; staged C++ goes through `.githooks/pre-commit` / `cpp-format`.
8. **No external WM branding** in new/edited comments, docs, tests, or commit prose - see `.cursor/rules/no-external-wm-branding.mdc`.

## Avoid

- Unrequested large edits; mixing feature + refactor + format in one change.
- Macros/indirection without clear benefit.

## See also

- `README.md` - product overview, install, run, config path
- `docs/behavior/must-features.md` - `MUST-*` contracts
- `tests/integration/README.md` - probe list and `MUST-*` mapping
- `docs/config/workspaces.md` - workspace / special model
- `docs/config/binds.md`, `window-rules.md`, `group.md` - config surfaces
