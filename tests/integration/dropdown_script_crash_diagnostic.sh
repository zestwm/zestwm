#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-}"
if [[ -z "${build_dir}" ]]; then
  echo "dropdown-script-crash-diagnostic: missing build directory argument" >&2
  exit 2
fi

zestwm_bin="${build_dir}/zestwm"
zestctl_bin="${build_dir}/zestctl"
probe_client_bin="${build_dir}/focusurgent-urgent-client-test"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=tests/integration/xserver_common.inc.sh
source "${repo_root}/tests/integration/xserver_common.inc.sh"

base_conf_path="${repo_root}/examples/zestwm.conf"
probe_conf_path="${repo_root}/tests/integration/.dropdown-script-crash-diagnostic.conf"
ready_file="${repo_root}/tests/integration/.dropdown-script-client.ready"
ready_normal="${repo_root}/tests/integration/.dropdown-normal-client.ready"
special_tag="dropdown"
wm_log="/tmp/zestwm-dropdown-script-crash.log"

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "dropdown-script-crash-diagnostic: missing required command '$1'" >&2
    exit 77
  fi
}

zestwm_require_nested_x11 "dropdown-script-crash-diagnostic"
need_cmd xdpyinfo
need_cmd xdotool

cp "${base_conf_path}" "${probe_conf_path}"
cat >> "${probe_conf_path}" <<EOF
workspace = special:${special_tag}
binds {
  SUPER+G { togglegroup; }
}
EOF
rm -f "${ready_file}" "${ready_normal}" "${wm_log}"

display=":179"
xvfb_pid=""
wm_pid=""
client_pid=""

cleanup() {
  for pid in "${client_pid}" "${wm_pid}" "${xvfb_pid}"; do
    if [[ -n "${pid}" ]] && kill -0 "${pid}" 2>/dev/null; then
      kill "${pid}" 2>/dev/null || true
      wait "${pid}" 2>/dev/null || true
    fi
  done
  rm -f "${probe_conf_path}" "${ready_file}"
  rm -f "${ready_normal}"
}
trap cleanup EXIT

zestwm_start_nested_x11 "${display}"
for _ in {1..40}; do
  if DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
  echo "dropdown-script-crash-diagnostic: nested X server not ready" >&2
  exit 77
fi

DISPLAY="${display}" "${zestwm_bin}" -c "${probe_conf_path}" >"${wm_log}" 2>&1 &
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
    echo "dropdown-script-crash-diagnostic: wm crashed" >&2
    echo "--- wm log ---" >&2
    if [[ -f "${wm_log}" ]]; then
      sed -n '1,240p' "${wm_log}" >&2
    fi
    exit 1
  fi
}

read_ready_window() {
  local path="$1"
  for _ in {1..120}; do
    if [[ -s "${path}" ]]; then
      tr -d '[:space:]' < "${path}"
      return 0
    fi
    sleep 0.05
  done
  return 1
}

if ! wait_for_wm_ready; then
  echo "dropdown-script-crash-diagnostic: wm not ready" >&2
  exit 2
fi

# Emulate dropdown script: if overlay hidden, open special and spawn dropdown client class.
# 1) Normal client in workspace + togglegroup before opening dropdown.
DISPLAY="${display}" "${probe_client_bin}" --title normal-before-dropdown --ready-file "${ready_normal}" >/tmp/dropdown-diag-normal.log 2>&1 &
normal_dec="$(read_ready_window "${ready_normal}" || true)"
if [[ -z "${normal_dec}" ]]; then
  echo "dropdown-script-crash-diagnostic: failed to spawn normal client" >&2
  exit 2
fi
normal_hex="$(printf '0x%08x' "${normal_dec}")"
DISPLAY="${display}" "${zestctl_bin}" dispatch focuswindow "${normal_hex}" >/dev/null
assert_wm_alive
DISPLAY="${display}" xdotool key super+g
sleep 0.1
assert_wm_alive

# 2) Then open dropdown special and spawn dropdown-class client.
DISPLAY="${display}" "${zestctl_bin}" dispatch special "${special_tag}" >/dev/null
assert_wm_alive
sleep 0.2
DISPLAY="${display}" "${probe_client_bin}" --wm-class terminal-dropdown --wm-instance terminal-dropdown --title dropdown-diag --ready-file "${ready_file}" >/tmp/dropdown-diag-client.log 2>&1 &
client_pid=$!
win_dec="$(read_ready_window "${ready_file}" || true)"
if [[ -z "${win_dec}" ]]; then
  echo "dropdown-script-crash-diagnostic: failed to spawn dropdown client" >&2
  exit 2
fi
assert_wm_alive

# User-reported crash sequence continuation: toggle special with grouped normal client already present.
win_hex="$(printf '0x%08x' "${win_dec}")"
DISPLAY="${display}" "${zestctl_bin}" dispatch focuswindow "${win_hex}" >/dev/null
assert_wm_alive
DISPLAY="${display}" "${zestctl_bin}" dispatch special "${special_tag}" >/dev/null
assert_wm_alive

echo "dropdown-script-crash-diagnostic: pass (no crash), log=${wm_log}"
