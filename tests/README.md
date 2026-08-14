# Test and Coverage Plan

This document describes a future testing and coverage strategy for `zestwm`.
It is intentionally lightweight and can be implemented incrementally.

## Goals

- Add fast, deterministic unit tests for pure logic.
- Add integration tests for real X11 WM behavior.
- Track line coverage over time to reduce regressions.
- Keep test setup simple and scriptable.

## C++23 style baseline

When adding or updating C++ test code in this plan:

- Use C++23 language features supported by the project toolchain.
- Prefer named casts (`static_cast`, `const_cast`) over C-style casts.
- Prefer scoped enums and explicit conversions in assertions/helpers.
- Keep tests deterministic and explicit about ownership/lifetimes (RAII).
- Keep modernization-only edits separate from behavioral test changes.

## Scope split

### 1) Unit tests (fast)

Target files/functions with little or no X server dependency:

- `src/layout_tree.cpp`
  - `lt_grouped_add/remove/move_active`
  - split ratio clamping
  - active index handling after mutations
- `src/config/runtime.cpp`
  - parser branches for `group {}` / `groupbar {}`
  - key normalization and error paths
- `src/wm_state.cpp`
  - parse/serialize helpers for saved state payloads
  - selection/tree/group restore edge cases (invalid entries, missing clients)

Recommended framework:

- `doctest` (very small footprint), or
- `Catch2` (feature-rich, still simple for C++).

### 2) Integration tests (behavioral)

Run `zestwm` in isolated `Xephyr` session and assert behavior via `zestctl`.

High-value scenarios:

- Reload preserves:
  - selected workspace
  - selected group client
  - grouped/tree state
- Closing client inside group keeps focus in same group.
- Groupbar rendering behavior for position/indicator/title settings.

Suggested tools:

- Shell scripts (`bash`) under `tests/integration/`
- Optional helpers: `xdotool`, `xprop`, `xwininfo`, `wmctrl`.

## Coverage tooling

### GCC path (simple default)

- Compile with:
  - `-O0 -g --coverage`
- Generate report with:
  - `lcov`
  - `genhtml`

Typical flow:

1. Build instrumented binaries.
2. Run unit + integration tests.
3. Capture `.gcda/.gcno`.
4. Generate HTML report.

### Clang path (optional)

- Compile with:
  - `-fprofile-instr-generate -fcoverage-mapping`
- Generate report with:
  - `llvm-profdata`
  - `llvm-cov`.

## Proposed layout

```text
tests/
  README.md
  unit/
    test_layout_tree.cpp
    test_config_parser.cpp
    test_wm_state.cpp
  integration/
    reload_preserves_selection.sh
    group_focus_on_close.sh
  fixtures/
    configs/
      minimal.conf
      groupbar.conf
```

## Suggested Meson targets (future)

- `meson test -C build` (unit and integration once wired)
- Optional Ninja targets or custom Meson targets for coverage when added

These targets are not implemented yet; this file is the integration blueprint.

## Current implemented integration test

The first integration test is now wired:

- Test name: `wmclass-property`
- Source: `tests/integration/wmclass_property_probe.cpp`
- Purpose: validate `WM_CLASS` parsing safety/behavior in `x11_get_class_hint` with valid and malformed payloads.

### Configure and run

Enable integration tests (default is enabled):

```sh
meson setup build -Dintegration_tests=true
```

Run all registered tests:

```sh
meson test -C build
```

Run only this integration test:

```sh
meson test -C build wmclass-property
```

### Incremental test build (without rebuilding whole WM)

Build only the integration test binary:

```sh
meson compile -C build wmclass-property-test
```

This keeps the test flow fast and avoids recompiling the full `zestwm` target.

### Disable integration tests

```sh
meson setup --reconfigure build -Dintegration_tests=false
```

## Rollout plan

1. Add `tests/unit/` with `layout_tree` tests first.
2. Add one `Xephyr` integration test for reload-selection preservation.
3. Wire a basic `coverage` target.
4. Expand to parser and groupbar behavior once baseline is stable.

