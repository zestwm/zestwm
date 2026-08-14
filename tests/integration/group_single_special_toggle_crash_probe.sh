#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-}"
if [[ -z "${build_dir}" ]]; then
  echo "group-single-special-toggle-crash: missing build directory argument" >&2
  exit 2
fi

zestwm_bin="${build_dir}/zestwm"
zestctl_bin="${build_dir}/zestctl"
probe_client_bin="${build_dir}/focusurgent-urgent-client-test"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=tests/integration/xserver_common.inc.sh
source "${repo_root}/tests/integration/xserver_common.inc.sh"

base_conf_path="${repo_root}/examples/zestwm.conf"
probe_conf_path="${repo_root}/tests/integration/.group-single-special-toggle-crash.conf"
ready_a="${repo_root}/tests/integration/.group-single-special-toggle-a.ready"
special_tag="group-single-special-crash"

if [[ ! -x "${zestwm_bin}" || ! -x "${zestctl_bin}" || ! -x "${probe_client_bin}" ]]; then
  echo "group-single-special-toggle-crash: missing built binaries in ${build_dir}" >&2
  exit 2
fi
if [[ ! -f "${base_conf_path}" ]]; then
  echo "group-single-special-toggle-crash: missing config fixture ${base_conf_path}" >&2
  exit 2
fi

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "group-single-special-toggle-crash: missing required command '$1'" >&2
    exit 77
  fi
}

zestwm_require_nested_x11 "group-single-special-toggle-crash-probe"
need_cmd xdpyinfo
need_cmd xdotool

cp "${base_conf_path}" "${probe_conf_path}"
cat >> "${probe_conf_path}" <<EOF
workspace = special:${special_tag}
workspace = special:
binds {
  SUPER+G { togglegroup; }
  SUPER+S { togglespecialworkspace ${special_tag}; }
  SUPER+D { workspace special:; }
}
EOF
rm -f "${ready_a}"

display_num=160
while [[ ${display_num} -lt 180 ]]; do
  if [[ ! -S "/tmp/.X11-unix/X${display_num}" ]]; then
    break
  fi
  display_num=$((display_num + 1))
done
if [[ ${display_num} -ge 180 ]]; then
  echo "group-single-special-toggle-crash: unable to allocate free X11 display" >&2
  rm -f "${probe_conf_path}"
  exit 2
fi

display=":${display_num}"
xvfb_pid=""
wm_pid=""
client_a_pid=""

cleanup() {
  if [[ -n "${client_a_pid}" ]] && kill -0 "${client_a_pid}" 2>/dev/null; then
    kill "${client_a_pid}" 2>/dev/null || true
    wait "${client_a_pid}" 2>/dev/null || true
  fi
  if [[ -n "${wm_pid}" ]] && kill -0 "${wm_pid}" 2>/dev/null; then
    kill "${wm_pid}" 2>/dev/null || true
    wait "${wm_pid}" 2>/dev/null || true
  fi
  if [[ -n "${xvfb_pid}" ]] && kill -0 "${xvfb_pid}" 2>/dev/null; then
    kill "${xvfb_pid}" 2>/dev/null || true
    wait "${xvfb_pid}" 2>/dev/null || true
  fi
  rm -f "${probe_conf_path}" "${ready_a}"
}
trap cleanup EXIT

zestwm_start_nested_x11 "${display}"

for _ in {1..40}; do
  if ! kill -0 "${xvfb_pid}" 2>/dev/null; then
    echo "group-single-special-toggle-crash: nested X server exited during startup (skipping on this host)" >&2
    exit 77
  fi
  if DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
  echo "group-single-special-toggle-crash: nested X server did not become ready (skipping on this host)" >&2
  exit 77
fi

DISPLAY="${display}" "${zestwm_bin}" -c "${probe_conf_path}" >/tmp/zestwm-group-single-special-toggle-crash.log 2>&1 &
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
    echo "group-single-special-toggle-crash: zestwm crashed unexpectedly" >&2
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

if ! wait_for_wm_ready; then
  echo "group-single-special-toggle-crash: zestwm did not respond on ${display}" >&2
  exit 2
fi

DISPLAY="${display}" "${probe_client_bin}" --title "group-single-special-a" --ready-file "${ready_a}" >/tmp/zestwm-group-single-special-a.log 2>&1 &
client_a_pid=$!
win_a_dec="$(read_ready_window "${ready_a}" || true)"
if [[ -z "${win_a_dec}" ]]; then
  echo "group-single-special-toggle-crash: failed to capture client window id" >&2
  exit 2
fi
win_a_hex="$(printf '0x%08x' "${win_a_dec}")"

DISPLAY="${display}" "${zestctl_bin}" dispatch focuswindow "${win_a_hex}" >/dev/null
assert_wm_alive
DISPLAY="${display}" xdotool key super+g
sleep 0.1
assert_wm_alive

# Crash reproducer: with one grouped client, keyboard workspace special (default tag) must not crash.
DISPLAY="${display}" xdotool key super+d
assert_wm_alive
if ! wait_for_wm_ready; then
  echo "group-single-special-toggle-crash: zestwm stopped responding after workspace special: toggle" >&2
  exit 1
fi
assert_wm_alive

# Keep tagged togglespecialworkspace checks too (regression guard).
DISPLAY="${display}" xdotool key super+s
assert_wm_alive
if ! wait_for_wm_ready; then
  echo "group-single-special-toggle-crash: zestwm stopped responding after special toggle" >&2
  exit 1
fi
assert_wm_alive
DISPLAY="${display}" xdotool key super+s
assert_wm_alive
if ! wait_for_wm_ready; then
  echo "group-single-special-toggle-crash: zestwm stopped responding after closing special toggle" >&2
  exit 1
fi
assert_wm_alive

echo "group-single-special-toggle-crash: pass"
