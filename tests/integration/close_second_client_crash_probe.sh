#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-}"
if [[ -z "${build_dir}" ]]; then
  echo "close-second-client-crash: missing build directory argument" >&2
  exit 2
fi

zestwm_bin="${build_dir}/zestwm"
zestctl_bin="${build_dir}/zestctl"
probe_client_bin="${build_dir}/focusurgent-urgent-client-test"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=tests/integration/xserver_common.inc.sh
source "${repo_root}/tests/integration/xserver_common.inc.sh"

base_conf_path="${repo_root}/examples/zestwm.conf"
probe_conf_path="${repo_root}/tests/integration/.close-second-client-crash.conf"
client_a_ready="${repo_root}/tests/integration/.close-second-client-a.ready"
client_b_ready="${repo_root}/tests/integration/.close-second-client-b.ready"

if [[ ! -x "${zestwm_bin}" || ! -x "${zestctl_bin}" || ! -x "${probe_client_bin}" ]]; then
  echo "close-second-client-crash: missing built binaries in ${build_dir}" >&2
  exit 2
fi
if [[ ! -f "${base_conf_path}" ]]; then
  echo "close-second-client-crash: missing config fixture ${base_conf_path}" >&2
  exit 2
fi

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "close-second-client-crash: missing required command '$1'" >&2
    exit 77
  fi
}

zestwm_require_nested_x11 "close-second-client-crash-probe"
need_cmd xdpyinfo

cp "${base_conf_path}" "${probe_conf_path}"
rm -f "${client_a_ready}" "${client_b_ready}"

display_num=130
while [[ ${display_num} -lt 150 ]]; do
  if [[ ! -S "/tmp/.X11-unix/X${display_num}" ]]; then
    break
  fi
  display_num=$((display_num + 1))
done
if [[ ${display_num} -ge 150 ]]; then
  echo "close-second-client-crash: unable to allocate free X11 display" >&2
  rm -f "${probe_conf_path}"
  exit 2
fi

display=":${display_num}"
xvfb_pid=""
wm_pid=""
client_a_pid=""
client_b_pid=""

cleanup() {
  if [[ -n "${client_a_pid}" ]] && kill -0 "${client_a_pid}" 2>/dev/null; then
    kill "${client_a_pid}" 2>/dev/null || true
    wait "${client_a_pid}" 2>/dev/null || true
  fi
  if [[ -n "${client_b_pid}" ]] && kill -0 "${client_b_pid}" 2>/dev/null; then
    kill "${client_b_pid}" 2>/dev/null || true
    wait "${client_b_pid}" 2>/dev/null || true
  fi
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
    echo "close-second-client-crash: nested X server exited during startup (skipping on this host)" >&2
    exit 77
  fi
  if DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
  echo "close-second-client-crash: nested X server did not become ready (skipping on this host)" >&2
  exit 77
fi

DISPLAY="${display}" "${zestwm_bin}" -c "${probe_conf_path}" >/tmp/zestwm-close-second-client-crash.log 2>&1 &
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
    echo "close-second-client-crash: zestwm crashed unexpectedly" >&2
    exit 1
  fi
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

window_is_present() {
  local win_hex="$1"
  DISPLAY="${display}" "${zestctl_bin}" clients | awk -v w="${win_hex}" '$1 == "win:" w { found=1 } END { exit(found ? 0 : 1) }'
}

wait_for_window_present() {
  local win_hex="$1"
  for _ in {1..120}; do
    if window_is_present "${win_hex}"; then
      return 0
    fi
    sleep 0.05
  done
  return 1
}

wait_for_window_absent() {
  local win_hex="$1"
  for _ in {1..120}; do
    if ! window_is_present "${win_hex}"; then
      return 0
    fi
    sleep 0.05
  done
  return 1
}

if ! wait_for_wm_ready; then
  echo "close-second-client-crash: zestwm did not respond on ${display}" >&2
  exit 2
fi

DISPLAY="${display}" "${probe_client_bin}" --title close-second-client-a --ready-file "${client_a_ready}" >/tmp/zestwm-close-second-client-a.log 2>&1 &
client_a_pid=$!
DISPLAY="${display}" "${probe_client_bin}" --title close-second-client-b --ready-file "${client_b_ready}" >/tmp/zestwm-close-second-client-b.log 2>&1 &
client_b_pid=$!

client_a_dec="$(read_ready_window "${client_a_ready}" || true)"
client_b_dec="$(read_ready_window "${client_b_ready}" || true)"
if [[ -z "${client_a_dec}" || -z "${client_b_dec}" ]]; then
  echo "close-second-client-crash: failed to capture probe window ids" >&2
  exit 2
fi

client_a_hex="$(printf '0x%08x' "${client_a_dec}")"
client_b_hex="$(printf '0x%08x' "${client_b_dec}")"

if ! wait_for_window_present "${client_a_hex}" || ! wait_for_window_present "${client_b_hex}"; then
  echo "close-second-client-crash: probe windows not managed in time" >&2
  exit 1
fi

# Repro flow: open app, open second app, close second app.
kill "${client_b_pid}" 2>/dev/null || true
wait "${client_b_pid}" 2>/dev/null || true
client_b_pid=""

if ! wait_for_window_absent "${client_b_hex}"; then
  echo "close-second-client-crash: closed client still present after kill" >&2
  exit 1
fi
assert_wm_alive

# WM must still respond to IPC after close.
if ! DISPLAY="${display}" "${zestctl_bin}" clients >/dev/null 2>&1; then
  echo "close-second-client-crash: zestctl clients failed after closing second client" >&2
  exit 1
fi
assert_wm_alive

echo "close-second-client-crash: pass"
