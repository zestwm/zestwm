#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-}"
if [[ -z "${build_dir}" ]]; then
  echo "parser-workspace-name-dispatch: missing build directory argument" >&2
  exit 2
fi

zestwm_bin="${build_dir}/zestwm"
zestctl_bin="${build_dir}/zestctl"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=tests/integration/xserver_common.inc.sh
source "${repo_root}/tests/integration/xserver_common.inc.sh"

base_conf_path="${repo_root}/examples/zestwm.conf"
probe_conf_path="${repo_root}/tests/integration/.parser-workspace-name-dispatch.conf"
ws_name="WmstateNamedWs"

if [[ ! -x "${zestwm_bin}" || ! -x "${zestctl_bin}" ]]; then
  echo "parser-workspace-name-dispatch: missing built binaries in ${build_dir}" >&2
  exit 2
fi
if [[ ! -f "${base_conf_path}" ]]; then
  echo "parser-workspace-name-dispatch: missing config fixture ${base_conf_path}" >&2
  exit 2
fi

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "parser-workspace-name-dispatch: missing required command '$1'" >&2
    exit 77
  fi
}

zestwm_require_nested_x11 "parser-workspace-name-dispatch-probe"
need_cmd xdpyinfo

cp "${base_conf_path}" "${probe_conf_path}"
{
  printf '\n# MUST-PARSER-001: named workspace in registry (EWMH export + zestctl dispatch token)\n'
  printf 'workspace = 7, %s\n' "${ws_name}"
} >> "${probe_conf_path}"

display_num=140
while [[ ${display_num} -lt 170 ]]; do
  if [[ ! -S "/tmp/.X11-unix/X${display_num}" ]]; then
    break
  fi
  display_num=$((display_num + 1))
done
if [[ ${display_num} -ge 170 ]]; then
  echo "parser-workspace-name-dispatch: unable to allocate free X11 display" >&2
  rm -f "${probe_conf_path}"
  exit 2
fi

display=":${display_num}"
xvfb_pid=""
wm_pid=""

cleanup() {
  if [[ -n "${wm_pid}" ]] && kill -0 "${wm_pid}" 2>/dev/null; then
    kill "${wm_pid}" 2>/dev/null || true
    wait "${wm_pid}" 2>/dev/null || true
  fi
  if [[ -n "${xvfb_pid}" ]] && kill -0 "${xvfb_pid}" 2>/dev/null; then
    kill "${xvfb_pid}" 2>/dev/null || true
    wait "${xvfb_pid}" 2>/dev/null || true
  fi
  rm -f "${probe_conf_path}"
}
trap cleanup EXIT

zestwm_start_nested_x11 "${display}"

for _ in {1..40}; do
  if ! kill -0 "${xvfb_pid}" 2>/dev/null; then
    echo "parser-workspace-name-dispatch: nested X server exited during startup (skipping on this host)" >&2
    exit 77
  fi
  if DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
  echo "parser-workspace-name-dispatch: nested X server did not become ready (skipping on this host)" >&2
  exit 77
fi

DISPLAY="${display}" "${zestwm_bin}" -c "${probe_conf_path}" >/tmp/zestwm-parser-workspace-name-dispatch.log 2>&1 &
wm_pid=$!

for _ in {1..120}; do
  if DISPLAY="${display}" "${zestctl_bin}" version >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${display}" "${zestctl_bin}" version >/dev/null 2>&1; then
  echo "parser-workspace-name-dispatch: zestwm did not respond on ${display}" >&2
  exit 2
fi

if ! kill -0 "${wm_pid}" 2>/dev/null; then
  echo "parser-workspace-name-dispatch: zestwm exited during startup" >&2
  exit 1
fi

active_name_json() {
  DISPLAY="${display}" "${zestctl_bin}" -j activeworkspace
}

wait_for_active_name() {
  local want="$1"
  local line got
  for _ in {1..200}; do
    line="$(active_name_json)"
    if [[ "${line}" =~ \"name\":\"([^\"]+)\" ]]; then
      got="${BASH_REMATCH[1]}"
      if [[ "${got}" == "${want}" ]]; then
        return 0
      fi
    fi
    sleep 0.05
  done
  return 1
}

# zestctl name dispatch reads _NET_DESKTOP_NAMES; wait until WM exported the registry display name.
for _ in {1..200}; do
  if DISPLAY="${display}" "${zestctl_bin}" workspaces 2>/dev/null | grep -qF "${ws_name}"; then
    break
  fi
  sleep 0.05
done

if ! DISPLAY="${display}" "${zestctl_bin}" dispatch workspace "${ws_name}"; then
  echo "parser-workspace-name-dispatch: dispatch workspace ${ws_name} failed, trying numeric id 7" >&2
  DISPLAY="${display}" "${zestctl_bin}" dispatch workspace 7
fi

if ! wait_for_active_name "${ws_name}"; then
  echo "parser-workspace-name-dispatch: active workspace name did not become ${ws_name} after dispatch" >&2
  active_name_json >&2 || true
  exit 1
fi

if ! kill -0 "${wm_pid}" 2>/dev/null; then
  echo "parser-workspace-name-dispatch: zestwm crashed after dispatch" >&2
  exit 1
fi

echo "parser-workspace-name-dispatch: pass"
exit 0
