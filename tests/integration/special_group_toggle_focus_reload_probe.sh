#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-}"
if [[ -z "${build_dir}" ]]; then
  echo "special-group-toggle-focus-reload: missing build directory argument" >&2
  exit 2
fi

zestwm_bin="${build_dir}/zestwm"
zestctl_bin="${build_dir}/zestctl"
probe_client_bin="${build_dir}/focusurgent-urgent-client-test"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=tests/integration/xserver_common.inc.sh
source "${repo_root}/tests/integration/xserver_common.inc.sh"

base_conf_path="${repo_root}/examples/zestwm.conf"
probe_conf_path="${repo_root}/tests/integration/.special-group-toggle-focus-reload.conf"
tag_magic="magic"
tag_dropdown="dropdown"

ready_ws1_a="${repo_root}/tests/integration/.special-group-toggle-focus-reload.ws1-a.ready"
ready_ws1_b="${repo_root}/tests/integration/.special-group-toggle-focus-reload.ws1-b.ready"
ready_magic_a="${repo_root}/tests/integration/.special-group-toggle-focus-reload.magic-a.ready"
ready_magic_b="${repo_root}/tests/integration/.special-group-toggle-focus-reload.magic-b.ready"
ready_drop_a="${repo_root}/tests/integration/.special-group-toggle-focus-reload.drop-a.ready"
ready_drop_b="${repo_root}/tests/integration/.special-group-toggle-focus-reload.drop-b.ready"

if [[ ! -x "${zestwm_bin}" || ! -x "${zestctl_bin}" || ! -x "${probe_client_bin}" ]]; then
  echo "special-group-toggle-focus-reload: missing built binaries in ${build_dir}" >&2
  exit 2
fi
if [[ ! -f "${base_conf_path}" ]]; then
  echo "special-group-toggle-focus-reload: missing config fixture ${base_conf_path}" >&2
  exit 2
fi

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "special-group-toggle-focus-reload: missing required command '$1'" >&2
    exit 77
  fi
}

zestwm_require_nested_x11 "special-group-toggle-focus-reload-probe"
need_cmd xdpyinfo
need_cmd xdotool

cp "${base_conf_path}" "${probe_conf_path}"
cat >> "${probe_conf_path}" <<EOF
workspace = special:${tag_magic}
workspace = special:${tag_dropdown}
window-rule {
  match app-id="SpWs1A"
  open-on-workspace = 1
}
window-rule {
  match app-id="SpWs1B"
  open-on-workspace = 1
}
window-rule {
  match app-id="SpMagicA"
  open-on-workspace = special:${tag_magic}
}
window-rule {
  match app-id="SpMagicB"
  open-on-workspace = special:${tag_magic}
}
window-rule {
  match app-id="SpDropA"
  open-on-workspace = special:${tag_dropdown}
}
window-rule {
  match app-id="SpDropB"
  open-on-workspace = special:${tag_dropdown}
}
binds { ALT+g { groupmode -1; } }
EOF

rm -f "${ready_ws1_a}" "${ready_ws1_b}" "${ready_magic_a}" "${ready_magic_b}" "${ready_drop_a}" "${ready_drop_b}"

display_num=170
while [[ ${display_num} -lt 230 ]]; do
  if [[ ! -S "/tmp/.X11-unix/X${display_num}" ]]; then
    break
  fi
  display_num=$((display_num + 1))
done
if [[ ${display_num} -ge 230 ]]; then
  echo "special-group-toggle-focus-reload: unable to allocate free X11 display" >&2
  rm -f "${probe_conf_path}"
  exit 2
fi

display=":${display_num}"
xvfb_pid=""
wm_pid=""
pids=()

cleanup() {
  for pid in "${pids[@]}"; do
    if [[ -n "${pid}" ]] && kill -0 "${pid}" 2>/dev/null; then
      kill "${pid}" 2>/dev/null || true
      wait "${pid}" 2>/dev/null || true
    fi
  done
  if [[ -n "${wm_pid}" ]] && kill -0 "${wm_pid}" 2>/dev/null; then
    kill "${wm_pid}" 2>/dev/null || true
    wait "${wm_pid}" 2>/dev/null || true
  fi
  if [[ -n "${xvfb_pid}" ]] && kill -0 "${xvfb_pid}" 2>/dev/null; then
    kill "${xvfb_pid}" 2>/dev/null || true
    wait "${xvfb_pid}" 2>/dev/null || true
  fi
  rm -f "${probe_conf_path}" "${ready_ws1_a}" "${ready_ws1_b}" "${ready_magic_a}" "${ready_magic_b}" "${ready_drop_a}" "${ready_drop_b}"
}
trap cleanup EXIT

zestwm_start_nested_x11 "${display}"

for _ in {1..40}; do
  if ! kill -0 "${xvfb_pid}" 2>/dev/null; then
    echo "special-group-toggle-focus-reload: nested X server exited during startup (skipping on this host)" >&2
    exit 77
  fi
  if DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
  echo "special-group-toggle-focus-reload: nested X server did not become ready (skipping on this host)" >&2
  exit 77
fi

DISPLAY="${display}" "${zestwm_bin}" -c "${probe_conf_path}" >/tmp/zestwm-special-group-toggle-focus-reload.log 2>&1 &
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
    echo "special-group-toggle-focus-reload: zestwm crashed unexpectedly" >&2
    exit 1
  fi
}

run_dispatch() {
  if ! DISPLAY="${display}" "${zestctl_bin}" dispatch "$@" >/dev/null; then
    echo "special-group-toggle-focus-reload: dispatch $* failed" >&2
    exit 1
  fi
  assert_wm_alive
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

spawn_client() {
  local title="$1"
  local klass="$2"
  local instance="$3"
  local ready="$4"
  DISPLAY="${display}" "${probe_client_bin}" --title "${title}" --wm-class "${klass}" --wm-instance "${instance}" --ready-file "${ready}" >/tmp/zestwm-"${instance}".log 2>&1 &
  pids+=("$!")
  local win_dec
  win_dec="$(read_ready_window "${ready}" || true)"
  if [[ -z "${win_dec}" ]]; then
    echo "special-group-toggle-focus-reload: failed to read window id for ${instance}" >&2
    exit 2
  fi
  printf '0x%08x' "${win_dec}"
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

active_window_hex() {
  DISPLAY="${display}" "${zestctl_bin}" activewindow | awk '/^win:/ {print $1}' | sed 's/^win://'
}

special_visible_for_tag() {
  local tag="$1"
  DISPLAY="${display}" "${zestctl_bin}" workspaces | awk -v target="name:special:${tag}" '
    $0 ~ target {
      for (i = 1; i <= NF; ++i) {
        if ($i == "visible") {
          print "yes";
          exit 0;
        }
      }
      print "no";
      exit 0;
    }
    END { if (NR == 0) print "no" }'
}

wait_for_active_window() {
  local expected="$1"
  for _ in {1..120}; do
    local got
    got="$(active_window_hex | tr -d '[:space:]')"
    if [[ "${got}" == "${expected}" ]]; then
      return 0
    fi
    sleep 0.05
  done
  return 1
}

get_client_field() {
  local win_hex="$1"
  local key="$2"
  DISPLAY="${display}" "${zestctl_bin}" clients | awk -v w="${win_hex}" -v k="${key}" '
    $1 == "win:" w {
      for (i = 1; i <= NF; ++i) {
        if ($i ~ ("^" k ":")) {
          sub(("^" k ":"), "", $i);
          print $i;
          exit 0;
        }
      }
    }'
}

assert_client_location() {
  local win_hex="$1"
  local want_ws="$2"
  local want_tag="$3"
  local got_ws got_tag
  got_ws="$(get_client_field "${win_hex}" "ws" | tr -d '[:space:]')"
  got_tag="$(get_client_field "${win_hex}" "special_tag" | tr -d '[:space:]')"
  if [[ "${got_ws}" != "${want_ws}" || "${got_tag}" != "${want_tag}" ]]; then
    echo "special-group-toggle-focus-reload: expected ${win_hex} ws:${want_ws} special_tag:${want_tag}, got ws:${got_ws:-<empty>} special_tag:${got_tag:-<empty>}" >&2
    DISPLAY="${display}" "${zestctl_bin}" clients >&2 || true
    exit 1
  fi
}

assert_group_pair() {
  local win_a="$1"
  local win_b="$2"
  local size_a size_b grouped_a grouped_b
  size_a="$(get_client_field "${win_a}" "group_size" | tr -d '[:space:]')"
  size_b="$(get_client_field "${win_b}" "group_size" | tr -d '[:space:]')"
  grouped_a="$(get_client_field "${win_a}" "grouped" | tr -d '[:space:]')"
  grouped_b="$(get_client_field "${win_b}" "grouped" | tr -d '[:space:]')"
  if ! { [[ "${grouped_a}" == "yes" && "${size_a}" =~ ^[0-9]+$ && "${size_a}" -ge 2 ]] || [[ "${grouped_b}" == "yes" && "${size_b}" =~ ^[0-9]+$ && "${size_b}" -ge 2 ]]; }; then
    echo "special-group-toggle-focus-reload: expected grouped pair for ${win_a}/${win_b}, got grouped=${grouped_a}/${grouped_b} size=${size_a}/${size_b}" >&2
    DISPLAY="${display}" "${zestctl_bin}" clients >&2 || true
    exit 1
  fi
}

create_group_for_pair() {
  local focus_hex="$1"
  local second_spawn_fn="$2"
  run_dispatch focuswindow "${focus_hex}"
  if ! wait_for_active_window "${focus_hex}"; then
    echo "special-group-toggle-focus-reload: cannot focus ${focus_hex} before single-client group toggle" >&2
    exit 1
  fi
  DISPLAY="${display}" xdotool key alt+g
  sleep 0.1
  DISPLAY="${display}" xdotool key alt+g
  sleep 0.1
  eval "${second_spawn_fn}"
  run_dispatch focuswindow "${focus_hex}"
  if ! wait_for_active_window "${focus_hex}"; then
    echo "special-group-toggle-focus-reload: cannot refocus ${focus_hex} before regroup" >&2
    exit 1
  fi
  DISPLAY="${display}" xdotool key alt+g
  sleep 0.12
}

if ! wait_for_wm_ready; then
  echo "special-group-toggle-focus-reload: zestwm did not respond on ${display}" >&2
  exit 2
fi

# ws1 group (2 clients)
win_ws1_a_hex="$(spawn_client "spgrp-ws1-a" "SpWs1A" "spgrp-ws1-a" "${ready_ws1_a}")"
if ! wait_for_window_present "${win_ws1_a_hex}"; then
  echo "special-group-toggle-focus-reload: ws1-a not listed" >&2
  exit 1
fi
create_group_for_pair "${win_ws1_a_hex}" 'win_ws1_b_hex="$(spawn_client "spgrp-ws1-b" "SpWs1B" "spgrp-ws1-b" "'"${ready_ws1_b}"'")"; wait_for_window_present "${win_ws1_b_hex}" >/dev/null'

# special:magic group (2 clients)
run_dispatch special "${tag_magic}"
win_magic_a_hex="$(spawn_client "spgrp-magic-a" "SpMagicA" "spgrp-magic-a" "${ready_magic_a}")"
if ! wait_for_window_present "${win_magic_a_hex}"; then
  echo "special-group-toggle-focus-reload: magic-a not listed" >&2
  exit 1
fi
create_group_for_pair "${win_magic_a_hex}" 'win_magic_b_hex="$(spawn_client "spgrp-magic-b" "SpMagicB" "spgrp-magic-b" "'"${ready_magic_b}"'")"; wait_for_window_present "${win_magic_b_hex}" >/dev/null'

# special:dropdown group (2 clients)
run_dispatch special "${tag_dropdown}"
win_drop_a_hex="$(spawn_client "spgrp-drop-a" "SpDropA" "spgrp-drop-a" "${ready_drop_a}")"
if ! wait_for_window_present "${win_drop_a_hex}"; then
  echo "special-group-toggle-focus-reload: drop-a not listed" >&2
  exit 1
fi
create_group_for_pair "${win_drop_a_hex}" 'win_drop_b_hex="$(spawn_client "spgrp-drop-b" "SpDropB" "spgrp-drop-b" "'"${ready_drop_b}"'")"; wait_for_window_present "${win_drop_b_hex}" >/dev/null'

# Baseline assertions before reload.
assert_client_location "${win_ws1_a_hex}" "1" "-"
assert_client_location "${win_ws1_b_hex}" "1" "-"
assert_client_location "${win_magic_a_hex}" "0" "${tag_magic}"
assert_client_location "${win_magic_b_hex}" "0" "${tag_magic}"
assert_client_location "${win_drop_a_hex}" "0" "${tag_dropdown}"
assert_client_location "${win_drop_b_hex}" "0" "${tag_dropdown}"

# Switch to first special workspace, then reload.
run_dispatch special "${tag_magic}"
run_dispatch reload
sleep 0.35
if ! wait_for_wm_ready; then
  echo "special-group-toggle-focus-reload: zestwm did not respond after reload" >&2
  exit 1
fi
assert_wm_alive

for win in "${win_ws1_a_hex}" "${win_ws1_b_hex}" "${win_magic_a_hex}" "${win_magic_b_hex}" "${win_drop_a_hex}" "${win_drop_b_hex}"; do
  if ! wait_for_window_present "${win}"; then
    echo "special-group-toggle-focus-reload: missing window after reload ${win}" >&2
    DISPLAY="${display}" "${zestctl_bin}" clients >&2 || true
    exit 1
  fi
done

# Final placement + grouping assertions after reload.
assert_client_location "${win_ws1_a_hex}" "1" "-"
assert_client_location "${win_ws1_b_hex}" "1" "-"
assert_client_location "${win_magic_a_hex}" "0" "${tag_magic}"
assert_client_location "${win_magic_b_hex}" "0" "${tag_magic}"
assert_client_location "${win_drop_a_hex}" "0" "${tag_dropdown}"
assert_client_location "${win_drop_b_hex}" "0" "${tag_dropdown}"

# Simulate app activation request while special is hidden (Telegram-like flow).
run_dispatch workspace 1
DISPLAY="${display}" xdotool windowactivate "${win_magic_b_hex}" || true
for _ in {1..120}; do
  if [[ "$(special_visible_for_tag "${tag_magic}" | tr -d '[:space:]')" == "yes" ]]; then
    break
  fi
  sleep 0.05
done
if [[ "$(special_visible_for_tag "${tag_magic}" | tr -d '[:space:]')" != "yes" ]]; then
  echo "special-group-toggle-focus-reload: activate request did not open special:${tag_magic}" >&2
  DISPLAY="${display}" "${zestctl_bin}" workspaces >&2 || true
  exit 1
fi
assert_client_location "${win_ws1_a_hex}" "1" "-"
assert_client_location "${win_ws1_b_hex}" "1" "-"
assert_client_location "${win_magic_a_hex}" "0" "${tag_magic}"
assert_client_location "${win_magic_b_hex}" "0" "${tag_magic}"
assert_client_location "${win_drop_a_hex}" "0" "${tag_dropdown}"
assert_client_location "${win_drop_b_hex}" "0" "${tag_dropdown}"

# Second reload while currently on special:magic.
run_dispatch special "${tag_magic}"
run_dispatch reload
sleep 0.35
if ! wait_for_wm_ready; then
  echo "special-group-toggle-focus-reload: zestwm did not respond after second reload" >&2
  exit 1
fi
assert_wm_alive

for win in "${win_ws1_a_hex}" "${win_ws1_b_hex}" "${win_magic_a_hex}" "${win_magic_b_hex}" "${win_drop_a_hex}" "${win_drop_b_hex}"; do
  if ! wait_for_window_present "${win}"; then
    echo "special-group-toggle-focus-reload: missing window after second reload ${win}" >&2
    DISPLAY="${display}" "${zestctl_bin}" clients >&2 || true
    exit 1
  fi
done

assert_client_location "${win_ws1_a_hex}" "1" "-"
assert_client_location "${win_ws1_b_hex}" "1" "-"
assert_client_location "${win_magic_a_hex}" "0" "${tag_magic}"
assert_client_location "${win_magic_b_hex}" "0" "${tag_magic}"
assert_client_location "${win_drop_a_hex}" "0" "${tag_dropdown}"
assert_client_location "${win_drop_b_hex}" "0" "${tag_dropdown}"

echo "special-group-toggle-focus-reload: pass"
