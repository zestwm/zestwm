#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-}"
if [[ -z "${build_dir}" ]]; then
  echo "workspace-policy-keys: missing build directory argument" >&2
  exit 2
fi

zestwm_bin="${build_dir}/zestwm"
zestctl_bin="${build_dir}/zestctl"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=tests/integration/xserver_common.inc.sh
source "${repo_root}/tests/integration/xserver_common.inc.sh"

base_conf_path="${repo_root}/examples/zestwm.conf"
probe_conf_path="${repo_root}/tests/integration/.workspace-policy-keys.conf"
wm_log_path="/tmp/zestwm-workspace-policy-keys.log"

if [[ ! -x "${zestwm_bin}" || ! -x "${zestctl_bin}" ]]; then
  echo "workspace-policy-keys: missing built binaries in ${build_dir}" >&2
  exit 2
fi
if [[ ! -f "${base_conf_path}" ]]; then
  echo "workspace-policy-keys: missing config fixture ${base_conf_path}" >&2
  exit 2
fi

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "workspace-policy-keys: missing required command '$1'" >&2
    exit 77
  fi
}

zestwm_require_nested_x11 "workspace-policy-keys-probe"
need_cmd xdpyinfo

cp "${base_conf_path}" "${probe_conf_path}"
cat >> "${probe_conf_path}" <<'EOF'
workspace = 2, default:true, monitor:0
workspace = 3, persistent:false
workspace = 4, defaultName:media
EOF
rm -f "${wm_log_path}"

display_num=90
while [[ ${display_num} -lt 230 ]]; do
  if [[ ! -S "/tmp/.X11-unix/X${display_num}" ]]; then
    break
  fi
  display_num=$((display_num + 1))
done
if [[ ${display_num} -ge 230 ]]; then
  echo "workspace-policy-keys: unable to allocate free X11 display" >&2
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
    echo "workspace-policy-keys: nested X server exited during startup (skipping on this host)" >&2
    exit 77
  fi
  if DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
  echo "workspace-policy-keys: nested X server did not become ready (skipping on this host)" >&2
  exit 77
fi

DISPLAY="${display}" "${zestwm_bin}" -c "${probe_conf_path}" >"${wm_log_path}" 2>&1 &
wm_pid=$!

for _ in {1..120}; do
  if DISPLAY="${display}" "${zestctl_bin}" version >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${display}" "${zestctl_bin}" version >/dev/null 2>&1; then
  echo "workspace-policy-keys: zestwm did not respond on ${display}" >&2
  exit 2
fi

if rg -n "workspace rule: key 'default' not supported|workspace rule: key 'persistent' not supported|workspace rule: key 'defaultName' not supported" "${wm_log_path}" >/dev/null 2>&1; then
  echo "workspace-policy-keys: parser still reports default/persistent/defaultName as unsupported" >&2
  rg -n "workspace rule:" "${wm_log_path}" || true
  exit 1
fi

echo "workspace-policy-keys: pass"
