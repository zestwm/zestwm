#!/usr/bin/env bash
# Probe: MUST-EWMH-002 UTILITY / transient windows honor fullscreen (pre-map state, ClientMessage, window-rule);
# DIALOG keeps app fullscreen suppressed.
set -euo pipefail

build_dir="${1:-}"
if [[ -z "${build_dir}" ]]; then
  echo "fullscreen-utility-transient: missing build directory argument" >&2
  exit 2
fi

zestwm_bin="${build_dir}/zestwm"
zestctl_bin="${build_dir}/zestctl"
probe_client_bin="${build_dir}/focusurgent-urgent-client-test"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=tests/integration/xserver_common.inc.sh
source "${repo_root}/tests/integration/xserver_common.inc.sh"

base_conf_path="${repo_root}/examples/zestwm.conf"
probe_conf_path="${repo_root}/tests/integration/.fullscreen-utility-transient.conf"
ready_prefix="${repo_root}/tests/integration/.fullscreen-utility-transient"
rule_class="FsRuleUtility"

if [[ ! -x "${zestwm_bin}" || ! -x "${zestctl_bin}" || ! -x "${probe_client_bin}" ]]; then
  echo "fullscreen-utility-transient: missing built binaries in ${build_dir}" >&2
  exit 2
fi

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "fullscreen-utility-transient: missing required command '$1'" >&2
    exit 77
  fi
}

zestwm_require_nested_x11 "fullscreen-utility-transient-probe"
need_cmd xdpyinfo
need_cmd xwininfo
need_cmd xdotool

cp "${base_conf_path}" "${probe_conf_path}"
cat >> "${probe_conf_path}" <<EOF
window-rule {
  match app-id="${rule_class}"
  open-fullscreen = true
}
EOF

display_num=110
while [[ ${display_num} -lt 250 && -S "/tmp/.X11-unix/X${display_num}" ]]; do
  display_num=$((display_num + 1))
done
if [[ ${display_num} -ge 250 ]]; then
  echo "fullscreen-utility-transient: unable to allocate free X11 display" >&2
  rm -f "${probe_conf_path}"
  exit 2
fi

display=":${display_num}"
host_display="${ZESTWM_HOST_DISPLAY:-${DISPLAY:-}}"
if [[ -n "${host_display}" && "${display}" == "${host_display}" ]]; then
  echo "fullscreen-utility-transient: refusing host display '${display}'" >&2
  exit 2
fi

xvfb_pid=""
wm_pid=""

cleanup() {
  for pid in "${wm_pid}" "${xvfb_pid}"; do
    if [[ -n "${pid}" ]] && kill -0 "${pid}" 2>/dev/null; then
      kill "${pid}" 2>/dev/null || true
      wait "${pid}" 2>/dev/null || true
    fi
  done
  rm -f "${probe_conf_path}" "${ready_prefix}".*.ready
}
trap cleanup EXIT

zestwm_start_nested_x11 "${display}"

for _ in {1..40}; do
  if ! kill -0 "${xvfb_pid}" 2>/dev/null; then
    echo "fullscreen-utility-transient: nested X server exited during startup (skipping on this host)" >&2
    exit 77
  fi
  if DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
  echo "fullscreen-utility-transient: nested X server did not become ready (skipping on this host)" >&2
  exit 77
fi

dims="$(DISPLAY="${display}" xdpyinfo | awk '/dimensions:/ { print $2; exit }')"
screen_w="${dims%x*}"
screen_h="${dims#*x}"

DISPLAY="${display}" "${zestwm_bin}" -c "${probe_conf_path}" >/tmp/zestwm-fullscreen-utility-transient.log 2>&1 &
wm_pid=$!

for _ in {1..120}; do
  if DISPLAY="${display}" "${zestctl_bin}" version >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${display}" "${zestctl_bin}" version >/dev/null 2>&1; then
  echo "fullscreen-utility-transient: zestwm did not respond on ${display}" >&2
  exit 2
fi

# Spawn a probe client; prints its window id as 0x%08x.
spawn_client() {
  local name="$1"
  shift
  local ready="${ready_prefix}.${name}.ready"
  rm -f "${ready}"
  DISPLAY="${display}" "${probe_client_bin}" --title "fs-${name}" --ready-file "${ready}" "$@" \
    >"/tmp/zestwm-fullscreen-utility-transient-${name}.log" 2>&1 &
  for _ in {1..120}; do
    if [[ -s "${ready}" ]]; then
      local dec
      dec="$(tr -d '[:space:]' < "${ready}")"
      if [[ "${dec}" =~ ^[0-9]+$ ]]; then
        printf '0x%08x' "${dec}"
        return 0
      fi
    fi
    sleep 0.05
  done
  echo "fullscreen-utility-transient: ${name}: failed to read window id" >&2
  exit 2
}

window_geometry() {
  DISPLAY="${display}" xwininfo -id "$1" | awk '
    /Absolute upper-left X:/ {x=$4} /Absolute upper-left Y:/ {y=$4} /Width:/ {w=$2} /Height:/ {h=$2}
    END {print x "/" y "/" w "/" h}'
}

client_fullscreen_field() {
  DISPLAY="${display}" "${zestctl_bin}" clients | awk -v w="win:$1" '$1 == w { for (i = 2; i <= NF; i++) if ($i ~ /^fullscreen:/) { sub(/^fullscreen:/, "", $i); print $i; exit } }'
}

# Wait until zestctl reports the wanted fullscreen state (yes|no) for the window.
expect_fullscreen() {
  local win="$1" want="$2" label="$3" got=""
  for _ in {1..60}; do
    got="$(client_fullscreen_field "${win}")"
    [[ "${got}" == "${want}" ]] && break
    sleep 0.05
  done
  if [[ "${got}" != "${want}" ]]; then
    echo "fullscreen-utility-transient: ${label}: expected fullscreen:${want}, got '${got}'" >&2
    DISPLAY="${display}" "${zestctl_bin}" clients >&2 || true
    exit 1
  fi
  if [[ "${want}" == "yes" && "$(window_geometry "${win}")" != "0/0/${screen_w}/${screen_h}" ]]; then
    echo "fullscreen-utility-transient: ${label}: geometry $(window_geometry "${win}") != 0/0/${screen_w}/${screen_h}" >&2
    exit 1
  fi
  echo "fullscreen-utility-transient: ${label}: ok"
}

# Case 1: UTILITY with _NET_WM_STATE_FULLSCREEN set before map (map-time EWMH path).
util_win="$(spawn_client utility --wm-class FsUtility --utility --fullscreen)"
expect_fullscreen "${util_win}" yes "utility pre-map state"

# Case 2: transient UTILITY requests fullscreen after map (_NET_WM_STATE ClientMessage path).
parent_win="$(spawn_client parent --wm-class FsParent)"
trans_win="$(spawn_client transient --wm-class FsTransient --utility --transient-for "${parent_win}")"
expect_fullscreen "${trans_win}" no "transient before request"
DISPLAY="${display}" xdotool windowstate --add FULLSCREEN "${trans_win}"
expect_fullscreen "${trans_win}" yes "transient clientmessage add"
DISPLAY="${display}" xdotool windowstate --remove FULLSCREEN "${trans_win}"
expect_fullscreen "${trans_win}" no "transient clientmessage remove"

# Case 3: window-rule open-fullscreen on a UTILITY window.
rule_win="$(spawn_client rule --wm-class "${rule_class}" --utility)"
expect_fullscreen "${rule_win}" yes "utility window-rule open-fullscreen"

# Case 4: DIALOG keeps app-driven fullscreen suppressed.
dialog_win="$(spawn_client dialog --wm-class FsDialog --dialog --fullscreen)"
# zestctl echoes the client-set _NET_WM_STATE here, so assert WM did not apply fullscreen geometry.
sleep 0.2
if [[ "$(window_geometry "${dialog_win}")" == "0/0/${screen_w}/${screen_h}" ]]; then
  echo "fullscreen-utility-transient: dialog: got fullscreen geometry, expected suppression" >&2
  exit 1
fi
echo "fullscreen-utility-transient: dialog stays suppressed: ok"

if ! kill -0 "${wm_pid}" 2>/dev/null; then
  echo "fullscreen-utility-transient: zestwm exited during probe" >&2
  exit 1
fi
echo "fullscreen-utility-transient: pass"
