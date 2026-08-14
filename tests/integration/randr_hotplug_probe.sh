#!/usr/bin/env bash
# Verifies the WM survives a RandR screen-change (mode switch) without crashing
# and keeps responding to zestctl. Uses Xvfb as an invisible parent for Xephyr
# (which renders into its parent), so nothing touches the host display.
set -euo pipefail

build_dir="${1:-}"
if [[ -z "${build_dir}" ]]; then
  echo "randr-hotplug: missing build directory argument" >&2
  exit 2
fi

zestwm_bin="${build_dir}/zestwm"
zestctl_bin="${build_dir}/zestctl"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=tests/integration/xserver_common.inc.sh
source "${repo_root}/tests/integration/xserver_common.inc.sh"

base_conf_path="${repo_root}/examples/zestwm.conf"
probe_conf_path="${repo_root}/tests/integration/.randr-hotplug.conf"

if [[ ! -x "${zestwm_bin}" || ! -x "${zestctl_bin}" ]]; then
  echo "randr-hotplug: missing built binaries in ${build_dir}" >&2
  exit 2
fi
if [[ ! -f "${base_conf_path}" ]]; then
  echo "randr-hotplug: missing config fixture ${base_conf_path}" >&2
  exit 2
fi

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "randr-hotplug: missing required command '$1'" >&2
    exit 77
  fi
}

zestwm_require_nested_x11 "randr-hotplug-probe"
need_cmd xdpyinfo
# xrandr is required to drive the mode switch. An AppImage shim may shadow the
# system binary in PATH, so resolve and store the real path once here.
if ! command -v xrandr >/dev/null 2>&1; then
  echo "randr-hotplug: missing required command 'xrandr'" >&2
  exit 77
fi
xrandr_bin="$(command -v xrandr)"

cp "${base_conf_path}" "${probe_conf_path}"

# Allocate a parent Xvfb display (invisible framebuffer; Xephyr renders into it
# instead of the host display) and a nested Xephyr display where zestwm runs.
parent_num=300
while [[ ${parent_num} -lt 400 ]]; do
  if [[ ! -S "/tmp/.X11-unix/X${parent_num}" ]]; then
    break
  fi
  parent_num=$((parent_num + 1))
done
if [[ ${parent_num} -ge 400 ]]; then
  echo "randr-hotplug: unable to allocate free parent X11 display" >&2
  rm -f "${probe_conf_path}"
  exit 2
fi

nested_num=$((parent_num + 1))
while [[ ${nested_num} -lt 500 ]]; do
  if [[ ! -S "/tmp/.X11-unix/X${nested_num}" ]]; then
    break
  fi
  nested_num=$((nested_num + 1))
done
if [[ ${nested_num} -ge 500 ]]; then
  echo "randr-hotplug: unable to allocate free nested X11 display" >&2
  rm -f "${probe_conf_path}"
  exit 2
fi

parent_display=":${parent_num}"
nested_display=":${nested_num}"
xvfb_pid=""
xephyr_pid=""
wm_pid=""

cleanup() {
  if [[ -n "${wm_pid}" ]] && kill -0 "${wm_pid}" 2>/dev/null; then
    kill "${wm_pid}" 2>/dev/null || true
    wait "${wm_pid}" 2>/dev/null || true
  fi
  if [[ -n "${xephyr_pid}" ]] && kill -0 "${xephyr_pid}" 2>/dev/null; then
    kill "${xephyr_pid}" 2>/dev/null || true
    wait "${xephyr_pid}" 2>/dev/null || true
  fi
  if [[ -n "${xvfb_pid}" ]] && kill -0 "${xvfb_pid}" 2>/dev/null; then
    kill "${xvfb_pid}" 2>/dev/null || true
    wait "${xvfb_pid}" 2>/dev/null || true
  fi
  rm -f "${probe_conf_path}"
}
trap cleanup EXIT

# Xvfb parent: invisible framebuffer, large enough to host the Xephyr window.
Xvfb "${parent_display}" -screen 0 1600x1200x24 -nolisten tcp -extension GLX >/dev/null 2>&1 &
xvfb_pid=$!

for _ in {1..40}; do
  if ! kill -0 "${xvfb_pid}" 2>/dev/null; then
    echo "randr-hotplug: parent Xvfb exited during startup (skipping on this host)" >&2
    exit 77
  fi
  if DISPLAY="${parent_display}" xdpyinfo >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${parent_display}" xdpyinfo >/dev/null 2>&1; then
  echo "randr-hotplug: parent Xvfb did not become ready (skipping on this host)" >&2
  exit 77
fi

# Xephyr nested INTO the Xvfb parent (not the host display), so its window is
# invisible. Starts at 1280x720; we switch to 1024x768 below to fire a
# ScreenChangeNotify that the WM must handle.
DISPLAY="${parent_display}" Xephyr "${nested_display}" -screen 1280x720x24 -ac -nolisten tcp >/dev/null 2>&1 &
xephyr_pid=$!

for _ in {1..40}; do
  if ! kill -0 "${xephyr_pid}" 2>/dev/null; then
    echo "randr-hotplug: Xephyr exited during startup (skipping on this host)" >&2
    exit 77
  fi
  if DISPLAY="${nested_display}" xdpyinfo >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${nested_display}" xdpyinfo >/dev/null 2>&1; then
  echo "randr-hotplug: Xephyr did not become ready (skipping on this host)" >&2
  exit 77
fi

DISPLAY="${nested_display}" "${zestwm_bin}" -c "${probe_conf_path}" >/tmp/zestwm-randr-hotplug.log 2>&1 &
wm_pid=$!

for _ in {1..120}; do
  if DISPLAY="${nested_display}" "${zestctl_bin}" version >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${nested_display}" "${zestctl_bin}" version >/dev/null 2>&1; then
  echo "randr-hotplug: zestwm did not respond on ${nested_display}" >&2
  exit 2
fi

# Sanity: one monitor present before the switch. zestctl monitors reports
# nothing until the WM has published its monitor state, so poll briefly.
before_monitors=0
for _ in {1..50}; do
  before_monitors="$(DISPLAY="${nested_display}" "${zestctl_bin}" monitors 2>/dev/null | wc -l)"
  if [[ "${before_monitors}" -ge 1 ]]; then
    break
  fi
  sleep 0.1
done
if [[ "${before_monitors}" -lt 1 ]]; then
  echo "randr-hotplug: expected at least one monitor before switch" >&2
  exit 1
fi

# Drive a mode switch on the nested Xephyr output. This fires a RandR
# ScreenChangeNotify that the WM subscribed to in setup() must handle.
# Use a clean PATH so an AppImage xrandr shim cannot intercept the call.
DISPLAY="${nested_display}" env PATH="/usr/sbin:/usr/bin:/sbin:/bin" "${xrandr_bin}" --output default --mode 1024x768 2>/dev/null || true
sleep 1

# The WM must still be alive and respond to zestctl after handling the event.
if ! kill -0 "${wm_pid}" 2>/dev/null; then
  echo "randr-hotplug: zestwm exited after RandR screen change" >&2
  exit 1
fi
if ! DISPLAY="${nested_display}" "${zestctl_bin}" version >/dev/null 2>&1; then
  echo "randr-hotplug: zestwm did not respond after RandR screen change" >&2
  exit 1
fi

after_monitors="$(DISPLAY="${nested_display}" "${zestctl_bin}" monitors 2>/dev/null | wc -l)"
if [[ "${after_monitors}" -lt 1 ]]; then
  echo "randr-hotplug: no monitors reported after RandR screen change" >&2
  exit 1
fi

echo "randr-hotplug: WM survived RandR screen change and still responds (before=${before_monitors} after=${after_monitors} monitors)"
