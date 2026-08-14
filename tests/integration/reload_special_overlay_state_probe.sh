#!/usr/bin/env bash
# MUST-RELOAD-011: closed special overlay stays closed across reload; normal-workspace
# clients are not pulled onto special when a non-silent special client is remapped.
set -euo pipefail

build_dir="${1:-}"
if [[ -z "${build_dir}" ]]; then
  echo "reload-special-overlay-state: missing build directory argument" >&2
  exit 2
fi

zestwm_bin="${build_dir}/zestwm"
zestctl_bin="${build_dir}/zestctl"
probe_client_bin="${build_dir}/focusurgent-urgent-client-test"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=tests/integration/xserver_common.inc.sh
source "${repo_root}/tests/integration/xserver_common.inc.sh"

base_conf_path="${repo_root}/examples/zestwm.conf"
probe_conf_path="${repo_root}/tests/integration/.reload-special-overlay-state.conf"
ready_ws1_a="${repo_root}/tests/integration/.reload-special-overlay-state.ws1-a.ready"
ready_ws1_b="${repo_root}/tests/integration/.reload-special-overlay-state.ws1-b.ready"
ready_drop="${repo_root}/tests/integration/.reload-special-overlay-state.drop.ready"
special_tag="dropdown"

if [[ ! -x "${zestwm_bin}" || ! -x "${zestctl_bin}" || ! -x "${probe_client_bin}" ]]; then
  echo "reload-special-overlay-state: missing built binaries in ${build_dir}" >&2
  exit 2
fi
if [[ ! -f "${base_conf_path}" ]]; then
  echo "reload-special-overlay-state: missing config fixture ${base_conf_path}" >&2
  exit 2
fi

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "reload-special-overlay-state: missing required command '$1'" >&2
    exit 77
  fi
}

zestwm_require_nested_x11 "reload-special-overlay-state-probe"
need_cmd xdpyinfo

cp "${base_conf_path}" "${probe_conf_path}"
cat >> "${probe_conf_path}" <<EOF
workspace = special:${special_tag}
window-rule {
  match app-id="RelOvWs1A"
  open-on-workspace = 1
}
window-rule {
  match app-id="RelOvWs1B"
  open-on-workspace = 1
}
window-rule {
  match app-id="RelOvDrop"
  open-on-workspace = special:${special_tag}
}
EOF

rm -f "${ready_ws1_a}" "${ready_ws1_b}" "${ready_drop}"

display_num=160
while [[ ${display_num} -lt 230 ]]; do
  if [[ ! -S "/tmp/.X11-unix/X${display_num}" ]]; then
    break
  fi
  display_num=$((display_num + 1))
done
if [[ ${display_num} -ge 230 ]]; then
  echo "reload-special-overlay-state: unable to allocate free X11 display" >&2
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
  rm -f "${probe_conf_path}" "${ready_ws1_a}" "${ready_ws1_b}" "${ready_drop}"
}
trap cleanup EXIT

zestwm_start_nested_x11 "${display}"

DISPLAY="${display}" "${zestwm_bin}" -c "${probe_conf_path}" >/tmp/zestwm-reload-special-overlay-state.log 2>&1 &
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
    echo "reload-special-overlay-state: zestwm crashed unexpectedly" >&2
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
    --ready-file "${ready}" >/tmp/zestwm-reload-special-overlay-state-"${instance}".log 2>&1 &
  pids+=("$!")
  local win_dec
  win_dec="$(read_ready_window "${ready}" || true)"
  if [[ -z "${win_dec}" ]]; then
    echo "reload-special-overlay-state: failed to read ready window for ${instance}" >&2
    exit 2
  fi
  printf '0x%08x' "${win_dec}"
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

wait_special_visible() {
  local tag="$1"
  local want="$2"
  for _ in {1..120}; do
    if [[ "$(special_visible_for_tag "${tag}" | tr -d '[:space:]')" == "${want}" ]]; then
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
    echo "reload-special-overlay-state: expected ${win_hex} ws:${want_ws} special_tag:${want_tag}, got ws:${got_ws:-<empty>} special_tag:${got_tag:-<empty>}" >&2
    DISPLAY="${display}" "${zestctl_bin}" clients >&2 || true
    exit 1
  fi
}

if ! wait_for_wm_ready; then
  echo "reload-special-overlay-state: zestwm did not respond on ${display}" >&2
  exit 2
fi

# Normal workspace clients (telegram/signal-like pair on ws1).
win_ws1_a_hex="$(spawn_client "relov-ws1-a" "RelOvWs1A" "relov-ws1-a" "${ready_ws1_a}")"
if ! wait_for_window_present "${win_ws1_a_hex}"; then
  echo "reload-special-overlay-state: ws1-a not listed" >&2
  exit 1
fi
win_ws1_b_hex="$(spawn_client "relov-ws1-b" "RelOvWs1B" "relov-ws1-b" "${ready_ws1_b}")"
if ! wait_for_window_present "${win_ws1_b_hex}"; then
  echo "reload-special-overlay-state: ws1-b not listed" >&2
  exit 1
fi

# Non-silent special:dropdown client (opens overlay on map).
win_drop_hex="$(spawn_client "relov-drop" "RelOvDrop" "relov-drop" "${ready_drop}")"
if ! wait_for_window_present "${win_drop_hex}"; then
  echo "reload-special-overlay-state: dropdown client not listed" >&2
  exit 1
fi
if ! wait_special_visible "${special_tag}" "yes"; then
  echo "reload-special-overlay-state: expected special:${special_tag} visible after non-silent map" >&2
  DISPLAY="${display}" "${zestctl_bin}" workspaces >&2 || true
  exit 1
fi

# Close overlay, then reload while it is closed.
run_dispatch special "${special_tag}"
if ! wait_special_visible "${special_tag}" "no"; then
  echo "reload-special-overlay-state: expected special:${special_tag} closed before reload" >&2
  DISPLAY="${display}" "${zestctl_bin}" workspaces >&2 || true
  exit 1
fi

assert_client_location "${win_ws1_a_hex}" "1" "-"
assert_client_location "${win_ws1_b_hex}" "1" "-"
assert_client_location "${win_drop_hex}" "0" "${special_tag}"

run_dispatch reload
sleep 0.35
if ! wait_for_wm_ready; then
  echo "reload-special-overlay-state: zestwm did not respond after reload" >&2
  exit 1
fi
assert_wm_alive

for win in "${win_ws1_a_hex}" "${win_ws1_b_hex}" "${win_drop_hex}"; do
  if ! wait_for_window_present "${win}"; then
    echo "reload-special-overlay-state: missing window after reload ${win}" >&2
    DISPLAY="${display}" "${zestctl_bin}" clients >&2 || true
    exit 1
  fi
done

if ! wait_special_visible "${special_tag}" "no"; then
  echo "reload-special-overlay-state: special:${special_tag} reopened after reload (expected closed)" >&2
  DISPLAY="${display}" "${zestctl_bin}" workspaces >&2 || true
  exit 1
fi

assert_client_location "${win_ws1_a_hex}" "1" "-"
assert_client_location "${win_ws1_b_hex}" "1" "-"
assert_client_location "${win_drop_hex}" "0" "${special_tag}"

# Open path still works after closed-reload restore; open state must also survive reload.
run_dispatch special "${special_tag}"
if ! wait_special_visible "${special_tag}" "yes"; then
  echo "reload-special-overlay-state: cannot reopen special:${special_tag} after reload" >&2
  DISPLAY="${display}" "${zestctl_bin}" workspaces >&2 || true
  exit 1
fi
assert_client_location "${win_ws1_a_hex}" "1" "-"
assert_client_location "${win_ws1_b_hex}" "1" "-"
assert_client_location "${win_drop_hex}" "0" "${special_tag}"

run_dispatch reload
sleep 0.35
if ! wait_for_wm_ready; then
  echo "reload-special-overlay-state: zestwm did not respond after second reload" >&2
  exit 1
fi
assert_wm_alive
if ! wait_special_visible "${special_tag}" "yes"; then
  echo "reload-special-overlay-state: special:${special_tag} did not stay open across reload" >&2
  DISPLAY="${display}" "${zestctl_bin}" workspaces >&2 || true
  exit 1
fi
assert_client_location "${win_ws1_a_hex}" "1" "-"
assert_client_location "${win_ws1_b_hex}" "1" "-"
assert_client_location "${win_drop_hex}" "0" "${special_tag}"

echo "reload-special-overlay-state: pass"
