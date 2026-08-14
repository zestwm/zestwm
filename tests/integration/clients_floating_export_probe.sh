#!/usr/bin/env bash
# Asserts `zestctl clients` reports runtime floating via `_NET_ZEST_FLOATING_CLIENTS`.
# Spawns one tiled client (`floating:no`), toggles floating (`floating:yes`), toggles back.
set -euo pipefail

build_dir="${1:-}"
if [[ -z "${build_dir}" ]]; then
  echo "clients-floating-export: missing build directory argument" >&2
  exit 2
fi

zestwm_bin="${build_dir}/zestwm"
zestctl_bin="${build_dir}/zestctl"
probe_client_bin="${build_dir}/focusurgent-urgent-client-test"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=tests/integration/xserver_common.inc.sh
source "${repo_root}/tests/integration/xserver_common.inc.sh"

base_conf_path="${repo_root}/examples/zestwm.conf"
probe_conf_path="${repo_root}/tests/integration/.clients-floating-export.conf"
client_ready="${repo_root}/tests/integration/.clients-floating-export.ready"

if [[ ! -x "${zestwm_bin}" || ! -x "${zestctl_bin}" || ! -x "${probe_client_bin}" ]]; then
  echo "clients-floating-export: missing built binaries in ${build_dir}" >&2
  exit 2
fi
if [[ ! -f "${base_conf_path}" ]]; then
  echo "clients-floating-export: missing config fixture ${base_conf_path}" >&2
  exit 2
fi

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "clients-floating-export: missing required command '$1'" >&2
    exit 77
  fi
}

zestwm_require_nested_x11 "clients-floating-export-probe"
need_cmd xdpyinfo

cp "${base_conf_path}" "${probe_conf_path}"
rm -f "${client_ready}"

display_num=150
while [[ ${display_num} -lt 170 ]]; do
  if [[ ! -S "/tmp/.X11-unix/X${display_num}" ]]; then
    break
  fi
  display_num=$((display_num + 1))
done
if [[ ${display_num} -ge 170 ]]; then
  echo "clients-floating-export: unable to allocate free X11 display" >&2
  rm -f "${probe_conf_path}"
  exit 2
fi

display=":${display_num}"
host_display="${ZESTWM_HOST_DISPLAY:-${DISPLAY:-}}"
if [[ -n "${host_display}" && "${display}" == "${host_display}" ]]; then
  echo "clients-floating-export: refusing host display '${display}'" >&2
  exit 2
fi
xvfb_pid=""
wm_pid=""
client_pid=""

cleanup() {
  if [[ -n "${client_pid}" ]] && kill -0 "${client_pid}" 2>/dev/null; then
    kill "${client_pid}" 2>/dev/null || true
    wait "${client_pid}" 2>/dev/null || true
  fi
  if [[ -n "${wm_pid}" ]] && kill -0 "${wm_pid}" 2>/dev/null; then
    kill "${wm_pid}" 2>/dev/null || true
    wait "${wm_pid}" 2>/dev/null || true
  fi
  if [[ -n "${xvfb_pid}" ]] && kill -0 "${xvfb_pid}" 2>/dev/null; then
    kill "${xvfb_pid}" 2>/dev/null || true
    wait "${xvfb_pid}" 2>/dev/null || true
  fi
  rm -f "${probe_conf_path}" "${client_ready}"
}
trap cleanup EXIT

zestwm_start_nested_x11 "${display}"

for _ in {1..40}; do
  if ! kill -0 "${xvfb_pid}" 2>/dev/null; then
    echo "clients-floating-export: nested X server exited during startup (skipping on this host)" >&2
    exit 77
  fi
  if DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
  echo "clients-floating-export: nested X server did not become ready (skipping on this host)" >&2
  exit 77
fi

DISPLAY="${display}" "${zestwm_bin}" -c "${probe_conf_path}" >/tmp/zestwm-clients-floating-export.log 2>&1 &
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
  echo "clients-floating-export: zestwm did not respond on ${display}" >&2
  exit 2
fi

assert_wm_alive() {
  if ! kill -0 "${wm_pid}" 2>/dev/null; then
    echo "clients-floating-export: zestwm crashed unexpectedly" >&2
    exit 1
  fi
}

run_dispatch() {
  DISPLAY="${display}" "${zestctl_bin}" dispatch "$@" >/dev/null
  assert_wm_alive
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

wait_for_window_present() {
  local win_hex="$1"
  for _ in {1..120}; do
    if DISPLAY="${display}" "${zestctl_bin}" clients | grep -Fq "win:${win_hex}"; then
      return 0
    fi
    sleep 0.05
  done
  return 1
}

client_floating_flag() {
  local win_hex="$1"
  DISPLAY="${display}" "${zestctl_bin}" clients | grep -F "win:${win_hex}" | grep -Eo 'floating:(yes|no)' | head -n1 | cut -d: -f2
}

wait_for_floating() {
  local win_hex="$1"
  local want="$2"
  for _ in {1..40}; do
    local got
    got="$(client_floating_flag "${win_hex}" || true)"
    if [[ "${got}" == "${want}" ]]; then
      return 0
    fi
    sleep 0.05
  done
  echo "clients-floating-export: expected floating:${want} for ${win_hex}, got '$(client_floating_flag "${win_hex}" || true)'" >&2
  DISPLAY="${display}" "${zestctl_bin}" clients >&2 || true
  return 1
}

run_dispatch workspace 1
DISPLAY="${display}" "${probe_client_bin}" --title clients-float-export --ready-file "${client_ready}" >/tmp/zestwm-clients-floating-export-client.log 2>&1 &
client_pid=$!

win_dec="$(read_ready_window "${client_ready}" || true)"
if [[ -z "${win_dec}" ]]; then
  echo "clients-floating-export: failed to capture probe window id" >&2
  exit 2
fi
win_hex="$(printf '0x%08x' "${win_dec}")"

if ! wait_for_window_present "${win_hex}"; then
  echo "clients-floating-export: probe window not managed in time" >&2
  exit 1
fi

if ! wait_for_floating "${win_hex}" "no"; then
  exit 1
fi

run_dispatch focuswindow "${win_hex}"
run_dispatch togglefloating
if ! wait_for_floating "${win_hex}" "yes"; then
  exit 1
fi

run_dispatch togglefloating
if ! wait_for_floating "${win_hex}" "no"; then
  exit 1
fi

echo "clients-floating-export: pass"
