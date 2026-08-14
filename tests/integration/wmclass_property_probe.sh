#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-}"
if [[ -z "${build_dir}" ]]; then
  echo "wmclass-property-probe: missing build directory argument" >&2
  exit 2
fi

probe_bin="${build_dir}/wmclass-property-test"
if [[ ! -x "${probe_bin}" ]]; then
  echo "wmclass-property-probe: missing binary ${probe_bin}" >&2
  exit 2
fi
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=tests/integration/xserver_common.inc.sh
source "${repo_root}/tests/integration/xserver_common.inc.sh"


need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "wmclass-property-probe: missing required command '$1'" >&2
    exit 77
  fi
}

zestwm_require_nested_x11 "wmclass-property-probe"
need_cmd xdpyinfo

display_num=90
while [[ ${display_num} -lt 110 ]]; do
  if [[ ! -S "/tmp/.X11-unix/X${display_num}" ]]; then
    break
  fi
  display_num=$((display_num + 1))
done
if [[ ${display_num} -ge 110 ]]; then
  echo "wmclass-property-probe: unable to allocate free X11 display" >&2
  exit 2
fi

display=":${display_num}"
xvfb_pid=""

cleanup() {
  if [[ -n "${xvfb_pid}" ]] && kill -0 "${xvfb_pid}" 2>/dev/null; then
    kill "${xvfb_pid}" 2>/dev/null || true
    wait "${xvfb_pid}" 2>/dev/null || true
  fi
}
trap cleanup EXIT

zestwm_start_nested_x11 "${display}"

for _ in {1..40}; do
  if ! kill -0 "${xvfb_pid}" 2>/dev/null; then
    echo "wmclass-property-probe: nested X server exited during startup (skipping on this host)" >&2
    exit 77
  fi
  if DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
  echo "wmclass-property-probe: nested X server did not become ready (skipping on this host)" >&2
  exit 77
fi

DISPLAY="${display}" "${probe_bin}"
