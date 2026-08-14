#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-}"
if [[ -z "${build_dir}" ]]; then
  echo "dispatch-special-aliases: missing build directory argument" >&2
  exit 2
fi

zestwm_bin="${build_dir}/zestwm"
zestctl_bin="${build_dir}/zestctl"
probe_client_bin="${build_dir}/focusurgent-urgent-client-test"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=tests/integration/xserver_common.inc.sh
source "${repo_root}/tests/integration/xserver_common.inc.sh"

base_conf_path="${repo_root}/examples/zestwm.conf"
probe_conf_path="${repo_root}/tests/integration/.dispatch-special-aliases.conf"
ready_file="${repo_root}/tests/integration/.dispatch-special-aliases.ready"
special_tag="ipcalias"
undeclared_tag="ipcalias-undeclared"
title="ipc-special-alias-client"

if [[ ! -x "${zestwm_bin}" || ! -x "${zestctl_bin}" || ! -x "${probe_client_bin}" ]]; then
  echo "dispatch-special-aliases: missing built binaries in ${build_dir}" >&2
  exit 2
fi
if [[ ! -f "${base_conf_path}" ]]; then
  echo "dispatch-special-aliases: missing config fixture ${base_conf_path}" >&2
  exit 2
fi

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "dispatch-special-aliases: missing required command '$1'" >&2
    exit 77
  fi
}

zestwm_require_nested_x11 "dispatch-special-aliases-probe"
need_cmd xdpyinfo

cp "${base_conf_path}" "${probe_conf_path}"
cat >> "${probe_conf_path}" <<EOF
workspace = special:${special_tag}
EOF
rm -f "${ready_file}"

display_num=90
while [[ ${display_num} -lt 110 ]]; do
  if [[ ! -S "/tmp/.X11-unix/X${display_num}" ]]; then
    break
  fi
  display_num=$((display_num + 1))
done
if [[ ${display_num} -ge 110 ]]; then
  echo "dispatch-special-aliases: unable to allocate free X11 display" >&2
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
    echo "dispatch-special-aliases: nested X server exited during startup (skipping on this host)" >&2
    exit 77
  fi
  if DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
  echo "dispatch-special-aliases: nested X server did not become ready (skipping on this host)" >&2
  exit 77
fi

DISPLAY="${display}" "${zestwm_bin}" -c "${probe_conf_path}" >/tmp/zestwm-dispatch-special-aliases.log 2>&1 &
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

wait_for_special_tag() {
  local win_hex="$1"
  local want="$2"
  for _ in {1..120}; do
    local got
    got="$(special_tag_for_window "${win_hex}" | tr -d '[:space:]')"
    if [[ "${got}" == "${want}" ]]; then
      return 0
    fi
    sleep 0.05
  done
  return 1
}

assert_dispatch_fails_for_undeclared() {
  local command="$1"
  if DISPLAY="${display}" "${zestctl_bin}" dispatch "${command}" "special:${undeclared_tag}" >/dev/null 2>&1; then
    echo "dispatch-special-aliases: expected '${command} special:${undeclared_tag}' to fail for undeclared special tag" >&2
    exit 1
  fi
}

if ! wait_for_wm_ready; then
  echo "dispatch-special-aliases: zestwm did not respond on ${display}" >&2
  exit 2
fi

DISPLAY="${display}" "${probe_client_bin}" --title "${title}" --ready-file "${ready_file}" >/tmp/zestwm-dispatch-special-aliases-client.log 2>&1 &
client_pid=$!
win_dec="$(read_ready_window "${ready_file}" || true)"
if [[ -z "${win_dec}" ]]; then
  echo "dispatch-special-aliases: failed to read test client window id" >&2
  exit 2
fi
win_hex="$(printf '0x%08x' "${win_dec}")"

if ! wait_for_window_present "${win_hex}"; then
  echo "dispatch-special-aliases: client not listed by zestctl clients" >&2
  exit 1
fi

if ! DISPLAY="${display}" "${zestctl_bin}" dispatch movetoworkspace "special:${special_tag}" "${win_hex}" >/dev/null; then
  echo "dispatch-special-aliases: movetoworkspace special dispatch failed" >&2
  exit 1
fi
if ! wait_for_special_tag "${win_hex}" "${special_tag}"; then
  got_sp="$(special_tag_for_window "${win_hex}" | tr -d '[:space:]')"
  echo "dispatch-special-aliases: expected special_tag:${special_tag} after move, got '${got_sp:-<empty>}'" >&2
  DISPLAY="${display}" "${zestctl_bin}" clients >&2 || true
  exit 1
fi

if ! DISPLAY="${display}" "${zestctl_bin}" dispatch workspace "special:${special_tag}" >/dev/null; then
  echo "dispatch-special-aliases: workspace special dispatch failed" >&2
  exit 1
fi
if ! DISPLAY="${display}" "${zestctl_bin}" dispatch view "special:${special_tag}" >/dev/null; then
  echo "dispatch-special-aliases: view special dispatch failed" >&2
  exit 1
fi

assert_dispatch_fails_for_undeclared workspace
assert_dispatch_fails_for_undeclared view

if ! DISPLAY="${display}" "${zestctl_bin}" workspaces | awk -v tag="name:special:${special_tag}" '$0 ~ tag { found=1 } END { exit(found ? 0 : 1) }'; then
  echo "dispatch-special-aliases: workspaces output missing declared special row after alias dispatches" >&2
  DISPLAY="${display}" "${zestctl_bin}" workspaces >&2 || true
  exit 1
fi
echo "dispatch-special-aliases: pass"
