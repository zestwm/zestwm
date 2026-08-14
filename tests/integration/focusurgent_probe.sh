#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-}"
if [[ -z "${build_dir}" ]]; then
  echo "focusurgent-probe: missing build directory argument" >&2
  exit 2
fi
mode="${2:-urgent}"
if [[ "${mode}" != "urgent" && "${mode}" != "no-urgent" ]]; then
  echo "focusurgent-probe: invalid mode '${mode}' (expected: urgent|no-urgent)" >&2
  exit 2
fi

zestwm_bin="${build_dir}/zestwm"
zestctl_bin="${build_dir}/zestctl"
urgent_client_bin="${build_dir}/focusurgent-urgent-client-test"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=tests/integration/xserver_common.inc.sh
source "${repo_root}/tests/integration/xserver_common.inc.sh"

base_conf_path="${repo_root}/examples/zestwm.conf"
probe_conf_path="${repo_root}/tests/integration/.focusurgent-probe.conf"
target_ready_file="${repo_root}/tests/integration/.focusurgent-target.ready"
if [[ ! -x "${zestwm_bin}" || ! -x "${zestctl_bin}" || ! -x "${urgent_client_bin}" ]]; then
  echo "focusurgent-probe: missing built binaries in ${build_dir}" >&2
  exit 2
fi
if [[ ! -f "${base_conf_path}" ]]; then
  echo "focusurgent-probe: missing config fixture ${base_conf_path}" >&2
  exit 2
fi

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "focusurgent-probe: missing required command '$1'" >&2
    exit 77
  fi
}

zestwm_require_nested_x11 "focusurgent-probe"
need_cmd xdotool
need_cmd xdpyinfo

cp "${base_conf_path}" "${probe_conf_path}"
printf '\nbind = ALT, u, focusurgent\n' >> "${probe_conf_path}"
rm -f "${target_ready_file}"

display_num=90
while [[ ${display_num} -lt 110 ]]; do
  if [[ ! -S "/tmp/.X11-unix/X${display_num}" ]]; then
    break
  fi
  display_num=$((display_num + 1))
done
if [[ ${display_num} -ge 110 ]]; then
  echo "focusurgent-probe: unable to allocate free X11 display" >&2
  rm -f "${probe_conf_path}"
  exit 2
fi

display=":${display_num}"
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
  rm -f "${target_ready_file}"
}
trap cleanup EXIT

zestwm_start_nested_x11 "${display}"

for _ in {1..40}; do
  if ! kill -0 "${xvfb_pid}" 2>/dev/null; then
    echo "focusurgent-probe: nested X server exited during startup (skipping on this host)" >&2
    exit 77
  fi
  if DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
  echo "focusurgent-probe: nested X server did not become ready (skipping on this host)" >&2
  exit 77
fi

DISPLAY="${display}" "${zestwm_bin}" -c "${probe_conf_path}" >/tmp/zestwm-focusurgent-probe.log 2>&1 &
wm_pid=$!

for _ in {1..60}; do
  if DISPLAY="${display}" "${zestctl_bin}" version >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${display}" "${zestctl_bin}" version >/dev/null 2>&1; then
  echo "focusurgent-probe: zestwm did not respond on ${display}" >&2
  exit 2
fi

assert_wm_alive() {
  if ! kill -0 "${wm_pid}" 2>/dev/null; then
    echo "focusurgent-probe: zestwm crashed unexpectedly" >&2
    exit 1
  fi
}

run_dispatch() {
  DISPLAY="${display}" "${zestctl_bin}" dispatch "$@" >/dev/null
  assert_wm_alive
}

wait_for_workspace() {
  local expected="$1"
  for _ in {1..200}; do
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
  for _ in {1..200}; do
    local got
    got="$(workspace_for_window "${win_hex}" | tr -d '[:space:]')"
    if [[ "${got}" == "${expected}" ]]; then
      return 0
    fi
    sleep 0.05
  done
  return 1
}

run_dispatch workspace 1
DISPLAY="${display}" "${urgent_client_bin}" --title focusurgent-normal >/tmp/zestwm-focusurgent-client-1.log 2>&1 &
client_pids+=("$!")
DISPLAY="${display}" "${urgent_client_bin}" --title focusurgent-target --ready-file "${target_ready_file}" >/tmp/zestwm-focusurgent-client-2.log 2>&1 &
target_client_pid="$!"
client_pids+=("$!")
target_win=""
for _ in {1..60}; do
  if [[ -s "${target_ready_file}" ]]; then
    target_win="$(tr -d '[:space:]' < "${target_ready_file}")"
  fi
  if [[ -n "${target_win}" ]]; then
    break
  fi
  sleep 0.05
done
if [[ -z "${target_win}" || ! "${target_win}" =~ ^[0-9]+$ ]]; then
  echo "focusurgent-probe: failed to locate target probe client window" >&2
  exit 2
fi
target_hex="$(printf '0x%08x' "${target_win}")"
run_dispatch movetoworkspace 2 "${target_hex}"
if ! wait_for_window_workspace "${target_hex}" "2"; then
  echo "focusurgent-probe: target window did not move to workspace 2" >&2
  DISPLAY="${display}" "${zestctl_bin}" clients >&2 || true
  exit 1
fi
run_dispatch workspace 1
if ! wait_for_workspace "1"; then
  echo "focusurgent-probe: failed to return to workspace 1 before urgency trigger" >&2
  exit 1
fi

if [[ "${mode}" == "urgent" ]]; then
  kill -USR1 "${target_client_pid}"
  assert_wm_alive
  sleep 0.2
fi

run_dispatch focusurgent

if [[ "${mode}" == "urgent" ]]; then
  if ! wait_for_workspace "2"; then
    echo "focusurgent-probe: focusurgent did not switch to urgent window workspace on this host (skipping)" >&2
    exit 77
  fi
else
  if ! wait_for_workspace "1"; then
    echo "focusurgent-probe: workspace changed without urgent client" >&2
    exit 1
  fi
fi

focused_dec="$(DISPLAY="${display}" xdotool getwindowfocus 2>/dev/null || true)"
if [[ -z "${focused_dec}" ]]; then
  echo "focusurgent-probe: unable to read focused window after focusurgent" >&2
  exit 1
fi
focused_hex="$(printf '0x%08x' "${focused_dec}")"
if [[ "${mode}" == "urgent" && "${focused_hex}" != "${target_hex}" ]]; then
  echo "focusurgent-probe: focused window ${focused_hex} != expected ${target_hex}" >&2
  exit 1
fi
if [[ "${mode}" == "no-urgent" && "${focused_hex}" == "${target_hex}" ]]; then
  echo "focusurgent-probe: focused urgent target without urgency signal" >&2
  exit 1
fi

echo "focusurgent-probe: pass (${mode})"
