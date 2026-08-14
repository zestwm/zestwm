#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-}"
if [[ -z "${build_dir}" ]]; then
  echo "windowrule-special-workspace: missing build directory argument" >&2
  exit 2
fi

zestwm_bin="${build_dir}/zestwm"
zestctl_bin="${build_dir}/zestctl"
probe_client_bin="${build_dir}/focusurgent-urgent-client-test"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=tests/integration/xserver_common.inc.sh
source "${repo_root}/tests/integration/xserver_common.inc.sh"

base_conf_path="${repo_root}/examples/zestwm.conf"
probe_conf_path="${repo_root}/tests/integration/.windowrule-special-workspace.conf"
ready_file="${repo_root}/tests/integration/.windowrule-special-workspace.ready"
special_tag="wrspprobe"

if [[ ! -x "${zestwm_bin}" || ! -x "${zestctl_bin}" || ! -x "${probe_client_bin}" ]]; then
  echo "windowrule-special-workspace: missing built binaries in ${build_dir}" >&2
  exit 2
fi
if [[ ! -f "${base_conf_path}" ]]; then
  echo "windowrule-special-workspace: missing config fixture ${base_conf_path}" >&2
  exit 2
fi

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "windowrule-special-workspace: missing required command '$1'" >&2
    exit 77
  fi
}

zestwm_require_nested_x11 "windowrule-special-workspace-probe"
need_cmd xdpyinfo

cp "${base_conf_path}" "${probe_conf_path}"
cat >> "${probe_conf_path}" <<EOF
window-rule {
  match app-id="FocusurgentProbe" title="wrsp-special-probe"
  open-on-workspace = special:${special_tag}
}
EOF
rm -f "${ready_file}"

display_num=90
while [[ ${display_num} -lt 250 ]]; do
  if [[ ! -S "/tmp/.X11-unix/X${display_num}" ]]; then
    break
  fi
  display_num=$((display_num + 1))
done
if [[ ${display_num} -ge 250 ]]; then
  echo "windowrule-special-workspace: unable to allocate free X11 display" >&2
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
    echo "windowrule-special-workspace: nested X server exited during startup (skipping on this host)" >&2
    exit 77
  fi
  if DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
  echo "windowrule-special-workspace: nested X server did not become ready (skipping on this host)" >&2
  exit 77
fi

DISPLAY="${display}" "${zestwm_bin}" -c "${probe_conf_path}" >/tmp/zestwm-windowrule-special-workspace.log 2>&1 &
wm_pid=$!

for _ in {1..120}; do
  if DISPLAY="${display}" "${zestctl_bin}" version >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${display}" "${zestctl_bin}" version >/dev/null 2>&1; then
  echo "windowrule-special-workspace: zestwm did not respond on ${display}" >&2
  exit 2
fi

DISPLAY="${display}" "${probe_client_bin}" --title wrsp-special-probe --ready-file "${ready_file}" >/tmp/zestwm-windowrule-special-workspace-client.log 2>&1 &
client_pid=$!

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

win_dec="$(read_ready_window "${ready_file}" || true)"
if [[ -z "${win_dec}" ]]; then
  echo "windowrule-special-workspace: failed to read test client window id" >&2
  exit 2
fi
win_hex="$(printf '0x%08x' "${win_dec}")"

if ! wait_for_window_present "${win_hex}"; then
  echo "windowrule-special-workspace: client not listed by zestctl clients" >&2
  DISPLAY="${display}" "${zestctl_bin}" clients >&2 || true
  exit 1
fi

if ! DISPLAY="${display}" "${zestctl_bin}" dispatch special "${special_tag}"; then
  echo "windowrule-special-workspace: dispatch special (toggle 1) failed" >&2
  exit 1
fi

sleep 0.2
if ! DISPLAY="${display}" "${zestctl_bin}" dispatch special "${special_tag}"; then
  echo "windowrule-special-workspace: dispatch special (toggle 2) failed" >&2
  exit 1
fi

sleep 0.2
if ! DISPLAY="${display}" "${zestctl_bin}" clients | grep -F "win:${win_hex}" | grep -q 'focused:yes'; then
  echo "windowrule-special-workspace: expected probe window focused after reopening special overlay" >&2
  DISPLAY="${display}" "${zestctl_bin}" clients >&2 || true
  exit 1
fi

echo "windowrule-special-workspace: pass"
