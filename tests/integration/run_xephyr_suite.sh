#!/usr/bin/env bash
set -euo pipefail

build_dir="build"
no_build=0
headless=1
xvfb_display=""
xvfb_pid=""
selected_tests=()
suite_sandbox_root=""
suite_home=""
suite_xdg_config_home=""
suite_xdg_state_home=""
suite_xdg_cache_home=""
suite_xdg_data_home=""
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=tests/integration/xserver_common.inc.sh
source "${repo_root}/tests/integration/xserver_common.inc.sh"
host_display="${DISPLAY:-}"
export ZESTWM_HOST_DISPLAY="${host_display}"

usage() {
  cat <<'EOF'
Usage:
  tests/integration/run_xephyr_suite.sh [--build-dir DIR] [--no-build] [--headless|--headed] [--test NAME ...]

Options:
  --build-dir DIR     Meson build directory (default: build)
  --no-build          Skip meson compile step before running tests
  --headless          Start a nested X11 wrapper (Xvfb preferred; see xserver_common.inc.sh)
  --headed            Do not start a nested server (each probe still spawns its own isolated :N)
  --test NAME         Run only selected test name (repeatable)
  -h, --help          Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir)
      shift
      build_dir="${1:-}"
      if [[ -z "${build_dir}" ]]; then
        echo "run_xephyr_suite: --build-dir requires a value" >&2
        exit 2
      fi
      ;;
    --no-build) no_build=1 ;;
    --headless) headless=1 ;;
    --headed) headless=0 ;;
    --test)
      shift
      test_name="${1:-}"
      if [[ -z "${test_name}" ]]; then
        echo "run_xephyr_suite: --test requires a value" >&2
        exit 2
      fi
      selected_tests+=("${test_name}")
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "run_xephyr_suite: unknown option '$1'" >&2
      usage >&2
      exit 2
      ;;
  esac
  shift
done

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "run_xephyr_suite: missing required command '$1'" >&2
    exit 2
  fi
}

need_cmd meson

start_headless_nested_x11_optional() {
  local display_num
  zestwm_require_nested_x11 "run_xephyr_suite"
  if ! command -v xdpyinfo >/dev/null 2>&1; then
    echo "run_xephyr_suite: missing xdpyinfo" >&2
    exit 2
  fi
  display_num=90
  while [[ ${display_num} -lt 110 ]]; do
    if [[ ! -S "/tmp/.X11-unix/X${display_num}" ]]; then
      break
    fi
    display_num=$((display_num + 1))
  done
  if [[ ${display_num} -ge 110 ]]; then
    echo "run_xephyr_suite: unable to allocate free X11 display for suite wrapper" >&2
    exit 2
  fi
  xvfb_display=":${display_num}"
  zestwm_start_nested_x11 "${xvfb_display}" "/tmp/zestwm-xephyr-suite-wrapper.log"
  for _ in {1..40}; do
    if ! kill -0 "${xvfb_pid}" 2>/dev/null; then
      echo "run_xephyr_suite: nested X server exited during startup (check /tmp/zestwm-xephyr-suite-wrapper.log)" >&2
      exit 2
    fi
    if DISPLAY="${xvfb_display}" xdpyinfo >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "run_xephyr_suite: nested X server did not become ready" >&2
  exit 2
}

cleanup() {
  if [[ -n "${xvfb_pid}" ]] && kill -0 "${xvfb_pid}" 2>/dev/null; then
    kill "${xvfb_pid}" 2>/dev/null || true
    wait "${xvfb_pid}" 2>/dev/null || true
  fi
  if [[ -n "${suite_sandbox_root}" && -d "${suite_sandbox_root}" ]]; then
    rm -rf "${suite_sandbox_root}" 2>/dev/null || true
  fi
}
trap cleanup EXIT

setup_suite_sandbox_env() {
  suite_sandbox_root="$(mktemp -d "/tmp/zestwm-test-suite.XXXXXX")"
  suite_home="${suite_sandbox_root}/home"
  suite_xdg_config_home="${suite_sandbox_root}/xdg-config"
  suite_xdg_state_home="${suite_sandbox_root}/xdg-state"
  suite_xdg_cache_home="${suite_sandbox_root}/xdg-cache"
  suite_xdg_data_home="${suite_sandbox_root}/xdg-data"
  mkdir -p "${suite_home}" "${suite_xdg_config_home}" "${suite_xdg_state_home}" "${suite_xdg_cache_home}" "${suite_xdg_data_home}"
}

# Each probe allocates a private :DISPLAY (nested Xephyr or Xvfb) and runs zestwm only there.
# `reload-*` tests call `zestctl dispatch reload` on that private display only — never on your login session.
tests=(
  focusurgent
  focusurgent-no-urgent
  reload-workspace-restore
  reload-skip-autostart
  reload-special-workspace
  reload-special-overlay-state
  reload-groupmode-special-ungrouped
  reload-special-workspace-dropped-tag
  reload-multi-workspace
  reload-multi-special-workspace
  dispatch-special-aliases
  parser-workspace-name-dispatch
  reload-persistence-conflict
  reload-persistence-conflict-special-vs-tree
  reload-persistence-special-hidden-id
  group-single-client
  group-single-special-toggle-crash
  group-special-workspace-regression
  special-group-toggle-focus-reload
  close-second-client-crash
  ewmh-dock-gap
  workspace-defaultname-export
  workspace-sparse-registry
  workspace-monitor-selector
  workspace-policy-keys
  workspace-rules-runtime
  workspace-selector-wt1
  xdg-autostart
  wmclass-property
  windowrule-keyword
  windowrule-anon-block
  windowrule-named-block
  windowrule-monitor-selector
  windowrule-special-workspace
  special-overlay-dialog-routing
  floating-dialog-onscreen
  clients-floating-export
  windowrule-workspace-occupancy-export
  windowrule-special-empty-tag
  windowrule-scratchpad-magic-silent
)

if [[ ${#selected_tests[@]} -gt 0 ]]; then
  filtered_tests=()
  for requested in "${selected_tests[@]}"; do
    found=0
    for known in "${tests[@]}"; do
      if [[ "${requested}" == "${known}" ]]; then
        filtered_tests+=("${requested}")
        found=1
        break
      fi
    done
    if [[ ${found} -eq 0 ]]; then
      echo "run_xephyr_suite: unknown test '${requested}'" >&2
      echo "run_xephyr_suite: known tests: ${tests[*]}" >&2
      exit 2
    fi
  done
  tests=("${filtered_tests[@]}")
fi

echo "run_xephyr_suite: build_dir=${build_dir}"
echo "run_xephyr_suite: tests=${tests[*]}"
setup_suite_sandbox_env
echo "run_xephyr_suite: sandbox HOME=${suite_home}"
if [[ "${headless}" -eq 1 ]]; then
  start_headless_nested_x11_optional
  echo "run_xephyr_suite: headless DISPLAY=${DISPLAY} (wrapper only; each probe starts its own nested server via xserver_common.inc.sh)"
fi

if [[ "${no_build}" -eq 0 ]]; then
  meson compile -C "${build_dir}"
fi

for t in "${tests[@]}"; do
  echo "run_xephyr_suite: >>> ${t}"
  env -u DISPLAY -u XAUTHORITY \
    HOME="${suite_home}" \
    XDG_CONFIG_HOME="${suite_xdg_config_home}" \
    XDG_STATE_HOME="${suite_xdg_state_home}" \
    XDG_CACHE_HOME="${suite_xdg_cache_home}" \
    XDG_DATA_HOME="${suite_xdg_data_home}" \
    ZESTWM_HOST_DISPLAY="${ZESTWM_HOST_DISPLAY}" \
    ZESTWM_PREFER_XEPHYR="${ZESTWM_PREFER_XEPHYR:-}" \
    ZESTWM_NESTED_X11_DEBUG="${ZESTWM_NESTED_X11_DEBUG:-0}" \
    ZESTWM_TEST_TRACE="${ZESTWM_TEST_TRACE:-1}" \
    meson test -C "${build_dir}" "${t}" --print-errorlogs --no-rebuild
done

echo "run_xephyr_suite: pass"
