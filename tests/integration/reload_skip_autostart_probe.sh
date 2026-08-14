#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-}"
if [[ -z "${build_dir}" ]]; then
  echo "reload-skip-autostart: missing build directory argument" >&2
  exit 2
fi

zestwm_bin="${build_dir}/zestwm"
zestctl_bin="${build_dir}/zestctl"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=tests/integration/xserver_common.inc.sh
source "${repo_root}/tests/integration/xserver_common.inc.sh"

base_conf_path="${repo_root}/examples/zestwm.conf"
probe_conf_path="${repo_root}/tests/integration/.reload-skip-autostart.conf"
marker_file="${repo_root}/tests/integration/.reload-skip-autostart.marker"

if [[ ! -x "${zestwm_bin}" || ! -x "${zestctl_bin}" ]]; then
  echo "reload-skip-autostart: missing built binaries in ${build_dir}" >&2
  exit 2
fi
if [[ ! -f "${base_conf_path}" ]]; then
  echo "reload-skip-autostart: missing config fixture ${base_conf_path}" >&2
  exit 2
fi

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "reload-skip-autostart: missing required command '$1'" >&2
    exit 77
  fi
}

zestwm_require_nested_x11 "reload-skip-autostart-probe"
need_cmd xdpyinfo

cp "${base_conf_path}" "${probe_conf_path}"
rm -f "${marker_file}"
cat >> "${probe_conf_path}" <<EOF
exec-once = echo autostart >> ${marker_file}
EOF

display_num=170
while [[ ${display_num} -lt 190 ]]; do
  if [[ ! -S "/tmp/.X11-unix/X${display_num}" ]]; then
    break
  fi
  display_num=$((display_num + 1))
done
if [[ ${display_num} -ge 190 ]]; then
  echo "reload-skip-autostart: unable to allocate free X11 display" >&2
  rm -f "${probe_conf_path}" "${marker_file}"
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
  rm -f "${probe_conf_path}" "${marker_file}"
}
trap cleanup EXIT

zestwm_start_nested_x11 "${display}"

for _ in {1..40}; do
  if ! kill -0 "${xvfb_pid}" 2>/dev/null; then
    echo "reload-skip-autostart: nested X server exited during startup (skipping on this host)" >&2
    exit 77
  fi
  if DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
  echo "reload-skip-autostart: nested X server did not become ready (skipping on this host)" >&2
  exit 77
fi

DISPLAY="${display}" "${zestwm_bin}" -c "${probe_conf_path}" >/tmp/zestwm-reload-skip-autostart.log 2>&1 &
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
    echo "reload-skip-autostart: zestwm crashed unexpectedly" >&2
    exit 1
  fi
}

if ! wait_for_wm_ready; then
  echo "reload-skip-autostart: zestwm did not respond on ${display}" >&2
  exit 2
fi

for _ in {1..60}; do
  if [[ -f "${marker_file}" ]]; then
    break
  fi
  sleep 0.05
done
if [[ ! -f "${marker_file}" ]]; then
  echo "reload-skip-autostart: exec-once marker missing after startup" >&2
  exit 1
fi
startup_count="$(wc -l < "${marker_file}" | tr -d '[:space:]')"
if [[ "${startup_count}" != "1" ]]; then
  echo "reload-skip-autostart: expected one exec-once run at startup, got ${startup_count}" >&2
  exit 1
fi

DISPLAY="${display}" "${zestctl_bin}" dispatch reload >/dev/null
assert_wm_alive

for _ in {1..120}; do
  if DISPLAY="${display}" "${zestctl_bin}" version >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${display}" "${zestctl_bin}" version >/dev/null 2>&1; then
  echo "reload-skip-autostart: zestwm did not respond after reload" >&2
  exit 1
fi

sleep 0.5
reload_count="$(wc -l < "${marker_file}" | tr -d '[:space:]')"
if [[ "${reload_count}" != "1" ]]; then
  echo "reload-skip-autostart: exec-once reran on reload (count=${reload_count})" >&2
  exit 1
fi

echo "reload-skip-autostart: pass"
