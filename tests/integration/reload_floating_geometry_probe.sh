#!/usr/bin/env bash
# MUST-RELOAD-009: floating client geometry survives WM reload via `_NET_ZEST_TREE_STATE |F(...)`.
# Spawns one client, toggles it floating, moves it to a known off-tile position, triggers a
# reload, then asserts the floating geometry (x/y/w/h) is preserved.
set -euo pipefail

build_dir="${1:-}"
if [[ -z "${build_dir}" ]]; then
  echo "reload-floating-geometry: missing build directory argument" >&2
  exit 2
fi

zestwm_bin="${build_dir}/zestwm"
zestctl_bin="${build_dir}/zestctl"
probe_client_bin="${build_dir}/focusurgent-urgent-client-test"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=tests/integration/xserver_common.inc.sh
source "${repo_root}/tests/integration/xserver_common.inc.sh"

base_conf_path="${repo_root}/examples/zestwm.conf"
probe_conf_path="${repo_root}/tests/integration/.reload-floating-geometry.conf"
client_ready="${repo_root}/tests/integration/.reload-floating-geometry.ready"

if [[ ! -x "${zestwm_bin}" || ! -x "${zestctl_bin}" || ! -x "${probe_client_bin}" ]]; then
  echo "reload-floating-geometry: missing built binaries in ${build_dir}" >&2
  exit 2
fi
if [[ ! -f "${base_conf_path}" ]]; then
  echo "reload-floating-geometry: missing config fixture ${base_conf_path}" >&2
  exit 2
fi

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "reload-floating-geometry: missing required command '$1'" >&2
    exit 77
  fi
}

zestwm_require_nested_x11 "reload-floating-geometry-probe"
need_cmd xdpyinfo
need_cmd xdotool

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
  echo "reload-floating-geometry: unable to allocate free X11 display" >&2
  rm -f "${probe_conf_path}"
  exit 2
fi

display=":${display_num}"
host_display="${ZESTWM_HOST_DISPLAY:-${DISPLAY:-}}"
if [[ -n "${host_display}" && "${display}" == "${host_display}" ]]; then
  echo "reload-floating-geometry: refusing host display '${display}'" >&2
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
    echo "reload-floating-geometry: nested X server exited during startup (skipping on this host)" >&2
    exit 77
  fi
  if DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
  echo "reload-floating-geometry: nested X server did not become ready (skipping on this host)" >&2
  exit 77
fi

DISPLAY="${display}" "${zestwm_bin}" -c "${probe_conf_path}" >/tmp/zestwm-reload-floating-geometry.log 2>&1 &
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
  echo "reload-floating-geometry: zestwm did not respond on ${display}" >&2
  exit 2
fi

assert_wm_alive() {
  if ! kill -0 "${wm_pid}" 2>/dev/null; then
    echo "reload-floating-geometry: zestwm crashed unexpectedly" >&2
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
    if DISPLAY="${display}" "${zestctl_bin}" clients | awk -v w="${win_hex}" '$1 == "win:" w { found=1 } END { exit(found ? 0 : 1) }'; then
      return 0
    fi
    sleep 0.05
  done
  return 1
}

# Distinct off-tile geometry (screen is 1280x720): well away from any tiled layout cell.
target_x=50
target_y=60
target_w=300
target_h=200

read_geometry() {
  local win_dec="$1"
  local geom
  geom="$(DISPLAY="${display}" xdotool getwindowgeometry --shell "${win_dec}" 2>/dev/null || true)"
  gx="$(awk -F= '/^X=/{print $2}' <<< "${geom}")"
  gy="$(awk -F= '/^Y=/{print $2}' <<< "${geom}")"
  gw="$(awk -F= '/^WIDTH=/{print $2}' <<< "${geom}")"
  gh="$(awk -F= '/^HEIGHT=/{print $2}' <<< "${geom}")"
}

run_dispatch workspace 1
DISPLAY="${display}" "${probe_client_bin}" --title reload-float-a --ready-file "${client_ready}" >/tmp/zestwm-reload-float-a.log 2>&1 &
client_pid=$!

win_dec="$(read_ready_window "${client_ready}" || true)"
if [[ -z "${win_dec}" ]]; then
  echo "reload-floating-geometry: failed to capture probe window id" >&2
  exit 2
fi
win_hex="$(printf '0x%08x' "${win_dec}")"

if ! wait_for_window_present "${win_hex}"; then
  echo "reload-floating-geometry: probe window not managed in time" >&2
  exit 1
fi

run_dispatch focuswindow "${win_hex}"
# Toggle floating, then drive the geometry to a known off-tile position/size via xdotool.
# Set size first, then position last, so the final ConfigureRequest carries the target x/y
# (a size-only request can otherwise reset position to 0,0 on some xdotool builds).
run_dispatch togglefloating
sleep 0.2
DISPLAY="${display}" xdotool windowsize "${win_dec}" "${target_w}" "${target_h}"
sleep 0.1
DISPLAY="${display}" xdotool windowmove "${win_dec}" "${target_x}" "${target_y}"
sleep 0.3

# Retry until the WM has applied the requested geometry (X11 ConfigureRequest is async).
for _ in {1..20}; do
  read_geometry "${win_dec}"
  if [[ -n "${gx}" && -n "${gy}" && -n "${gw}" && -n "${gh}" \
        && (( gx == target_x && gy == target_y && gw == target_w && gh == target_h )) ]]; then
    break
  fi
  DISPLAY="${display}" xdotool windowmove "${win_dec}" "${target_x}" "${target_y}"
  sleep 0.1
done
if [[ -z "${gx}" || -z "${gy}" || -z "${gw}" || -z "${gh}" ]]; then
  echo "reload-floating-geometry: failed to read pre-reload geometry" >&2
  exit 1
fi
if (( gx != target_x || gy != target_y || gw != target_w || gh != target_h )); then
  echo "reload-floating-geometry: pre-reload geometry mismatch (got x=${gx} y=${gy} w=${gw} h=${gh}, want ${target_x}/${target_y}/${target_w}/${target_h})" >&2
  exit 1
fi

run_dispatch reload
sleep 0.2
if ! wait_for_wm_ready; then
  echo "reload-floating-geometry: zestwm did not return after reload" >&2
  exit 1
fi
if ! wait_for_window_present "${win_hex}"; then
  echo "reload-floating-geometry: probe window not managed after reload" >&2
  exit 1
fi

read_geometry "${win_dec}"
if [[ -z "${gx}" || -z "${gy}" || -z "${gw}" || -z "${gh}" ]]; then
  echo "reload-floating-geometry: failed to read post-reload geometry" >&2
  exit 1
fi
if (( gx != target_x || gy != target_y || gw != target_w || gh != target_h )); then
  echo "reload-floating-geometry: floating geometry not preserved after reload (got x=${gx} y=${gy} w=${gw} h=${gh}, want ${target_x}/${target_y}/${target_w}/${target_h})" >&2
  exit 1
fi

echo "reload-floating-geometry: pass"
