#!/usr/bin/env bash
# Reload while a normal workspace has an active group must not force-merge ungrouped
# special:<tag> clients into one tab group (telegram/signal on magic regression).
set -euo pipefail

build_dir="${1:-}"
if [[ -z "${build_dir}" ]]; then
  echo "reload-groupmode-special-ungrouped: missing build directory argument" >&2
  exit 2
fi

zestwm_bin="${build_dir}/zestwm"
zestctl_bin="${build_dir}/zestctl"
probe_client_bin="${build_dir}/focusurgent-urgent-client-test"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=tests/integration/xserver_common.inc.sh
source "${repo_root}/tests/integration/xserver_common.inc.sh"

base_conf_path="${repo_root}/examples/zestwm.conf"
probe_conf_path="${repo_root}/tests/integration/.reload-groupmode-special-ungrouped.conf"
ready_ws1_a="${repo_root}/tests/integration/.reload-groupmode-special-ungrouped.ws1-a.ready"
ready_ws1_b="${repo_root}/tests/integration/.reload-groupmode-special-ungrouped.ws1-b.ready"
ready_magic_a="${repo_root}/tests/integration/.reload-groupmode-special-ungrouped.magic-a.ready"
ready_magic_b="${repo_root}/tests/integration/.reload-groupmode-special-ungrouped.magic-b.ready"
special_tag="magic"

if [[ ! -x "${zestwm_bin}" || ! -x "${zestctl_bin}" || ! -x "${probe_client_bin}" ]]; then
  echo "reload-groupmode-special-ungrouped: missing built binaries in ${build_dir}" >&2
  exit 2
fi
if [[ ! -f "${base_conf_path}" ]]; then
  echo "reload-groupmode-special-ungrouped: missing config fixture ${base_conf_path}" >&2
  exit 2
fi

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "reload-groupmode-special-ungrouped: missing required command '$1'" >&2
    exit 77
  fi
}

zestwm_require_nested_x11 "reload-groupmode-special-ungrouped-probe"
need_cmd xdpyinfo
need_cmd xdotool

cp "${base_conf_path}" "${probe_conf_path}"
cat >> "${probe_conf_path}" <<EOF
workspace = special:${special_tag}
window-rule {
  match app-id="RelGrpWs1A"
  open-on-workspace = 1
}
window-rule {
  match app-id="RelGrpWs1B"
  open-on-workspace = 1
}
window-rule {
  match app-id="RelGrpMagicA"
  open-on-workspace = special:${special_tag} silent
}
window-rule {
  match app-id="RelGrpMagicB"
  open-on-workspace = special:${special_tag} silent
}
binds {
  ALT+g { groupmode -1; }
}
EOF

rm -f "${ready_ws1_a}" "${ready_ws1_b}" "${ready_magic_a}" "${ready_magic_b}"

display_num=150
while [[ ${display_num} -lt 230 ]]; do
  if [[ ! -S "/tmp/.X11-unix/X${display_num}" ]]; then
    break
  fi
  display_num=$((display_num + 1))
done
if [[ ${display_num} -ge 230 ]]; then
  echo "reload-groupmode-special-ungrouped: unable to allocate free X11 display" >&2
  rm -f "${probe_conf_path}"
  exit 2
fi

display=":${display_num}"
xvfb_pid=""
wm_pid=""
pids=()

cleanup() {
  for pid in "${pids[@]:-}"; do
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
  rm -f "${probe_conf_path}" "${ready_ws1_a}" "${ready_ws1_b}" "${ready_magic_a}" "${ready_magic_b}"
}
trap cleanup EXIT

zestwm_start_nested_x11 "${display}"

DISPLAY="${display}" "${zestwm_bin}" -c "${probe_conf_path}" >/tmp/zestwm-reload-groupmode-special-ungrouped.log 2>&1 &
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
    echo "reload-groupmode-special-ungrouped: zestwm crashed unexpectedly" >&2
    exit 1
  fi
}

run_dispatch() {
  DISPLAY="${display}" "${zestctl_bin}" dispatch "$@" >/dev/null
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

spawn_client() {
  local title="$1"
  local klass="$2"
  local instance="$3"
  local ready="$4"
  rm -f "${ready}"
  DISPLAY="${display}" "${probe_client_bin}" --title "${title}" --wm-class "${klass}" --wm-instance "${instance}" \
    --ready-file "${ready}" >/tmp/zestwm-reload-groupmode-special-ungrouped-"${instance}".log 2>&1 &
  pids+=("$!")
  local win_dec
  win_dec="$(read_ready_window "${ready}" || true)"
  if [[ -z "${win_dec}" ]]; then
    echo "reload-groupmode-special-ungrouped: failed to read ready window for ${instance}" >&2
    exit 2
  fi
  printf '0x%08x' "${win_dec}"
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
    echo "reload-groupmode-special-ungrouped: expected ${win_hex} ws:${want_ws} special_tag:${want_tag}, got ws:${got_ws:-<empty>} special_tag:${got_tag:-<empty>}" >&2
    DISPLAY="${display}" "${zestctl_bin}" clients >&2 || true
    exit 1
  fi
}

assert_not_grouped_together() {
  local win_a="$1"
  local win_b="$2"
  local size_a size_b grouped_a grouped_b
  size_a="$(get_client_field "${win_a}" "group_size" | tr -d '[:space:]')"
  size_b="$(get_client_field "${win_b}" "group_size" | tr -d '[:space:]')"
  grouped_a="$(get_client_field "${win_a}" "grouped" | tr -d '[:space:]')"
  grouped_b="$(get_client_field "${win_b}" "grouped" | tr -d '[:space:]')"
  if [[ "${grouped_a}" == "yes" && "${size_a}" =~ ^[0-9]+$ && "${size_a}" -ge 2 ]] || [[ "${grouped_b}" == "yes" && "${size_b}" =~ ^[0-9]+$ && "${size_b}" -ge 2 ]]; then
    echo "reload-groupmode-special-ungrouped: special clients were force-grouped (${win_a}/${win_b} grouped=${grouped_a}/${grouped_b} size=${size_a}/${size_b})" >&2
    DISPLAY="${display}" "${zestctl_bin}" clients >&2 || true
    exit 1
  fi
}

active_window_hex() {
  DISPLAY="${display}" "${zestctl_bin}" activewindow | awk '/^win:/ {print $1}' | sed 's/^win://'
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

if ! wait_for_wm_ready; then
  echo "reload-groupmode-special-ungrouped: zestwm did not respond on ${display}" >&2
  exit 2
fi

# Active group on workspace 1 (groupmode on + second member).
win_ws1_a_hex="$(spawn_client "relgrp-ws1-a" "RelGrpWs1A" "relgrp-ws1-a" "${ready_ws1_a}")"
if ! wait_for_window_present "${win_ws1_a_hex}"; then
  echo "reload-groupmode-special-ungrouped: ws1-a not listed" >&2
  exit 1
fi
run_dispatch focuswindow "${win_ws1_a_hex}"
if ! wait_for_active_window "${win_ws1_a_hex}"; then
  echo "reload-groupmode-special-ungrouped: cannot focus ws1-a before groupmode" >&2
  exit 1
fi
DISPLAY="${display}" xdotool key alt+g
sleep 0.1
if [[ "$(get_client_field "${win_ws1_a_hex}" "groupmode" | tr -d '[:space:]')" != "yes" ]]; then
  echo "reload-groupmode-special-ungrouped: expected groupmode:yes on ws1-a before spawning partner" >&2
  DISPLAY="${display}" "${zestctl_bin}" clients >&2 || true
  exit 1
fi
win_ws1_b_hex="$(spawn_client "relgrp-ws1-b" "RelGrpWs1B" "relgrp-ws1-b" "${ready_ws1_b}")"
if ! wait_for_window_present "${win_ws1_b_hex}"; then
  echo "reload-groupmode-special-ungrouped: ws1-b not listed" >&2
  exit 1
fi
if [[ "$(get_client_field "${win_ws1_a_hex}" "group_size" | tr -d '[:space:]')" != "2" ]]; then
  echo "reload-groupmode-special-ungrouped: expected ws1 group_size:2 before reload" >&2
  DISPLAY="${display}" "${zestctl_bin}" clients >&2 || true
  exit 1
fi

# Two silent special:magic clients (must stay ungrouped / split).
win_magic_a_hex="$(spawn_client "relgrp-magic-a" "RelGrpMagicA" "relgrp-magic-a" "${ready_magic_a}")"
if ! wait_for_window_present "${win_magic_a_hex}"; then
  echo "reload-groupmode-special-ungrouped: magic-a not listed" >&2
  exit 1
fi
win_magic_b_hex="$(spawn_client "relgrp-magic-b" "RelGrpMagicB" "relgrp-magic-b" "${ready_magic_b}")"
if ! wait_for_window_present "${win_magic_b_hex}"; then
  echo "reload-groupmode-special-ungrouped: magic-b not listed" >&2
  exit 1
fi

assert_client_location "${win_ws1_a_hex}" "1" "-"
assert_client_location "${win_ws1_b_hex}" "1" "-"
assert_client_location "${win_magic_a_hex}" "0" "${special_tag}"
assert_client_location "${win_magic_b_hex}" "0" "${special_tag}"
assert_not_grouped_together "${win_magic_a_hex}" "${win_magic_b_hex}"

# Reload while ws1 group remains the current/active context.
run_dispatch workspace 1
run_dispatch focuswindow "${win_ws1_a_hex}"
run_dispatch reload
sleep 0.35
if ! wait_for_wm_ready; then
  echo "reload-groupmode-special-ungrouped: zestwm did not respond after reload" >&2
  exit 1
fi
assert_wm_alive

for win in "${win_ws1_a_hex}" "${win_ws1_b_hex}" "${win_magic_a_hex}" "${win_magic_b_hex}"; do
  if ! wait_for_window_present "${win}"; then
    echo "reload-groupmode-special-ungrouped: missing window after reload ${win}" >&2
    DISPLAY="${display}" "${zestctl_bin}" clients >&2 || true
    exit 1
  fi
done

assert_client_location "${win_ws1_a_hex}" "1" "-"
assert_client_location "${win_ws1_b_hex}" "1" "-"
assert_client_location "${win_magic_a_hex}" "0" "${special_tag}"
assert_client_location "${win_magic_b_hex}" "0" "${special_tag}"
assert_not_grouped_together "${win_magic_a_hex}" "${win_magic_b_hex}"

# ws1 group membership should still be a pair.
ws1_size="$(get_client_field "${win_ws1_a_hex}" "group_size" | tr -d '[:space:]')"
if [[ "${ws1_size}" != "2" ]]; then
  echo "reload-groupmode-special-ungrouped: expected ws1 group_size:2 after reload, got ${ws1_size:-<empty>}" >&2
  DISPLAY="${display}" "${zestctl_bin}" clients >&2 || true
  exit 1
fi

echo "reload-groupmode-special-ungrouped: pass"
