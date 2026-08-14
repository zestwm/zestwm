#!/usr/bin/env bash
# Probe: with open special overlay, dialogs inherit the tag and stay usable on the scratchpad.
set -euo pipefail

build_dir="${1:-}"
if [[ -z "${build_dir}" ]]; then
  echo "special-overlay-dialog-routing: missing build directory argument" >&2
  exit 2
fi

zestwm_bin="${build_dir}/zestwm"
zestctl_bin="${build_dir}/zestctl"
probe_client_bin="${build_dir}/focusurgent-urgent-client-test"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=tests/integration/xserver_common.inc.sh
source "${repo_root}/tests/integration/xserver_common.inc.sh"

base_conf_path="${repo_root}/examples/zestwm.conf"
probe_conf_path="${repo_root}/tests/integration/.special-overlay-dialog-routing.conf"
ready_parent="${repo_root}/tests/integration/.special-overlay-dialog-routing.parent.ready"
ready_dialog="${repo_root}/tests/integration/.special-overlay-dialog-routing.dialog.ready"
ready_normal_dialog="${repo_root}/tests/integration/.special-overlay-dialog-routing.normal-dialog.ready"
special_tag="dlgroute"
parent_class="DlgRouteParent"
dialog_class="DlgRouteDialog"
normal_dialog_class="DlgRouteNormalDialog"

if [[ ! -x "${zestwm_bin}" || ! -x "${zestctl_bin}" || ! -x "${probe_client_bin}" ]]; then
  echo "special-overlay-dialog-routing: missing built binaries in ${build_dir}" >&2
  exit 2
fi
if [[ ! -f "${base_conf_path}" ]]; then
  echo "special-overlay-dialog-routing: missing config fixture ${base_conf_path}" >&2
  exit 2
fi

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "special-overlay-dialog-routing: missing required command '$1'" >&2
    exit 77
  fi
}

zestwm_require_nested_x11 "special-overlay-dialog-routing-probe"
need_cmd xdpyinfo

unset DBUS_SESSION_BUS_ADDRESS
unset DBUS_SYSTEM_BUS_ADDRESS

cp "${base_conf_path}" "${probe_conf_path}"
cat >> "${probe_conf_path}" <<EOF
workspace = special:${special_tag}
window-rule {
  match app-id="${parent_class}"
  open-on-workspace = special:${special_tag}
}
window-rule {
  match app-id="${normal_dialog_class}"
  open-on-workspace = 1
}
EOF
rm -f "${ready_parent}" "${ready_dialog}" "${ready_normal_dialog}"

display_num=90
while [[ ${display_num} -lt 250 ]]; do
  if [[ ! -S "/tmp/.X11-unix/X${display_num}" ]]; then
    break
  fi
  display_num=$((display_num + 1))
done
if [[ ${display_num} -ge 250 ]]; then
  echo "special-overlay-dialog-routing: unable to allocate free X11 display" >&2
  rm -f "${probe_conf_path}"
  exit 2
fi

display=":${display_num}"
xvfb_pid=""
wm_pid=""
parent_pid=""
dialog_pid=""
normal_dialog_pid=""
parent_hex=""

cleanup() {
  for pid_var in normal_dialog_pid dialog_pid parent_pid wm_pid xvfb_pid; do
    pid="${!pid_var:-}"
    if [[ -n "${pid}" ]] && kill -0 "${pid}" 2>/dev/null; then
      kill "${pid}" 2>/dev/null || true
      wait "${pid}" 2>/dev/null || true
    fi
  done
  rm -f "${probe_conf_path}" "${ready_parent}" "${ready_dialog}" "${ready_normal_dialog}"
}
trap cleanup EXIT

zestwm_start_nested_x11 "${display}"

for _ in {1..40}; do
  if ! kill -0 "${xvfb_pid}" 2>/dev/null; then
    echo "special-overlay-dialog-routing: nested X server exited during startup (skipping on this host)" >&2
    exit 77
  fi
  if DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
  echo "special-overlay-dialog-routing: nested X server did not become ready (skipping on this host)" >&2
  exit 77
fi

DISPLAY="${display}" "${zestwm_bin}" -c "${probe_conf_path}" >/tmp/zestwm-special-overlay-dialog-routing.log 2>&1 &
wm_pid=$!

for _ in {1..120}; do
  if DISPLAY="${display}" "${zestctl_bin}" version >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${display}" "${zestctl_bin}" version >/dev/null 2>&1; then
  echo "special-overlay-dialog-routing: zestwm did not respond on ${display}" >&2
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

client_special_tag() {
  local win_hex="$1"
  DISPLAY="${display}" "${zestctl_bin}" clients | awk -v w="${win_hex}" '
    $1 == "win:" w {
      for (i = 1; i <= NF; i++) {
        if ($i ~ /^special_tag:/) {
          sub(/^special_tag:/, "", $i)
          print $i
          exit
        }
      }
    }'
}

special_overlay_visible() {
  local tag="$1"
  DISPLAY="${display}" "${zestctl_bin}" workspaces | awk -v target="name:special:${tag}" '
    $0 ~ target {
      for (i = 1; i <= NF; ++i) {
        if ($i == "visible") {
          found = 1
          exit
        }
      }
      exit
    }
    END { exit(found ? 0 : 1) }
  '
}

kill_stray_normal_clients() {
  local line win
  while IFS= read -r line; do
    if [[ "${line}" =~ win:(0x[0-9a-fA-F]+) ]] && [[ "${line}" == *"special_tag:-"* ]]; then
      win="${BASH_REMATCH[1]}"
      if [[ -n "${parent_hex}" && "${win}" == "${parent_hex}" ]]; then
        continue
      fi
      if command -v xdotool >/dev/null 2>&1; then
        DISPLAY="${display}" xdotool windowkill "${win}" >/dev/null 2>&1 || true
      else
        DISPLAY="${display}" "${zestctl_bin}" dispatch focuswindow "${win}" >/dev/null 2>&1 || true
        DISPLAY="${display}" "${zestctl_bin}" dispatch killclient >/dev/null 2>&1 || true
      fi
    fi
  done < <(DISPLAY="${display}" "${zestctl_bin}" clients || true)
}

sleep 2.5
kill_stray_normal_clients
sleep 0.2
kill_stray_normal_clients

DISPLAY="${display}" "${probe_client_bin}" \
  --title dlg-route-parent \
  --wm-class "${parent_class}" \
  --ready-file "${ready_parent}" >/tmp/zestwm-special-overlay-dialog-routing-parent.log 2>&1 &
parent_pid=$!

parent_dec="$(read_ready_window "${ready_parent}" || true)"
if [[ -z "${parent_dec}" ]]; then
  echo "special-overlay-dialog-routing: failed to read parent window id" >&2
  exit 2
fi
parent_hex="$(printf '0x%08x' "${parent_dec}")"
if ! wait_for_window_present "${parent_hex}"; then
  echo "special-overlay-dialog-routing: parent not listed by zestctl clients" >&2
  exit 1
fi
parent_tag=""
for _ in {1..120}; do
  parent_tag="$(client_special_tag "${parent_hex}")"
  if [[ "${parent_tag}" == "${special_tag}" ]]; then
    break
  fi
  sleep 0.05
done
if [[ "${parent_tag}" != "${special_tag}" ]]; then
  echo "special-overlay-dialog-routing: parent expected special_tag:${special_tag}, got '${parent_tag}'" >&2
  DISPLAY="${display}" "${zestctl_bin}" clients >&2 || true
  exit 1
fi

kill_stray_normal_clients
if ! special_overlay_visible "${special_tag}"; then
  if command -v xdotool >/dev/null 2>&1; then
    DISPLAY="${display}" xdotool windowactivate "${parent_hex}" >/dev/null 2>&1 || true
  fi
fi
for _ in {1..60}; do
  if special_overlay_visible "${special_tag}"; then
    break
  fi
  sleep 0.05
done
if ! special_overlay_visible "${special_tag}"; then
  echo "special-overlay-dialog-routing: special overlay not open before dialog map" >&2
  DISPLAY="${display}" "${zestctl_bin}" workspaces >&2 || true
  exit 1
fi

# Dialog with no rule inherits open special overlay.
DISPLAY="${display}" "${probe_client_bin}" \
  --title dlg-route-dialog \
  --wm-class "${dialog_class}" \
  --dialog \
  --ready-file "${ready_dialog}" >/tmp/zestwm-special-overlay-dialog-routing-dialog.log 2>&1 &
dialog_pid=$!

dialog_dec="$(read_ready_window "${ready_dialog}" || true)"
if [[ -z "${dialog_dec}" ]]; then
  echo "special-overlay-dialog-routing: failed to read dialog window id" >&2
  exit 2
fi
dialog_hex="$(printf '0x%08x' "${dialog_dec}")"
if ! wait_for_window_present "${dialog_hex}"; then
  echo "special-overlay-dialog-routing: dialog not listed by zestctl clients" >&2
  exit 1
fi
dialog_tag="$(client_special_tag "${dialog_hex}")"
if [[ "${dialog_tag}" != "${special_tag}" ]]; then
  echo "special-overlay-dialog-routing: dialog expected special_tag:${special_tag}, got '${dialog_tag}'" >&2
  DISPLAY="${display}" "${zestctl_bin}" clients >&2 || true
  exit 1
fi
if ! special_overlay_visible "${special_tag}"; then
  echo "special-overlay-dialog-routing: overlay should stay open after inherited dialog" >&2
  DISPLAY="${display}" "${zestctl_bin}" workspaces >&2 || true
  exit 1
fi

# Rule-routed dialog onto normal closes the overlay.
DISPLAY="${display}" "${probe_client_bin}" \
  --title dlg-route-normal \
  --wm-class "${normal_dialog_class}" \
  --dialog \
  --ready-file "${ready_normal_dialog}" >/tmp/zestwm-special-overlay-dialog-routing-normal-dialog.log 2>&1 &
normal_dialog_pid=$!

normal_dec="$(read_ready_window "${ready_normal_dialog}" || true)"
if [[ -z "${normal_dec}" ]]; then
  echo "special-overlay-dialog-routing: failed to read normal-routed dialog window id" >&2
  exit 2
fi
normal_hex="$(printf '0x%08x' "${normal_dec}")"
if ! wait_for_window_present "${normal_hex}"; then
  echo "special-overlay-dialog-routing: normal-routed dialog not listed by zestctl clients" >&2
  exit 1
fi
normal_tag="$(client_special_tag "${normal_hex}")"
if [[ "${normal_tag}" != "-" ]]; then
  echo "special-overlay-dialog-routing: normal-routed dialog expected special_tag:-, got '${normal_tag}'" >&2
  DISPLAY="${display}" "${zestctl_bin}" clients >&2 || true
  exit 1
fi
overlay_closed=0
for _ in {1..40}; do
  if ! special_overlay_visible "${special_tag}"; then
    overlay_closed=1
    break
  fi
  sleep 0.05
done
if [[ "${overlay_closed}" -ne 1 ]]; then
  echo "special-overlay-dialog-routing: expected overlay closed after normal-routed dialog" >&2
  DISPLAY="${display}" "${zestctl_bin}" workspaces >&2 || true
  exit 1
fi

echo "special-overlay-dialog-routing: pass"
