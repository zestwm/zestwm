#!/usr/bin/env bash
# Verifies `w[t1]` workspace selector applies gaps when exactly one tiled client is present.
set -euo pipefail

build_dir="${1:-}"
if [[ -z "${build_dir}" ]]; then
  echo "workspace-selector-wt1: missing build directory argument" >&2
  exit 2
fi

zestwm_bin="${build_dir}/zestwm"
zestctl_bin="${build_dir}/zestctl"
probe_client_bin="${build_dir}/focusurgent-urgent-client-test"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=tests/integration/xserver_common.inc.sh
source "${repo_root}/tests/integration/xserver_common.inc.sh"

base_conf_path="${repo_root}/examples/zestwm.conf"
probe_conf_path="${repo_root}/tests/integration/.workspace-selector-wt1.conf"
ready_file="${repo_root}/tests/integration/.workspace-selector-wt1.ready"

if [[ ! -x "${zestwm_bin}" || ! -x "${zestctl_bin}" || ! -x "${probe_client_bin}" ]]; then
  echo "workspace-selector-wt1: missing built binaries in ${build_dir}" >&2
  exit 2
fi
if [[ ! -f "${base_conf_path}" ]]; then
  echo "workspace-selector-wt1: missing config fixture ${base_conf_path}" >&2
  exit 2
fi

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "workspace-selector-wt1: missing required command '$1'" >&2
    exit 77
  fi
}

zestwm_require_nested_x11 "workspace-selector-wt1-probe"
need_cmd xdpyinfo
need_cmd xwininfo

cp "${base_conf_path}" "${probe_conf_path}"
grep -v '^[[:space:]]*workspace[[:space:]]*=' "${probe_conf_path}" > "${probe_conf_path}.tmp"
mv -f "${probe_conf_path}.tmp" "${probe_conf_path}"
cat >> "${probe_conf_path}" <<'EOF'
workspace = 1
workspace = w[t1], gapsout:40
EOF
rm -f "${ready_file}"

display_num=233
while [[ ${display_num} -lt 252 ]]; do
  if [[ ! -S "/tmp/.X11-unix/X${display_num}" ]]; then
    break
  fi
  display_num=$((display_num + 1))
done
if [[ ${display_num} -ge 252 ]]; then
  echo "workspace-selector-wt1: unable to allocate free X11 display" >&2
  rm -f "${probe_conf_path}"
  exit 2
fi

display=":${display_num}"
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
  rm -f "${probe_conf_path}" "${ready_file}"
}
trap cleanup EXIT

zestwm_start_nested_x11 "${display}"

for _ in {1..40}; do
  if ! kill -0 "${xvfb_pid}" 2>/dev/null; then
    echo "workspace-selector-wt1: nested X server exited during startup (skipping on this host)" >&2
    exit 77
  fi
  if DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
  echo "workspace-selector-wt1: nested X server did not become ready (skipping on this host)" >&2
  exit 77
fi

DISPLAY="${display}" "${zestwm_bin}" -c "${probe_conf_path}" >/tmp/zestwm-workspace-selector-wt1.log 2>&1 &
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

read_window_geometry() {
  local win_hex="$1"
  local info
  info="$(DISPLAY="${display}" xwininfo -id "${win_hex}" 2>/dev/null || true)"
  if [[ -z "${info}" ]]; then
    return 1
  fi
  GEO_X="$(printf '%s\n' "${info}" | awk '/Absolute upper-left X:/ {print $4; exit}')"
  GEO_W="$(printf '%s\n' "${info}" | awk '/Width:/ {print $2; exit}')"
  [[ -n "${GEO_X}" && -n "${GEO_W}" ]]
}

wait_for_geometry_predicate() {
  local win_hex="$1"
  local predicate="$2"
  for _ in {1..120}; do
    if read_window_geometry "${win_hex}" && eval "${predicate}"; then
      return 0
    fi
    sleep 0.05
  done
  return 1
}

if ! wait_for_wm_ready; then
  echo "workspace-selector-wt1: zestwm did not respond on ${display}" >&2
  exit 2
fi

run_dispatch workspace 1

DISPLAY="${display}" "${probe_client_bin}" --title "workspace-selector-wt1" --ready-file "${ready_file}" >/tmp/zestwm-workspace-selector-wt1-client.log 2>&1 &
client_pid=$!

win_dec="$(read_ready_window "${ready_file}" || true)"
if [[ -z "${win_dec}" ]]; then
  echo "workspace-selector-wt1: failed to capture probe window id" >&2
  exit 2
fi
win_hex="$(printf '0x%08x' "${win_dec}")"

if ! wait_for_geometry_predicate "${win_hex}" '(( GEO_X >= 35 && GEO_W <= 1210 ))'; then
  echo "workspace-selector-wt1: w[t1] gapsout:40 not applied (x=${GEO_X:-?}, w=${GEO_W:-?})" >&2
  DISPLAY="${display}" xwininfo -id "${win_hex}" >&2 || true
  exit 1
fi

echo "workspace-selector-wt1: pass"
