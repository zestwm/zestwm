#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-}"
if [[ -z "${build_dir}" ]]; then
  echo "ewmh-dock-gap: missing build directory argument" >&2
  exit 2
fi

zestwm_bin="${build_dir}/zestwm"
zestctl_bin="${build_dir}/zestctl"
probe_client_bin="${build_dir}/focusurgent-urgent-client-test"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=tests/integration/xserver_common.inc.sh
source "${repo_root}/tests/integration/xserver_common.inc.sh"

base_conf_path="${repo_root}/examples/zestwm.conf"
probe_conf_path="${repo_root}/tests/integration/.ewmh-dock-gap.conf"
ready_files=(
  "${repo_root}/tests/integration/.ewmh-gap-a.ready"
  "${repo_root}/tests/integration/.ewmh-gap-b.ready"
  "${repo_root}/tests/integration/.ewmh-gap-c.ready"
)

if [[ ! -x "${zestwm_bin}" || ! -x "${zestctl_bin}" || ! -x "${probe_client_bin}" ]]; then
  echo "ewmh-dock-gap: missing built binaries in ${build_dir}" >&2
  exit 2
fi
if [[ ! -f "${base_conf_path}" ]]; then
  echo "ewmh-dock-gap: missing config fixture ${base_conf_path}" >&2
  exit 2
fi

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "ewmh-dock-gap: missing required command '$1'" >&2
    exit 77
  fi
}

zestwm_require_nested_x11 "ewmh-dock-gap-probe"
need_cmd xdpyinfo
need_cmd xprop

cp "${base_conf_path}" "${probe_conf_path}"
# Strip static `workspace =` definitions so the registry grows only from runtime
# client placement (otherwise example `workspace = 5, ...` preallocates 5 desktops).
grep -v '^[[:space:]]*workspace[[:space:]]*=' "${probe_conf_path}" > "${probe_conf_path}.tmp"
mv -f "${probe_conf_path}.tmp" "${probe_conf_path}"
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
  echo "ewmh-dock-gap: unable to allocate free X11 display" >&2
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
  for f in "${ready_files[@]}"; do
    rm -f "${f}"
  done
}
trap cleanup EXIT

zestwm_start_nested_x11 "${display}"

for _ in {1..40}; do
  if ! kill -0 "${xvfb_pid}" 2>/dev/null; then
    echo "ewmh-dock-gap: nested X server exited during startup (skipping on this host)" >&2
    exit 77
  fi
  if DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
  echo "ewmh-dock-gap: nested X server did not become ready (skipping on this host)" >&2
  exit 77
fi

DISPLAY="${display}" "${zestwm_bin}" -c "${probe_conf_path}" >/tmp/zestwm-ewmh-dock-gap.log 2>&1 &
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

run_dispatch() {
  DISPLAY="${display}" "${zestctl_bin}" dispatch "$@" >/dev/null
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

if ! wait_for_wm_ready; then
  echo "ewmh-dock-gap: zestwm did not respond on ${display}" >&2
  exit 2
fi

run_dispatch workspace 1

for i in 0 1 2; do
  DISPLAY="${display}" "${probe_client_bin}" --title "ewmh-gap-${i}" --ready-file "${ready_files[$i]}" >/tmp/zestwm-ewmh-gap-client-"${i}".log 2>&1 &
  client_pids+=("$!")
done

wins_hex=()
for i in 0 1 2; do
  win_dec="$(read_ready_window "${ready_files[$i]}" || true)"
  if [[ -z "${win_dec}" ]]; then
    echo "ewmh-dock-gap: failed to capture window id for client ${i}" >&2
    exit 2
  fi
  wins_hex+=("$(printf '0x%08x' "${win_dec}")")
done

if ! move_window_to_workspace "2" "${wins_hex[1]}" || ! move_window_to_workspace "4" "${wins_hex[2]}"; then
  echo "ewmh-dock-gap: failed to prepare non-contiguous workspace occupancy" >&2
  exit 1
fi

# Spawn a dock client explicitly pinned to desktop index 4 (workspace id 5).
DISPLAY="${display}" "${probe_client_bin}" --title "ewmh-gap-dock" --dock --desktop-index 4 >/tmp/zestwm-ewmh-gap-dock.log 2>&1 &
client_pids+=("$!")
sleep 0.25

nd_line="$(DISPLAY="${display}" xprop -root _NET_NUMBER_OF_DESKTOPS 2>/dev/null || true)"
nd_value="$(printf '%s\n' "${nd_line}" | awk -F'= ' '/_NET_NUMBER_OF_DESKTOPS/ {print $2}' | tr -d '[:space:]')"
if [[ "${nd_value}" != "4" ]]; then
  echo "ewmh-dock-gap: expected _NET_NUMBER_OF_DESKTOPS=4, got '${nd_value}'" >&2
  echo "ewmh-dock-gap: raw xprop: ${nd_line}" >&2
  exit 1
fi

echo "ewmh-dock-gap: pass"
