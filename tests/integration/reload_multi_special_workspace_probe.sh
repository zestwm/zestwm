#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-}"
if [[ -z "${build_dir}" ]]; then
  echo "reload-multi-special-workspace: missing build directory argument" >&2
  exit 2
fi

zestwm_bin="${build_dir}/zestwm"
zestctl_bin="${build_dir}/zestctl"
probe_client_bin="${build_dir}/focusurgent-urgent-client-test"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=tests/integration/xserver_common.inc.sh
source "${repo_root}/tests/integration/xserver_common.inc.sh"

base_conf_path="${repo_root}/examples/zestwm.conf"
probe_conf_path="${repo_root}/tests/integration/.reload-multi-special-workspace.conf"
ready_a="${repo_root}/tests/integration/.reload-multi-special.readyA"
ready_b="${repo_root}/tests/integration/.reload-multi-special.readyB"
ready_c="${repo_root}/tests/integration/.reload-multi-special.readyC"
tag_a="msprela"
tag_b="msprelb"
title_aa="msprobe-reload-aa"

if [[ ! -x "${zestwm_bin}" || ! -x "${zestctl_bin}" || ! -x "${probe_client_bin}" ]]; then
  echo "reload-multi-special-workspace: missing built binaries in ${build_dir}" >&2
  exit 2
fi
if [[ ! -f "${base_conf_path}" ]]; then
  echo "reload-multi-special-workspace: missing config fixture ${base_conf_path}" >&2
  exit 2
fi

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "reload-multi-special-workspace: missing required command '$1'" >&2
    exit 77
  fi
}

zestwm_require_nested_x11 "reload-multi-special-workspace-probe"
need_cmd xdpyinfo
need_cmd xdotool

cp "${base_conf_path}" "${probe_conf_path}"
cat >> "${probe_conf_path}" <<EOF
workspace = special:${tag_a}
workspace = special:${tag_b}
window-rule {
  match app-id="FocusurgentProbe" title="msprobe-reload-a"
  open-on-workspace = special:${tag_a}
}
window-rule {
  match app-id="FocusurgentProbe" title="msprobe-reload-b"
  open-on-workspace = special:${tag_b}
}
window-rule {
  match app-id="FocusurgentProbe" title="${title_aa}"
  open-on-workspace = special:${tag_a}
}
binds {
  ALT+g { groupmode -1; }
  ALT+c { cyclegroup 1; }
}
EOF
rm -f "${ready_a}" "${ready_b}" "${ready_c}"

display_num=90
while [[ ${display_num} -lt 110 ]]; do
  if [[ ! -S "/tmp/.X11-unix/X${display_num}" ]]; then
    break
  fi
  display_num=$((display_num + 1))
done
if [[ ${display_num} -ge 110 ]]; then
  echo "reload-multi-special-workspace: unable to allocate free X11 display" >&2
  rm -f "${probe_conf_path}"
  exit 2
fi

display=":${display_num}"
host_display="${ZESTWM_HOST_DISPLAY:-${DISPLAY:-}}"
if [[ -n "${host_display}" && "${display}" == "${host_display}" ]]; then
  echo "reload-multi-special-workspace: refusing host display '${display}'" >&2
  rm -f "${probe_conf_path}" "${ready_a}" "${ready_b}" "${ready_c}"
  exit 2
fi
xvfb_pid=""
wm_pid=""
client_a_pid=""
client_b_pid=""
client_c_pid=""

cleanup() {
  for pid in "${client_a_pid}" "${client_b_pid}" "${client_c_pid}"; do
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
  rm -f "${probe_conf_path}" "${ready_a}" "${ready_b}" "${ready_c}"
}
trap cleanup EXIT

zestwm_start_nested_x11 "${display}"

for _ in {1..40}; do
  if ! kill -0 "${xvfb_pid}" 2>/dev/null; then
    echo "reload-multi-special-workspace: nested X server exited during startup (skipping on this host)" >&2
    exit 77
  fi
  if DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
  echo "reload-multi-special-workspace: nested X server did not become ready (skipping on this host)" >&2
  exit 77
fi

DISPLAY="${display}" "${zestwm_bin}" -c "${probe_conf_path}" >/tmp/zestwm-reload-multi-special.log 2>&1 &
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
    echo "reload-multi-special-workspace: zestwm crashed unexpectedly" >&2
    exit 1
  fi
}

run_dispatch() {
  if ! DISPLAY="${display}" "${zestctl_bin}" dispatch "$@"; then
    echo "reload-multi-special-workspace: dispatch $* failed" >&2
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

special_tag_for_window() {
  local win_hex="$1"
  DISPLAY="${display}" "${zestctl_bin}" clients | awk -v w="${win_hex}" '
    $1 == "win:" w {
      for (i = 1; i <= NF; ++i) {
        if ($i ~ /^special_tag:/) {
          sub(/^special_tag:/, "", $i);
          print $i;
          exit 0;
        }
      }
    }'
}

workspace_ws_for_window() {
  local win_hex="$1"
  DISPLAY="${display}" "${zestctl_bin}" clients | awk -v w="${win_hex}" '
    $1 == "win:" w {
      for (i = 1; i <= NF; ++i) {
        if ($i ~ /^ws:/) {
          sub(/^ws:/, "", $i);
          print $i;
          exit 0;
        }
      }
    }'
}

workspace_windows_by_id() {
  local ws_id="$1"
  DISPLAY="${display}" "${zestctl_bin}" workspaces | awk -v target="${ws_id}" '
    $2 == "id:" target {
      for (i = 1; i <= NF; ++i) {
        if ($i ~ /^windows:/) {
          sub(/^windows:/, "", $i);
          print $i;
          exit 0;
        }
      }
    }'
}

# Count normal-desktop probe clients by `zestctl clients` (ignores unrelated nested-display noise).
probe_normal_workspace_client_count() {
  local ws_id="$1"
  DISPLAY="${display}" "${zestctl_bin}" clients | awk -v target="${ws_id}" '
    $0 ~ /^win:/ && $0 ~ /class:FocusurgentProbe/ {
      ws = 0;
      sp = "-";
      for (i = 1; i <= NF; ++i) {
        if ($i ~ /^ws:/) {
          sub(/^ws:/, "", $i);
          ws = $i + 0;
        }
        if ($i ~ /^special_tag:/) {
          sub(/^special_tag:/, "", $i);
          sp = $i;
        }
      }
      if (sp == "-" && ws == target)
        count++;
    }
    END {
      print count + 0;
    }'
}

hidden_id_for_special_tag() {
  local tag="$1"
  DISPLAY="${display}" "${zestctl_bin}" workspaces | awk -v t="special:${tag}" '
    $0 ~ ("name:" t) {
      for (i = 1; i <= NF; ++i) {
        if ($i ~ /^hidden_id:/) {
          sub(/^hidden_id:/, "", $i);
          print $i;
          exit 0;
        }
      }
    }'
}

assert_special_hidden_id_present() {
  local tag="$1"
  local got
  got="$(hidden_id_for_special_tag "${tag}" | tr -d '[:space:]')"
  if [[ -z "${got}" || "${got}" == "-" || ! "${got}" =~ ^[0-9]+$ ]]; then
    echo "reload-multi-special-workspace: expected numeric hidden_id for special:${tag}, got '${got:-<empty>}'" >&2
    DISPLAY="${display}" "${zestctl_bin}" workspaces >&2 || true
    exit 1
  fi
}

assert_special_tag() {
  local win_hex="$1"
  local want="$2"
  local got
  got="$(special_tag_for_window "${win_hex}" | tr -d '[:space:]')"
  if [[ "${got}" != "${want}" ]]; then
    echo "reload-multi-special-workspace: expected special_tag=${want}, got '${got:-<empty>}' for ${win_hex}" >&2
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
  echo "reload-multi-special-workspace: zestwm did not respond on ${display}" >&2
  exit 2
fi

# Nested displays may already host unrelated clients (e.g. polkit agents); probe-class checks avoid that noise.
baseline_ws1_windows="$(probe_normal_workspace_client_count 1 | tr -d '[:space:]')"
baseline_ws2_windows="$(probe_normal_workspace_client_count 2 | tr -d '[:space:]')"
if [[ -z "${baseline_ws1_windows}" || ! "${baseline_ws1_windows}" =~ ^[0-9]+$ ]]; then
  baseline_ws1_windows="0"
fi
if [[ -z "${baseline_ws2_windows}" || ! "${baseline_ws2_windows}" =~ ^[0-9]+$ ]]; then
  baseline_ws2_windows="0"
fi

DISPLAY="${display}" "${probe_client_bin}" --title msprobe-reload-a --ready-file "${ready_a}" >/tmp/zestwm-reload-multi-special-a.log 2>&1 &
client_a_pid=$!
win_a_dec="$(read_ready_window "${ready_a}" || true)"
if [[ -z "${win_a_dec}" ]]; then
  echo "reload-multi-special-workspace: failed to read window id for client A" >&2
  exit 2
fi
win_a_hex="$(printf '0x%08x' "${win_a_dec}")"

DISPLAY="${display}" "${probe_client_bin}" --title msprobe-reload-b --ready-file "${ready_b}" >/tmp/zestwm-reload-multi-special-b.log 2>&1 &
client_b_pid=$!
win_b_dec="$(read_ready_window "${ready_b}" || true)"
if [[ -z "${win_b_dec}" ]]; then
  echo "reload-multi-special-workspace: failed to read window id for client B" >&2
  exit 2
fi
win_b_hex="$(printf '0x%08x' "${win_b_dec}")"

if ! wait_for_window_present "${win_a_hex}" || ! wait_for_window_present "${win_b_hex}"; then
  echo "reload-multi-special-workspace: clients not listed by zestctl clients" >&2
  exit 1
fi
assert_wm_alive
assert_special_tag "${win_a_hex}" "${tag_a}"
assert_special_tag "${win_b_hex}" "${tag_b}"
ws1_windows="$(probe_normal_workspace_client_count 1 | tr -d '[:space:]')"
if [[ "${ws1_windows}" != "${baseline_ws1_windows}" ]]; then
  echo "reload-multi-special-workspace: expected workspace 1 probe clients:${baseline_ws1_windows} while only special clients exist, got '${ws1_windows:-<empty>}'" >&2
  DISPLAY="${display}" "${zestctl_bin}" workspaces >&2 || true
  exit 1
fi

if ! DISPLAY="${display}" "${zestctl_bin}" workspaces | grep -q "special:${tag_a}"; then
  echo "reload-multi-special-workspace: zestctl workspaces missing special:${tag_a}" >&2
  exit 1
fi
if ! DISPLAY="${display}" "${zestctl_bin}" workspaces | grep -q "special:${tag_b}"; then
  echo "reload-multi-special-workspace: zestctl workspaces missing special:${tag_b}" >&2
  exit 1
fi
assert_special_hidden_id_present "${tag_a}"
assert_special_hidden_id_present "${tag_b}"

run_dispatch reload
assert_wm_alive
sleep 0.35
if ! wait_for_wm_ready; then
  echo "reload-multi-special-workspace: zestwm did not respond after reload" >&2
  exit 1
fi
assert_wm_alive

if ! wait_for_window_present "${win_a_hex}" || ! wait_for_window_present "${win_b_hex}"; then
  echo "reload-multi-special-workspace: clients missing after reload" >&2
  exit 1
fi
assert_special_tag "${win_a_hex}" "${tag_a}"
assert_special_tag "${win_b_hex}" "${tag_b}"
ws1_windows="$(probe_normal_workspace_client_count 1 | tr -d '[:space:]')"
if [[ "${ws1_windows}" != "${baseline_ws1_windows}" ]]; then
  echo "reload-multi-special-workspace: expected workspace 1 probe clients:${baseline_ws1_windows} after reload with only special clients, got '${ws1_windows:-<empty>}'" >&2
  DISPLAY="${display}" "${zestctl_bin}" workspaces >&2 || true
  exit 1
fi

if ! DISPLAY="${display}" "${zestctl_bin}" workspaces | grep -q "special:${tag_a}"; then
  echo "reload-multi-special-workspace: workspaces output missing special:${tag_a} after reload" >&2
  exit 1
fi
if ! DISPLAY="${display}" "${zestctl_bin}" workspaces | grep -q "special:${tag_b}"; then
  echo "reload-multi-special-workspace: workspaces output missing special:${tag_b} after reload" >&2
  exit 1
fi
assert_special_hidden_id_present "${tag_a}"
assert_special_hidden_id_present "${tag_b}"

# --- Group on special: overlay + groupmode + second client, then cyclegroup ---
run_dispatch special "${tag_a}"
sleep 0.2
run_dispatch focuswindow "${win_a_hex}"
if ! wait_for_active_window "${win_a_hex}"; then
  echo "reload-multi-special-workspace: cannot focus client A on special overlay" >&2
  exit 1
fi
DISPLAY="${display}" xdotool key alt+g
sleep 0.12

DISPLAY="${display}" "${probe_client_bin}" --title "${title_aa}" --ready-file "${ready_c}" >/tmp/zestwm-reload-multi-special-c.log 2>&1 &
client_c_pid=$!
win_c_dec="$(read_ready_window "${ready_c}" || true)"
if [[ -z "${win_c_dec}" ]]; then
  echo "reload-multi-special-workspace: failed to read window id for client C" >&2
  exit 2
fi
win_c_hex="$(printf '0x%08x' "${win_c_dec}")"
if ! wait_for_window_present "${win_c_hex}"; then
  echo "reload-multi-special-workspace: client C not listed" >&2
  exit 1
fi
assert_special_tag "${win_c_hex}" "${tag_a}"

run_dispatch focuswindow "${win_a_hex}"
if ! wait_for_active_window "${win_a_hex}"; then
  echo "reload-multi-special-workspace: cannot refocus A before cyclegroup" >&2
  exit 1
fi
DISPLAY="${display}" xdotool key alt+c
if ! wait_for_active_window "${win_c_hex}"; then
  echo "reload-multi-special-workspace: cyclegroup on special scratchpad did not activate client C" >&2
  DISPLAY="${display}" "${zestctl_bin}" clients >&2 || true
  exit 1
fi

# --- movetoworkspace: move B from special:tag_b to numeric desktop 2 ---
# B lives on a different special than the overlay we used for A/C; open tag_b so focus (and
# zestctl's 4-arg movetoworkspace pre-step) can reach it.
run_dispatch special "${tag_b}"
sleep 0.2
run_dispatch movetoworkspace 2 "${win_b_hex}"

got_ws="$(workspace_ws_for_window "${win_b_hex}" | tr -d '[:space:]')"
if [[ "${got_ws}" != "2" ]]; then
  echo "reload-multi-special-workspace: expected client B ws:2 after movetoworkspace, got '${got_ws:-}'" >&2
  DISPLAY="${display}" "${zestctl_bin}" clients >&2 || true
  exit 1
fi
got_sp="$(special_tag_for_window "${win_b_hex}" | tr -d '[:space:]')"
if [[ "${got_sp}" != "-" ]]; then
  echo "reload-multi-special-workspace: expected client B special_tag:- on normal desktop, got '${got_sp}'" >&2
  DISPLAY="${display}" "${zestctl_bin}" clients >&2 || true
  exit 1
fi
ws2_windows="$(probe_normal_workspace_client_count 2 | tr -d '[:space:]')"
expected_ws2_after_b="$((baseline_ws2_windows + 1))"
if [[ "${ws2_windows}" != "${expected_ws2_after_b}" ]]; then
  echo "reload-multi-special-workspace: expected workspace 2 probe clients:${expected_ws2_after_b} after moving B, got '${ws2_windows:-<empty>}'" >&2
  DISPLAY="${display}" "${zestctl_bin}" workspaces >&2 || true
  exit 1
fi

# --- Second reload: A+C stay on special tag_a; B stays on workspace 2 ---
run_dispatch reload
assert_wm_alive
sleep 0.35
if ! wait_for_wm_ready; then
  echo "reload-multi-special-workspace: zestwm did not respond after second reload" >&2
  exit 1
fi
assert_wm_alive

if ! wait_for_window_present "${win_a_hex}" || ! wait_for_window_present "${win_b_hex}" || ! wait_for_window_present "${win_c_hex}"; then
  echo "reload-multi-special-workspace: clients missing after second reload" >&2
  exit 1
fi
assert_special_tag "${win_a_hex}" "${tag_a}"
assert_special_tag "${win_c_hex}" "${tag_a}"
sleep 1.0
got_ws=""
for _ in {1..240}; do
  got_ws="$(workspace_ws_for_window "${win_b_hex}" | tr -d '[:space:]')"
  if [[ "${got_ws}" == "2" ]]; then
    break
  fi
  sleep 0.05
done
if [[ "${got_ws}" != "2" ]]; then
  echo "reload-multi-special-workspace: after second reload expected B ws:2, got '${got_ws:-}'" >&2
  DISPLAY="${display}" "${zestctl_bin}" clients >&2 || true
  exit 1
fi
got_sp="$(special_tag_for_window "${win_b_hex}" | tr -d '[:space:]')"
if [[ "${got_sp}" != "-" ]]; then
  echo "reload-multi-special-workspace: after second reload expected B special_tag:-, got '${got_sp}'" >&2
  exit 1
fi
ws1_windows="$(probe_normal_workspace_client_count 1 | tr -d '[:space:]')"
ws2_windows="$(probe_normal_workspace_client_count 2 | tr -d '[:space:]')"
if [[ "${ws1_windows}" != "${baseline_ws1_windows}" || "${ws2_windows}" != "${expected_ws2_after_b}" ]]; then
  echo "reload-multi-special-workspace: expected probe workspace counts ws1=${baseline_ws1_windows} ws2=${expected_ws2_after_b} after second reload, got ws1='${ws1_windows:-<empty>}' ws2='${ws2_windows:-<empty>}'" >&2
  DISPLAY="${display}" "${zestctl_bin}" workspaces >&2 || true
  DISPLAY="${display}" "${zestctl_bin}" clients >&2 || true
  exit 1
fi

echo "reload-multi-special-workspace: pass"
