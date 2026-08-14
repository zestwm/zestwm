#!/usr/bin/env bash
# Verifies per-workspace gaps and border rules apply at tile time (not just parse/registry).
set -euo pipefail

build_dir="${1:-}"
if [[ -z "${build_dir}" ]]; then
  echo "workspace-rules-runtime: missing build directory argument" >&2
  exit 2
fi

zestwm_bin="${build_dir}/zestwm"
zestctl_bin="${build_dir}/zestctl"
probe_client_bin="${build_dir}/focusurgent-urgent-client-test"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=tests/integration/xserver_common.inc.sh
source "${repo_root}/tests/integration/xserver_common.inc.sh"

base_conf_path="${repo_root}/examples/zestwm.conf"
probe_conf_path="${repo_root}/tests/integration/.workspace-rules-runtime.conf"
ready_file="${repo_root}/tests/integration/.workspace-rules-runtime.ready"

if [[ ! -x "${zestwm_bin}" || ! -x "${zestctl_bin}" || ! -x "${probe_client_bin}" ]]; then
  echo "workspace-rules-runtime: missing built binaries in ${build_dir}" >&2
  exit 2
fi
if [[ ! -f "${base_conf_path}" ]]; then
  echo "workspace-rules-runtime: missing config fixture ${base_conf_path}" >&2
  exit 2
fi

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "workspace-rules-runtime: missing required command '$1'" >&2
    exit 77
  fi
}

zestwm_require_nested_x11 "workspace-rules-runtime-probe"
need_cmd xdpyinfo
need_cmd xwininfo

cp "${base_conf_path}" "${probe_conf_path}"
cat >> "${probe_conf_path}" <<'EOF'
workspace = 6, gaprules, gapsin:40, gapsout:40
workspace = 7, noborder, bordersize:0, border:false
EOF
rm -f "${ready_file}"

display_num=212
while [[ ${display_num} -lt 232 ]]; do
  if [[ ! -S "/tmp/.X11-unix/X${display_num}" ]]; then
    break
  fi
  display_num=$((display_num + 1))
done
if [[ ${display_num} -ge 232 ]]; then
  echo "workspace-rules-runtime: unable to allocate free X11 display" >&2
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
    echo "workspace-rules-runtime: nested X server exited during startup (skipping on this host)" >&2
    exit 77
  fi
  if DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
  echo "workspace-rules-runtime: nested X server did not become ready (skipping on this host)" >&2
  exit 77
fi

DISPLAY="${display}" "${zestwm_bin}" -c "${probe_conf_path}" >/tmp/zestwm-workspace-rules-runtime.log 2>&1 &
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

workspace_for_window() {
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

wait_for_window_workspace() {
  local win_hex="$1"
  local expected="$2"
  for _ in {1..120}; do
    local got
    got="$(workspace_for_window "${win_hex}" | tr -d '[:space:]')"
    if [[ "${got}" == "${expected}" ]]; then
      return 0
    fi
    sleep 0.05
  done
  return 1
}

wait_for_active_workspace() {
  local want="$1"
  for _ in {1..120}; do
    local line got
    line="$(DISPLAY="${display}" "${zestctl_bin}" -j activeworkspace 2>/dev/null || true)"
    if [[ "${line}" =~ \"id\":([0-9]+) ]]; then
      got="${BASH_REMATCH[1]}"
      if [[ "${got}" == "${want}" ]]; then
        return 0
      fi
    fi
    sleep 0.05
  done
  return 1
}

move_window_to_workspace() {
  local workspace="$1"
  local win_hex="$2"
  for _ in {1..12}; do
    run_dispatch movetoworkspace "${workspace}" "${win_hex}"
    if wait_for_window_workspace "${win_hex}" "${workspace}"; then
      return 0
    fi
    sleep 0.05
  done
  return 1
}

read_window_geometry() {
  local win_hex="$1"
  local info
  info="$(DISPLAY="${display}" xwininfo -id "${win_hex}" 2>/dev/null || true)"
  if [[ -z "${info}" ]]; then
    return 1
  fi
  GEO_X="$(printf '%s\n' "${info}" | awk '/Absolute upper-left X:/ {print $4; exit}')"
  GEO_W="$(printf '%s\n' "${info}" | awk '/Width:/ {print $2; exit}')"
  GEO_BORDER="$(printf '%s\n' "${info}" | awk '/Border width:/ {print $NF; exit}')"
  [[ -n "${GEO_X}" && -n "${GEO_W}" && -n "${GEO_BORDER}" ]]
}

wait_for_geometry_predicate() {
  local win_hex="$1"
  local predicate="$2"
  for _ in {1..120}; do
    if read_window_geometry "${win_hex}" && eval "${predicate}"; then
      return 0
    fi
    sleep 0.05
  done
  return 1
}

if ! wait_for_wm_ready; then
  echo "workspace-rules-runtime: zestwm did not respond on ${display}" >&2
  exit 2
fi

run_dispatch workspace 1

DISPLAY="${display}" "${probe_client_bin}" --title "workspace-rules-runtime" --ready-file "${ready_file}" >/tmp/zestwm-workspace-rules-runtime-client.log 2>&1 &
client_pid=$!

win_dec="$(read_ready_window "${ready_file}" || true)"
if [[ -z "${win_dec}" ]]; then
  echo "workspace-rules-runtime: failed to capture probe window id" >&2
  exit 2
fi
win_hex="$(printf '0x%08x' "${win_dec}")"

if ! wait_for_window_workspace "${win_hex}" "1"; then
  echo "workspace-rules-runtime: probe client not on workspace 1" >&2
  DISPLAY="${display}" "${zestctl_bin}" clients >&2 || true
  exit 1
fi

if ! wait_for_geometry_predicate "${win_hex}" '(( GEO_BORDER == 1 ))'; then
  echo "workspace-rules-runtime: expected default border width 1 on workspace 1 (got ${GEO_BORDER:-?})" >&2
  DISPLAY="${display}" xwininfo -id "${win_hex}" >&2 || true
  exit 1
fi

if ! wait_for_geometry_predicate "${win_hex}" '(( GEO_X <= 10 && GEO_W >= 1250 ))'; then
  echo "workspace-rules-runtime: baseline geometry on workspace 1 looks wrong (x=${GEO_X:-?}, w=${GEO_W:-?})" >&2
  DISPLAY="${display}" xwininfo -id "${win_hex}" >&2 || true
  exit 1
fi
baseline_x="${GEO_X}"
baseline_w="${GEO_W}"

if ! move_window_to_workspace "6" "${win_hex}"; then
  echo "workspace-rules-runtime: failed to move client to workspace 6" >&2
  exit 1
fi
run_dispatch workspace 6
if ! wait_for_active_workspace "6"; then
  echo "workspace-rules-runtime: failed to view workspace 6" >&2
  exit 1
fi

if ! wait_for_geometry_predicate "${win_hex}" "(( GEO_X >= baseline_x + 35 && GEO_W <= baseline_w - 70 ))"; then
  echo "workspace-rules-runtime: gapsout/gapsin not applied on workspace 6 (x=${GEO_X:-?}, w=${GEO_W:-?}, baseline x=${baseline_x} w=${baseline_w})" >&2
  DISPLAY="${display}" xwininfo -id "${win_hex}" >&2 || true
  exit 1
fi

if ! move_window_to_workspace "7" "${win_hex}"; then
  echo "workspace-rules-runtime: failed to move client to workspace 7" >&2
  exit 1
fi
run_dispatch workspace 7
if ! wait_for_active_workspace "7"; then
  echo "workspace-rules-runtime: failed to view workspace 7" >&2
  exit 1
fi

if ! wait_for_geometry_predicate "${win_hex}" '(( GEO_BORDER == 0 ))'; then
  echo "workspace-rules-runtime: border:false/bordersize:0 not applied on workspace 7 (got border=${GEO_BORDER:-?})" >&2
  DISPLAY="${display}" xwininfo -id "${win_hex}" >&2 || true
  exit 1
fi

echo "workspace-rules-runtime: pass"
