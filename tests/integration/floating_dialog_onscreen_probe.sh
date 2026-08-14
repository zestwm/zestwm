#!/usr/bin/env bash
# Probe: floating dialogs map on-screen (not at the legacy x+2*sw pre-map place).
set -euo pipefail

build_dir="${1:-}"
if [[ -z "${build_dir}" ]]; then
  echo "floating-dialog-onscreen: missing build directory argument" >&2
  exit 2
fi

zestwm_bin="${build_dir}/zestwm"
zestctl_bin="${build_dir}/zestctl"
probe_client_bin="${build_dir}/focusurgent-urgent-client-test"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=tests/integration/xserver_common.inc.sh
source "${repo_root}/tests/integration/xserver_common.inc.sh"

base_conf_path="${repo_root}/examples/zestwm.conf"
probe_conf_path="${repo_root}/tests/integration/.floating-dialog-onscreen.conf"
ready_normal="${repo_root}/tests/integration/.floating-dialog-onscreen.normal.ready"
ready_parent="${repo_root}/tests/integration/.floating-dialog-onscreen.parent.ready"
ready_special="${repo_root}/tests/integration/.floating-dialog-onscreen.special.ready"
special_tag="floordlg"
parent_class="FloatDlgParent"
dialog_class="FloatDlgWindow"

if [[ ! -x "${zestwm_bin}" || ! -x "${zestctl_bin}" || ! -x "${probe_client_bin}" ]]; then
  echo "floating-dialog-onscreen: missing built binaries in ${build_dir}" >&2
  exit 2
fi
if [[ ! -f "${base_conf_path}" ]]; then
  echo "floating-dialog-onscreen: missing config fixture ${base_conf_path}" >&2
  exit 2
fi

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "floating-dialog-onscreen: missing required command '$1'" >&2
    exit 77
  fi
}

zestwm_require_nested_x11 "floating-dialog-onscreen-probe"
need_cmd xdpyinfo
need_cmd xwininfo
need_cmd xprop

unset DBUS_SESSION_BUS_ADDRESS
unset DBUS_SYSTEM_BUS_ADDRESS

cp "${base_conf_path}" "${probe_conf_path}"
cat >> "${probe_conf_path}" <<EOF
workspace = special:${special_tag}
window-rule {
  match app-id="${parent_class}"
  open-on-workspace = special:${special_tag}
}
EOF
rm -f "${ready_normal}" "${ready_parent}" "${ready_special}"

display_num=110
while [[ ${display_num} -lt 250 ]]; do
  if [[ ! -S "/tmp/.X11-unix/X${display_num}" ]]; then
    break
  fi
  display_num=$((display_num + 1))
done
if [[ ${display_num} -ge 250 ]]; then
  echo "floating-dialog-onscreen: unable to allocate free X11 display" >&2
  rm -f "${probe_conf_path}"
  exit 2
fi

display=":${display_num}"
host_display="${ZESTWM_HOST_DISPLAY:-${DISPLAY:-}}"
if [[ -n "${host_display}" && "${display}" == "${host_display}" ]]; then
  echo "floating-dialog-onscreen: refusing host display '${display}'" >&2
  exit 2
fi

xvfb_pid=""
wm_pid=""
normal_pid=""
parent_pid=""
special_pid=""

cleanup() {
  for pid_var in special_pid parent_pid normal_pid wm_pid xvfb_pid; do
    pid="${!pid_var:-}"
    if [[ -n "${pid}" ]] && kill -0 "${pid}" 2>/dev/null; then
      kill "${pid}" 2>/dev/null || true
      wait "${pid}" 2>/dev/null || true
    fi
  done
  rm -f "${probe_conf_path}" "${ready_normal}" "${ready_parent}" "${ready_special}"
}
trap cleanup EXIT

zestwm_start_nested_x11 "${display}"

for _ in {1..40}; do
  if ! kill -0 "${xvfb_pid}" 2>/dev/null; then
    echo "floating-dialog-onscreen: nested X server exited during startup (skipping on this host)" >&2
    exit 77
  fi
  if DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
  echo "floating-dialog-onscreen: nested X server did not become ready (skipping on this host)" >&2
  exit 77
fi

screen_w="$(DISPLAY="${display}" xdpyinfo | awk '/dimensions:/ { split($2, a, "x"); print a[1]; exit }')"
if [[ -z "${screen_w}" || "${screen_w}" -lt 1 ]]; then
  echo "floating-dialog-onscreen: failed to read screen width" >&2
  exit 2
fi

DISPLAY="${display}" "${zestwm_bin}" -c "${probe_conf_path}" >/tmp/zestwm-floating-dialog-onscreen.log 2>&1 &
wm_pid=$!

for _ in {1..120}; do
  if DISPLAY="${display}" "${zestctl_bin}" version >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${display}" "${zestctl_bin}" version >/dev/null 2>&1; then
  echo "floating-dialog-onscreen: zestwm did not respond on ${display}" >&2
  exit 2
fi

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

# Assert mapped floating dialog is on-screen (not parked at legacy x+2*sw) and fully opaque.
assert_dialog_onscreen() {
  local win_hex="$1"
  local label="$2"
  local info abs_x abs_y width height opacity
  info="$(DISPLAY="${display}" xwininfo -id "${win_hex}" 2>/dev/null || true)"
  abs_x="$(printf '%s\n' "${info}" | awk '/Absolute upper-left X:/ {print $4; exit}')"
  abs_y="$(printf '%s\n' "${info}" | awk '/Absolute upper-left Y:/ {print $4; exit}')"
  width="$(printf '%s\n' "${info}" | awk '/Width:/ {print $2; exit}')"
  height="$(printf '%s\n' "${info}" | awk '/Height:/ {print $2; exit}')"
  if [[ -z "${abs_x}" || -z "${abs_y}" || -z "${width}" || -z "${height}" ]]; then
    echo "floating-dialog-onscreen: ${label}: failed to read xwininfo geometry for ${win_hex}" >&2
    printf '%s\n' "${info}" >&2 || true
    exit 1
  fi
  if [[ "${abs_x}" -lt 0 || "${abs_y}" -lt 0 ]]; then
    echo "floating-dialog-onscreen: ${label}: dialog off-screen (x=${abs_x} y=${abs_y})" >&2
    exit 1
  fi
  if [[ "${abs_x}" -ge "${screen_w}" ]]; then
    echo "floating-dialog-onscreen: ${label}: dialog x=${abs_x} >= screen_w=${screen_w} (legacy off-screen pre-map?)" >&2
    exit 1
  fi
  local offscreen_floor=$((screen_w + screen_w / 2))
  if [[ "${abs_x}" -ge "${offscreen_floor}" ]]; then
    echo "floating-dialog-onscreen: ${label}: dialog x=${abs_x} looks like x+2*sw placement" >&2
    exit 1
  fi
  opacity="$(DISPLAY="${display}" xprop -id "${win_hex}" _NET_WM_WINDOW_OPACITY 2>/dev/null || true)"
  if printf '%s\n' "${opacity}" | rg -q '_NET_WM_WINDOW_OPACITY\(CARDINAL\)'; then
    echo "floating-dialog-onscreen: ${label}: expected no _NET_WM_WINDOW_OPACITY (fully opaque), got: ${opacity}" >&2
    exit 1
  fi
}

# --- Normal workspace dialog ---
DISPLAY="${display}" "${probe_client_bin}" \
  --title float-dlg-normal \
  --wm-class "${dialog_class}" \
  --dialog \
  --width 200 \
  --height 150 \
  --ready-file "${ready_normal}" >/tmp/zestwm-floating-dialog-onscreen-normal.log 2>&1 &
normal_pid=$!

normal_dec="$(read_ready_window "${ready_normal}" || true)"
if [[ -z "${normal_dec}" ]]; then
  echo "floating-dialog-onscreen: failed to read normal dialog window id" >&2
  exit 2
fi
normal_hex="$(printf '0x%08x' "${normal_dec}")"
if ! wait_for_window_present "${normal_hex}"; then
  echo "floating-dialog-onscreen: normal dialog not listed by zestctl clients" >&2
  exit 1
fi
# Allow arrange/center to settle.
sleep 0.15
assert_dialog_onscreen "${normal_hex}" "normal"

# --- Special overlay dialog ---
DISPLAY="${display}" "${probe_client_bin}" \
  --title float-dlg-parent \
  --wm-class "${parent_class}" \
  --ready-file "${ready_parent}" >/tmp/zestwm-floating-dialog-onscreen-parent.log 2>&1 &
parent_pid=$!

parent_dec="$(read_ready_window "${ready_parent}" || true)"
if [[ -z "${parent_dec}" ]]; then
  echo "floating-dialog-onscreen: failed to read special parent window id" >&2
  exit 2
fi
parent_hex="$(printf '0x%08x' "${parent_dec}")"
if ! wait_for_window_present "${parent_hex}"; then
  echo "floating-dialog-onscreen: special parent not listed by zestctl clients" >&2
  exit 1
fi

DISPLAY="${display}" "${zestctl_bin}" dispatch workspace "special:${special_tag}" >/dev/null 2>&1 || true
sleep 0.2

DISPLAY="${display}" "${probe_client_bin}" \
  --title float-dlg-special \
  --wm-class "${dialog_class}Special" \
  --dialog \
  --width 200 \
  --height 150 \
  --ready-file "${ready_special}" >/tmp/zestwm-floating-dialog-onscreen-special.log 2>&1 &
special_pid=$!

special_dec="$(read_ready_window "${ready_special}" || true)"
if [[ -z "${special_dec}" ]]; then
  echo "floating-dialog-onscreen: failed to read special dialog window id" >&2
  exit 2
fi
special_hex="$(printf '0x%08x' "${special_dec}")"
if ! wait_for_window_present "${special_hex}"; then
  echo "floating-dialog-onscreen: special dialog not listed by zestctl clients" >&2
  exit 1
fi
sleep 0.15
assert_dialog_onscreen "${special_hex}" "special"

echo "floating-dialog-onscreen: pass"
