#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-}"
if [[ -z "${build_dir}" ]]; then
  echo "group-single-client: missing build directory argument" >&2
  exit 2
fi

zestwm_bin="${build_dir}/zestwm"
zestctl_bin="${build_dir}/zestctl"
probe_client_bin="${build_dir}/focusurgent-urgent-client-test"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=tests/integration/xserver_common.inc.sh
source "${repo_root}/tests/integration/xserver_common.inc.sh"

base_conf_path="${repo_root}/examples/zestwm.conf"
probe_conf_path="${repo_root}/tests/integration/.group-single-client.conf"
ready_a="${repo_root}/tests/integration/.group-single-a.ready"
ready_b="${repo_root}/tests/integration/.group-single-b.ready"

if [[ ! -x "${zestwm_bin}" || ! -x "${zestctl_bin}" || ! -x "${probe_client_bin}" ]]; then
  echo "group-single-client: missing built binaries in ${build_dir}" >&2
  exit 2
fi
if [[ ! -f "${base_conf_path}" ]]; then
  echo "group-single-client: missing config fixture ${base_conf_path}" >&2
  exit 2
fi

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "group-single-client: missing required command '$1'" >&2
    exit 77
  fi
}

zestwm_require_nested_x11 "group-single-client-probe"
need_cmd xdpyinfo
need_cmd xdotool

cp "${base_conf_path}" "${probe_conf_path}"
cat >> "${probe_conf_path}" <<'EOF'
binds {
  ALT+g { groupmode -1; }
  ALT+c { cyclegroup 1; }
}
EOF
rm -f "${ready_a}" "${ready_b}"

display_num=130
while [[ ${display_num} -lt 150 ]]; do
  if [[ ! -S "/tmp/.X11-unix/X${display_num}" ]]; then
    break
  fi
  display_num=$((display_num + 1))
done
if [[ ${display_num} -ge 150 ]]; then
  echo "group-single-client: unable to allocate free X11 display" >&2
  rm -f "${probe_conf_path}"
  exit 2
fi

display=":${display_num}"
xvfb_pid=""
wm_pid=""
client_a_pid=""
client_b_pid=""

cleanup() {
  for pid in "${client_a_pid}" "${client_b_pid}"; do
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
  rm -f "${probe_conf_path}" "${ready_a}" "${ready_b}"
}
trap cleanup EXIT

zestwm_start_nested_x11 "${display}"

for _ in {1..40}; do
  if ! kill -0 "${xvfb_pid}" 2>/dev/null; then
    echo "group-single-client: nested X server exited during startup (skipping on this host)" >&2
    exit 77
  fi
  if DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
  echo "group-single-client: nested X server did not become ready (skipping on this host)" >&2
  exit 77
fi

DISPLAY="${display}" "${zestwm_bin}" -c "${probe_conf_path}" >/tmp/zestwm-group-single-client.log 2>&1 &
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
    echo "group-single-client: zestwm crashed unexpectedly" >&2
    exit 1
  fi
}

run_dispatch() {
  DISPLAY="${display}" "${zestctl_bin}" dispatch "$@" >/dev/null
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

assert_groupbar_visible() {
  local bar_win
  local geom
  local gx gy gw gh

  bar_win="$(DISPLAY="${display}" xdotool search --class zestwm-tab 2>/dev/null | awk 'NR==1 { print $1 }')"
  if [[ -z "${bar_win}" ]]; then
    echo "group-single-client: groupbar window not found after enabling groupmode" >&2
    exit 1
  fi
  geom="$(DISPLAY="${display}" xdotool getwindowgeometry --shell "${bar_win}" 2>/dev/null || true)"
  gx="$(awk -F= '/^X=/{print $2}' <<< "${geom}")"
  gy="$(awk -F= '/^Y=/{print $2}' <<< "${geom}")"
  gw="$(awk -F= '/^WIDTH=/{print $2}' <<< "${geom}")"
  gh="$(awk -F= '/^HEIGHT=/{print $2}' <<< "${geom}")"
  if [[ -z "${gx}" || -z "${gy}" || -z "${gw}" || -z "${gh}" ]]; then
    echo "group-single-client: failed to read groupbar geometry" >&2
    echo "${geom}" >&2
    exit 1
  fi
  if (( gw <= 0 || gh <= 0 || gx < 0 || gy < 0 )); then
    echo "group-single-client: groupbar hidden after single-client group toggle (x=${gx} y=${gy} w=${gw} h=${gh})" >&2
    exit 1
  fi
}

if ! wait_for_wm_ready; then
  echo "group-single-client: zestwm did not respond on ${display}" >&2
  exit 2
fi

run_dispatch workspace 1

DISPLAY="${display}" "${probe_client_bin}" --title "group-single-a" --ready-file "${ready_a}" >/tmp/zestwm-group-single-a.log 2>&1 &
client_a_pid=$!
win_a_dec="$(read_ready_window "${ready_a}" || true)"
if [[ -z "${win_a_dec}" ]]; then
  echo "group-single-client: failed to capture first client window id" >&2
  exit 2
fi
win_a_hex="$(printf '0x%08x' "${win_a_dec}")"

run_dispatch focuswindow "${win_a_hex}"
if ! wait_for_active_window "${win_a_hex}"; then
  echo "group-single-client: cannot focus first client before groupmode toggle" >&2
  exit 1
fi

# Enable grouped mode while only one client exists in workspace 1.
DISPLAY="${display}" xdotool key alt+g
sleep 0.1

# Single-client group must be observable as active group intent.
if ! DISPLAY="${display}" "${zestctl_bin}" clients | awk -v w="${win_a_hex}" '
  BEGIN { found = 0; ok = 0; }
  $1 == "win:" w {
    found = 1;
    grouped = "";
    groupmode = "";
    gsize = "";
    for (i = 1; i <= NF; ++i) {
      if ($i ~ /^grouped:/) {
        grouped = $i;
        sub(/^grouped:/, "", grouped);
      } else if ($i ~ /^groupmode:/) {
        groupmode = $i;
        sub(/^groupmode:/, "", groupmode);
      } else if ($i ~ /^group_size:/) {
        gsize = $i;
        sub(/^group_size:/, "", gsize);
      }
    }
    ok = (grouped == "yes" && groupmode == "yes" && gsize == "1");
    exit;
  }
  END { exit(found && ok ? 0 : 1) }'
then
  echo "group-single-client: expected grouped:yes groupmode:yes group_size:1 after enabling groupmode with one client" >&2
  DISPLAY="${display}" "${zestctl_bin}" clients >&2 || true
  exit 1
fi
assert_groupbar_visible

DISPLAY="${display}" "${probe_client_bin}" --title "group-single-b" --ready-file "${ready_b}" >/tmp/zestwm-group-single-b.log 2>&1 &
client_b_pid=$!
win_b_dec="$(read_ready_window "${ready_b}" || true)"
if [[ -z "${win_b_dec}" ]]; then
  echo "group-single-client: failed to capture second client window id" >&2
  exit 2
fi
win_b_hex="$(printf '0x%08x' "${win_b_dec}")"

# If second client joined the grouped leaf, cyclegroup must switch active tab to it.
run_dispatch focuswindow "${win_a_hex}"
if ! wait_for_active_window "${win_a_hex}"; then
  echo "group-single-client: cannot refocus first client before cyclegroup" >&2
  exit 1
fi

DISPLAY="${display}" xdotool key alt+c
if ! wait_for_active_window "${win_b_hex}"; then
  echo "group-single-client: second client did not become active via cyclegroup (single-client grouping regression)" >&2
  exit 1
fi

echo "group-single-client: pass"
