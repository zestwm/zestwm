#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-}"
if [[ -z "${build_dir}" ]]; then
  echo "reload-persistence-special-hidden-id: missing build directory argument" >&2
  exit 2
fi

zestwm_bin="${build_dir}/zestwm"
zestctl_bin="${build_dir}/zestctl"
probe_client_bin="${build_dir}/focusurgent-urgent-client-test"
set_utf8_prop_bin="${build_dir}/set-utf8-root-property-test"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=tests/integration/xserver_common.inc.sh
source "${repo_root}/tests/integration/xserver_common.inc.sh"

base_conf_path="${repo_root}/examples/zestwm.conf"
probe_conf_path="${repo_root}/tests/integration/.reload-persistence-special-hidden-id.conf"
ready_file="${repo_root}/tests/integration/.reload-persistence-special-hidden-id.ready"
special_tag="persisthiddenidsp"
special_hidden_id="32"
title="persist-hidden-id-probe"

if [[ ! -x "${zestwm_bin}" || ! -x "${zestctl_bin}" || ! -x "${probe_client_bin}" || ! -x "${set_utf8_prop_bin}" ]]; then
  echo "reload-persistence-special-hidden-id: missing built binaries in ${build_dir}" >&2
  exit 2
fi
if [[ ! -f "${base_conf_path}" ]]; then
  echo "reload-persistence-special-hidden-id: missing config fixture ${base_conf_path}" >&2
  exit 2
fi

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "reload-persistence-special-hidden-id: missing required command '$1'" >&2
    exit 77
  fi
}

zestwm_require_nested_x11 "reload-persistence-special-hidden-id-probe"
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
  echo "reload-persistence-special-hidden-id: unable to allocate free X11 display" >&2
  rm -f "${probe_conf_path}"
  exit 2
fi

display=":${display_num}"
host_display="${ZESTWM_HOST_DISPLAY:-${DISPLAY:-}}"
if [[ -n "${host_display}" && "${display}" == "${host_display}" ]]; then
  echo "reload-persistence-special-hidden-id: refusing host display '${display}'" >&2
  exit 2
fi
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
    echo "reload-persistence-special-hidden-id: nested X server exited during startup (skipping on this host)" >&2
    exit 77
  fi
  if DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
  echo "reload-persistence-special-hidden-id: nested X server did not become ready (skipping on this host)" >&2
  exit 77
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

wait_for_wm_ready() {
  for _ in {1..120}; do
    if DISPLAY="${display}" "${zestctl_bin}" version >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
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

DISPLAY="${display}" "${probe_client_bin}" --title "${title}" --ready-file "${ready_file}" >/tmp/zestwm-reload-persistence-special-hidden-id-client.log 2>&1 &
client_pid=$!
win_dec="$(read_ready_window "${ready_file}" || true)"
if [[ -z "${win_dec}" ]]; then
  echo "reload-persistence-special-hidden-id: failed to read test client window id" >&2
  exit 2
fi
win_hex="$(printf '0x%08x' "${win_dec}")"

tree_entry="0:s${special_hidden_id}:G(0:0:${win_dec})"
DISPLAY="${display}" "${set_utf8_prop_bin}" _NET_ZEST_TREE_STATE "${tree_entry}" >/dev/null

DISPLAY="${display}" "${zestwm_bin}" -c "${probe_conf_path}" >/tmp/zestwm-reload-persistence-special-hidden-id.log 2>&1 &
wm_pid=$!
if ! wait_for_wm_ready; then
  echo "reload-persistence-special-hidden-id: zestwm did not respond on ${display}" >&2
  exit 2
fi

if ! wait_for_window_present "${win_hex}"; then
  echo "reload-persistence-special-hidden-id: client not listed by zestctl clients" >&2
  exit 1
fi

got_special="$(special_tag_for_window "${win_hex}" | tr -d '[:space:]')"
if [[ "${got_special}" != "${special_tag}" ]]; then
  echo "reload-persistence-special-hidden-id: expected special_tag:${special_tag} from s<hidden_id> tree key, got '${got_special:-<empty>}'" >&2
  DISPLAY="${display}" "${zestctl_bin}" clients >&2 || true
  exit 1
fi

echo "reload-persistence-special-hidden-id: pass"
