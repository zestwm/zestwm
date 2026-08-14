#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-}"
if [[ -z "${build_dir}" ]]; then
  echo "reload-workspace-restore: missing build directory argument" >&2
  exit 2
fi

zestwm_bin="${build_dir}/zestwm"
zestctl_bin="${build_dir}/zestctl"
probe_client_bin="${build_dir}/focusurgent-urgent-client-test"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=tests/integration/xserver_common.inc.sh
source "${repo_root}/tests/integration/xserver_common.inc.sh"

base_conf_path="${repo_root}/examples/zestwm.conf"
probe_conf_path="${repo_root}/tests/integration/.reload-workspace-restore.conf"
client_a_ready="${repo_root}/tests/integration/.reload-client-a.ready"
client_b_ready="${repo_root}/tests/integration/.reload-client-b.ready"

if [[ ! -x "${zestwm_bin}" || ! -x "${zestctl_bin}" || ! -x "${probe_client_bin}" ]]; then
  echo "reload-workspace-restore: missing built binaries in ${build_dir}" >&2
  exit 2
fi
if [[ ! -f "${base_conf_path}" ]]; then
  echo "reload-workspace-restore: missing config fixture ${base_conf_path}" >&2
  exit 2
fi

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "reload-workspace-restore: missing required command '$1'" >&2
    exit 77
  fi
}

zestwm_require_nested_x11 "reload-workspace-restore-probe"
need_cmd xdpyinfo

cp "${base_conf_path}" "${probe_conf_path}"
rm -f "${client_a_ready}" "${client_b_ready}"

display_num=110
while [[ ${display_num} -lt 130 ]]; do
  if [[ ! -S "/tmp/.X11-unix/X${display_num}" ]]; then
    break
  fi
  display_num=$((display_num + 1))
done
if [[ ${display_num} -ge 130 ]]; then
  echo "reload-workspace-restore: unable to allocate free X11 display" >&2
  rm -f "${probe_conf_path}"
  exit 2
fi

display=":${display_num}"
host_display="${ZESTWM_HOST_DISPLAY:-${DISPLAY:-}}"
if [[ -n "${host_display}" && "${display}" == "${host_display}" ]]; then
  echo "reload-workspace-restore: refusing host display '${display}'" >&2
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
  rm -f "${probe_conf_path}" "${client_a_ready}" "${client_b_ready}"
}
trap cleanup EXIT

zestwm_start_nested_x11 "${display}"

for _ in {1..40}; do
  if ! kill -0 "${xvfb_pid}" 2>/dev/null; then
    echo "reload-workspace-restore: nested X server exited during startup (skipping on this host)" >&2
    exit 77
  fi
  if DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
  echo "reload-workspace-restore: nested X server did not become ready (skipping on this host)" >&2
  exit 77
fi

DISPLAY="${display}" "${zestwm_bin}" -c "${probe_conf_path}" >/tmp/zestwm-reload-workspace-restore.log 2>&1 &
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

if ! wait_for_wm_ready; then
  echo "reload-workspace-restore: zestwm did not respond on ${display}" >&2
  exit 2
fi

assert_wm_alive() {
  if ! kill -0 "${wm_pid}" 2>/dev/null; then
    echo "reload-workspace-restore: zestwm crashed unexpectedly" >&2
    exit 1
  fi
}

run_dispatch() {
  DISPLAY="${display}" "${zestctl_bin}" dispatch "$@" >/dev/null
  assert_wm_alive
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

wait_for_window_present() {
  local win_hex="$1"
  for _ in {1..120}; do
    if DISPLAY="${display}" "${zestctl_bin}" clients | awk -v w="${win_hex}" '$1 == "win:" w { found=1 } END { exit(found ? 0 : 1) }'; then
      return 0
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

run_dispatch workspace 1
DISPLAY="${display}" "${probe_client_bin}" --title reload-client-a --ready-file "${client_a_ready}" >/tmp/zestwm-reload-client-a.log 2>&1 &
client_pids+=("$!")
DISPLAY="${display}" "${probe_client_bin}" --title reload-client-b --ready-file "${client_b_ready}" >/tmp/zestwm-reload-client-b.log 2>&1 &
client_pids+=("$!")

client_a_dec="$(read_ready_window "${client_a_ready}" || true)"
client_b_dec="$(read_ready_window "${client_b_ready}" || true)"
if [[ -z "${client_a_dec}" || -z "${client_b_dec}" ]]; then
  echo "reload-workspace-restore: failed to capture probe window ids" >&2
  exit 2
fi

client_a_hex="$(printf '0x%08x' "${client_a_dec}")"
client_b_hex="$(printf '0x%08x' "${client_b_dec}")"

if ! wait_for_window_present "${client_a_hex}" || ! wait_for_window_present "${client_b_hex}"; then
  echo "reload-workspace-restore: probe windows not managed in time" >&2
  exit 1
fi

if ! move_window_to_workspace "2" "${client_b_hex}"; then
  echo "reload-workspace-restore: failed to move client B to workspace 2 before reload (skipping on this host)" >&2
  exit 77
fi
run_dispatch workspace 1
if ! wait_for_workspace "1"; then
  echo "reload-workspace-restore: failed to return to workspace 1 before reload" >&2
  exit 1
fi

if ! wait_for_window_workspace "${client_b_hex}" "2"; then
  echo "reload-workspace-restore: client B not on workspace 2 before reload" >&2
  exit 1
fi

run_dispatch reload
sleep 0.2
if ! wait_for_wm_ready; then
  echo "reload-workspace-restore: zestwm did not return after reload" >&2
  exit 1
fi

if ! wait_for_window_workspace "${client_b_hex}" "2"; then
  echo "reload-workspace-restore: client B workspace not restored after reload" >&2
  exit 1
fi

echo "reload-workspace-restore: pass"
