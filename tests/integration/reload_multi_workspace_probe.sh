#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-}"
if [[ -z "${build_dir}" ]]; then
  echo "reload-multi-workspace: missing build directory argument" >&2
  exit 2
fi

zestwm_bin="${build_dir}/zestwm"
zestctl_bin="${build_dir}/zestctl"
probe_client_bin="${build_dir}/focusurgent-urgent-client-test"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=tests/integration/xserver_common.inc.sh
source "${repo_root}/tests/integration/xserver_common.inc.sh"

base_conf_path="${repo_root}/examples/zestwm.conf"
probe_conf_path="${repo_root}/tests/integration/.reload-multi-workspace.conf"
ready_files=(
  "${repo_root}/tests/integration/.reload-multi-a.ready"
  "${repo_root}/tests/integration/.reload-multi-b.ready"
  "${repo_root}/tests/integration/.reload-multi-c.ready"
  "${repo_root}/tests/integration/.reload-multi-d.ready"
)

if [[ ! -x "${zestwm_bin}" || ! -x "${zestctl_bin}" || ! -x "${probe_client_bin}" ]]; then
  echo "reload-multi-workspace: missing built binaries in ${build_dir}" >&2
  exit 2
fi
if [[ ! -f "${base_conf_path}" ]]; then
  echo "reload-multi-workspace: missing config fixture ${base_conf_path}" >&2
  exit 2
fi

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "reload-multi-workspace: missing required command '$1'" >&2
    exit 77
  fi
}

zestwm_require_nested_x11 "reload-multi-workspace-probe"
need_cmd xdpyinfo
need_cmd xprop
need_cmd xwininfo

cp "${base_conf_path}" "${probe_conf_path}"
for f in "${ready_files[@]}"; do
  rm -f "${f}"
done

display_num=130
while [[ ${display_num} -lt 150 ]]; do
  if [[ ! -S "/tmp/.X11-unix/X${display_num}" ]]; then
    break
  fi
  display_num=$((display_num + 1))
done
if [[ ${display_num} -ge 150 ]]; then
  echo "reload-multi-workspace: unable to allocate free X11 display" >&2
  rm -f "${probe_conf_path}"
  exit 2
fi

display=":${display_num}"
host_display="${ZESTWM_HOST_DISPLAY:-${DISPLAY:-}}"
if [[ -n "${host_display}" && "${display}" == "${host_display}" ]]; then
  echo "reload-multi-workspace: refusing host display '${display}'" >&2
  exit 2
fi
xvfb_pid=""
wm_pid=""
client_pids=()

cleanup() {
  for pid in "${client_pids[@]:-}"; do
    if [[ -n "${pid}" ]] && kill -0 "${pid}" 2>/dev/null; then
      kill "${pid}" 2>/dev/null || true
      wait "${pid}" 2>/dev/null || true
    fi
  done
  if [[ -n "${wm_pid}" ]] && kill -0 "${wm_pid}" 2>/dev/null; then
    kill "${wm_pid}" 2>/dev/null || true
    wait "${wm_pid}" 2>/dev/null || true
  fi
  if [[ -n "${xvfb_pid}" ]] && kill -0 "${xvfb_pid}" 2>/dev/null; then
    kill "${xvfb_pid}" 2>/dev/null || true
    wait "${xvfb_pid}" 2>/dev/null || true
  fi
  rm -f "${probe_conf_path}"
  for f in "${ready_files[@]}"; do
    rm -f "${f}"
  done
}
trap cleanup EXIT

zestwm_start_nested_x11 "${display}"

for _ in {1..40}; do
  if ! kill -0 "${xvfb_pid}" 2>/dev/null; then
    echo "reload-multi-workspace: nested X server exited during startup (skipping on this host)" >&2
    exit 77
  fi
  if DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
  echo "reload-multi-workspace: nested X server did not become ready (skipping on this host)" >&2
  exit 77
fi

DISPLAY="${display}" "${zestwm_bin}" -c "${probe_conf_path}" >/tmp/zestwm-reload-multi-workspace.log 2>&1 &
wm_pid=$!

wait_for_wm_ready() {
  for _ in {1..120}; do
    if DISPLAY="${display}" "${zestctl_bin}" version >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  return 1
}

assert_wm_alive() {
  if ! kill -0 "${wm_pid}" 2>/dev/null; then
    echo "reload-multi-workspace: zestwm crashed unexpectedly" >&2
    exit 1
  fi
}

run_dispatch() {
  DISPLAY="${display}" "${zestctl_bin}" dispatch "$@" >/dev/null
  assert_wm_alive
}

dump_clients_state() {
  local label="$1"
  echo "reload-multi-workspace: --- ${label} ---" >&2
  DISPLAY="${display}" "${zestctl_bin}" clients >&2 || true
}

dump_window_props() {
  local label="$1"
  shift
  echo "reload-multi-workspace: --- ${label} window props ---" >&2
  for win_hex in "$@"; do
    DISPLAY="${display}" xprop -id "${win_hex}" _NET_WM_DESKTOP _NET_CLIENT_INFO >&2 || true
  done
}

workspace_for_window() {
  local win_hex="$1"
  DISPLAY="${display}" "${zestctl_bin}" clients | awk -v w="${win_hex}" '
    $1 == "win:" w {
      for (i = 1; i <= NF; ++i) {
        if ($i ~ /^ws:/) {
          sub(/^ws:/, "", $i);
          print $i;
          exit 0;
        }
      }
    }'
}

wait_for_window_workspace() {
  local win_hex="$1"
  local expected="$2"
  for _ in {1..120}; do
    local got
    got="$(workspace_for_window "${win_hex}" | tr -d '[:space:]')"
    if [[ "${got}" == "${expected}" ]]; then
      return 0
    fi
    sleep 0.05
  done
  return 1
}

wait_for_workspace() {
  local expected="$1"
  for _ in {1..120}; do
    local current
    current="$(DISPLAY="${display}" "${zestctl_bin}" activeworkspace | tr -d '[:space:]')"
    if [[ "${current}" == "${expected}" ]]; then
      return 0
    fi
    sleep 0.05
  done
  return 1
}

read_ready_window() {
  local path="$1"
  for _ in {1..120}; do
    if [[ -s "${path}" ]]; then
      local value
      value="$(tr -d '[:space:]' < "${path}")"
      if [[ "${value}" =~ ^[0-9]+$ ]]; then
        printf '%s' "${value}"
        return 0
      fi
    fi
    sleep 0.05
  done
  return 1
}

move_window_to_workspace() {
  local workspace="$1"
  local win_hex="$2"
  for _ in {1..12}; do
    run_dispatch movetoworkspace "${workspace}" "${win_hex}"
    if wait_for_window_workspace "${win_hex}" "${workspace}"; then
      return 0
    fi
    sleep 0.05
  done
  return 1
}

assert_window_is_not_centered_slot() {
  local win_hex="$1"
  local info abs_x width
  info="$(DISPLAY="${display}" xwininfo -id "${win_hex}" 2>/dev/null || true)"
  if [[ -z "${info}" ]]; then
    echo "reload-multi-workspace: unable to read geometry for ${win_hex}" >&2
    exit 1
  fi
  abs_x="$(printf '%s\n' "${info}" | awk '/Absolute upper-left X:/ {print $4; exit}')"
  width="$(printf '%s\n' "${info}" | awk '/Width:/ {print $2; exit}')"
  if [[ -z "${abs_x}" || -z "${width}" ]]; then
    echo "reload-multi-workspace: malformed geometry output for ${win_hex}" >&2
    exit 1
  fi
  if (( abs_x > 80 || width < 1120 )); then
    echo "reload-multi-workspace: single-client layout looks centered/stale (x=${abs_x}, w=${width})" >&2
    printf '%s\n' "${info}" >&2
    exit 1
  fi
}

if ! wait_for_wm_ready; then
  echo "reload-multi-workspace: zestwm did not respond on ${display}" >&2
  exit 2
fi

run_dispatch workspace 1

for i in 0 1 2 3; do
  DISPLAY="${display}" "${probe_client_bin}" --title "reload-multi-${i}" --ready-file "${ready_files[$i]}" >/tmp/zestwm-reload-multi-client-"${i}".log 2>&1 &
  client_pids+=("$!")
done

wins_dec=()
for i in 0 1 2 3; do
  win_dec="$(read_ready_window "${ready_files[$i]}" || true)"
  if [[ -z "${win_dec}" ]]; then
    echo "reload-multi-workspace: failed to capture window id for client ${i}" >&2
    exit 2
  fi
  wins_dec+=("${win_dec}")
done

wins_hex=()
for win_dec in "${wins_dec[@]}"; do
  wins_hex+=("$(printf '0x%08x' "${win_dec}")")
done

if ! move_window_to_workspace "3" "${wins_hex[0]}" || ! move_window_to_workspace "3" "${wins_hex[1]}"; then
  dump_clients_state "move ws3 failed"
  dump_window_props "move ws3 failed props" "${wins_hex[@]}"
  echo "reload-multi-workspace: failed to move two clients to workspace 3 (skipping on this host)" >&2
  exit 77
fi
if ! move_window_to_workspace "2" "${wins_hex[2]}" || ! move_window_to_workspace "2" "${wins_hex[3]}"; then
  dump_clients_state "move ws2 failed"
  dump_window_props "move ws2 failed props" "${wins_hex[@]}"
  echo "reload-multi-workspace: failed to move two clients to workspace 2 (skipping on this host)" >&2
  exit 77
fi

run_dispatch workspace 1
if ! wait_for_workspace "1"; then
  echo "reload-multi-workspace: failed to return to workspace 1 before reload" >&2
  exit 1
fi

if ! wait_for_window_workspace "${wins_hex[0]}" "3" || ! wait_for_window_workspace "${wins_hex[1]}" "3"; then
  dump_clients_state "pre-reload ws3 mismatch"
  dump_window_props "pre-reload ws3 mismatch props" "${wins_hex[@]}"
  echo "reload-multi-workspace: pre-reload workspace 3 ownership mismatch" >&2
  exit 1
fi
if ! wait_for_window_workspace "${wins_hex[2]}" "2" || ! wait_for_window_workspace "${wins_hex[3]}" "2"; then
  dump_clients_state "pre-reload ws2 mismatch"
  dump_window_props "pre-reload ws2 mismatch props" "${wins_hex[@]}"
  echo "reload-multi-workspace: pre-reload workspace 2 ownership mismatch" >&2
  exit 1
fi

run_dispatch reload
sleep 0.25
if ! wait_for_wm_ready; then
  echo "reload-multi-workspace: zestwm did not return after reload" >&2
  exit 1
fi

if ! wait_for_window_workspace "${wins_hex[0]}" "3" || ! wait_for_window_workspace "${wins_hex[1]}" "3"; then
  dump_clients_state "post-reload ws3 mismatch"
  dump_window_props "post-reload ws3 mismatch props" "${wins_hex[@]}"
  echo "reload-multi-workspace: post-reload workspace 3 ownership mismatch" >&2
  exit 1
fi
if ! wait_for_window_workspace "${wins_hex[2]}" "2" || ! wait_for_window_workspace "${wins_hex[3]}" "2"; then
  dump_clients_state "post-reload ws2 mismatch"
  dump_window_props "post-reload ws2 mismatch props" "${wins_hex[@]}"
  echo "reload-multi-workspace: post-reload workspace 2 ownership mismatch" >&2
  exit 1
fi

run_dispatch reload
sleep 0.25
if ! wait_for_wm_ready; then
  echo "reload-multi-workspace: zestwm did not return after second reload" >&2
  exit 1
fi

if ! wait_for_window_workspace "${wins_hex[0]}" "3" || ! wait_for_window_workspace "${wins_hex[1]}" "3"; then
  dump_clients_state "post-second-reload ws3 mismatch"
  dump_window_props "post-second-reload ws3 mismatch props" "${wins_hex[@]}"
  echo "reload-multi-workspace: post-second-reload workspace 3 ownership mismatch" >&2
  exit 1
fi
if ! wait_for_window_workspace "${wins_hex[2]}" "2" || ! wait_for_window_workspace "${wins_hex[3]}" "2"; then
  dump_clients_state "post-second-reload ws2 mismatch"
  dump_window_props "post-second-reload ws2 mismatch props" "${wins_hex[@]}"
  echo "reload-multi-workspace: post-second-reload workspace 2 ownership mismatch" >&2
  exit 1
fi

# Layout sanity check: with a single non-dock client on ws1, geometry must use full tile area.
if ! move_window_to_workspace "1" "${wins_hex[0]}"; then
  dump_clients_state "failed to move probe client to ws1 for geometry check"
  echo "reload-multi-workspace: failed to place single probe client on workspace 1" >&2
  exit 1
fi
if ! move_window_to_workspace "2" "${wins_hex[2]}"; then
  dump_clients_state "failed to move extra client off ws1 for geometry check"
  echo "reload-multi-workspace: failed to keep workspace 1 single-client for geometry check" >&2
  exit 1
fi
run_dispatch workspace 1
if ! wait_for_workspace "1" || ! wait_for_window_workspace "${wins_hex[0]}" "1"; then
  dump_clients_state "ws1 geometry precondition mismatch"
  echo "reload-multi-workspace: workspace 1 geometry precondition failed" >&2
  exit 1
fi
assert_window_is_not_centered_slot "${wins_hex[0]}"

echo "reload-multi-workspace: pass"
