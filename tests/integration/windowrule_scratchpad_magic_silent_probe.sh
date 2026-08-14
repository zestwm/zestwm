#!/usr/bin/env bash
# Silent `open-on-workspace = special:magic`: ownership survives a second map (BSP split handoff)
# and both clients keep `special_tag:magic` with overlay closed then open.
set -euo pipefail

build_dir="${1:-}"
if [[ -z "${build_dir}" ]]; then
  echo "windowrule-scratchpad-magic-silent: missing build directory argument" >&2
  exit 2
fi

zestwm_bin="${build_dir}/zestwm"
zestctl_bin="${build_dir}/zestctl"
probe_client_bin="${build_dir}/focusurgent-urgent-client-test"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=tests/integration/xserver_common.inc.sh
source "${repo_root}/tests/integration/xserver_common.inc.sh"

base_conf_path="${repo_root}/examples/zestwm.conf"
probe_conf_path="${repo_root}/tests/integration/.windowrule-scratchpad-magic-silent.conf"
ready_file="${repo_root}/tests/integration/.windowrule-scratchpad-magic-silent.ready"

if [[ ! -x "${zestwm_bin}" || ! -x "${zestctl_bin}" || ! -x "${probe_client_bin}" ]]; then
  echo "windowrule-scratchpad-magic-silent: missing built binaries in ${build_dir}" >&2
  exit 2
fi

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "windowrule-scratchpad-magic-silent: missing required command '$1'" >&2
    exit 77
  fi
}

zestwm_require_nested_x11 "windowrule-scratchpad-magic-silent-probe"
need_cmd xdpyinfo

cp "${base_conf_path}" "${probe_conf_path}"
cat >> "${probe_conf_path}" <<'EOF'
workspace = special:magic
window-rule {
  match app-id="(ScratchMagicA|ScratchMagicB)"
  open-on-workspace = special:magic silent
}
EOF
rm -f "${ready_file}"

display_num=90
while [[ ${display_num} -lt 250 ]]; do
  if [[ ! -S "/tmp/.X11-unix/X${display_num}" ]]; then
    break
  fi
  display_num=$((display_num + 1))
done
if [[ ${display_num} -ge 250 ]]; then
  echo "windowrule-scratchpad-magic-silent: unable to allocate free X11 display" >&2
  rm -f "${probe_conf_path}"
  exit 2
fi

display=":${display_num}"
xvfb_pid=""
wm_pid=""
client_pids=()

cleanup() {
  for pid in "${client_pids[@]:-}"; do
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
  rm -f "${probe_conf_path}" "${ready_file}"
}
trap cleanup EXIT

zestwm_start_nested_x11 "${display}"

for _ in {1..40}; do
  if ! kill -0 "${xvfb_pid}" 2>/dev/null; then
    echo "windowrule-scratchpad-magic-silent: nested X server exited during startup (skipping on this host)" >&2
    exit 77
  fi
  if DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
  echo "windowrule-scratchpad-magic-silent: nested X server did not become ready (skipping on this host)" >&2
  exit 77
fi

DISPLAY="${display}" "${zestwm_bin}" -c "${probe_conf_path}" >/tmp/zestwm-windowrule-scratchpad-magic-silent.log 2>&1 &
wm_pid=$!

for _ in {1..120}; do
  if DISPLAY="${display}" "${zestctl_bin}" version >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${display}" "${zestctl_bin}" version >/dev/null 2>&1; then
  echo "windowrule-scratchpad-magic-silent: zestwm did not respond on ${display}" >&2
  exit 2
fi

map_client() {
  local label="$1"
  local class="$2"
  local inst="$3"
  rm -f "${ready_file}"
  DISPLAY="${display}" "${probe_client_bin}" \
    --title "scratchpad-magic-${label}" \
    --wm-class "${class}" \
    --wm-instance "${inst}" \
    --ready-file "${ready_file}" >/tmp/zestwm-windowrule-scratchpad-magic-silent-client-${label}.log 2>&1 &
  client_pids+=("$!")

  local win_dec=""
  for _ in {1..120}; do
    if [[ -s "${ready_file}" ]]; then
      win_dec="$(tr -d '[:space:]' < "${ready_file}")"
      if [[ "${win_dec}" =~ ^[0-9]+$ ]]; then
        break
      fi
    fi
    sleep 0.05
  done
  if [[ -z "${win_dec}" ]] || ! [[ "${win_dec}" =~ ^[0-9]+$ ]]; then
    echo "windowrule-scratchpad-magic-silent: failed to read window id (${label})" >&2
    return 1
  fi
  printf '0x%08x' "${win_dec}"
}

wait_client_line() {
  local win_hex="$1"
  for _ in {1..120}; do
    if DISPLAY="${display}" "${zestctl_bin}" clients | grep -Fq "win:${win_hex}"; then
      return 0
    fi
    sleep 0.05
  done
  return 1
}

assert_special_tag() {
  local win_hex="$1"
  local line
  line="$(DISPLAY="${display}" "${zestctl_bin}" clients | grep -F "win:${win_hex}" || true)"
  if [[ -z "${line}" ]]; then
    echo "windowrule-scratchpad-magic-silent: missing client ${win_hex}" >&2
    DISPLAY="${display}" "${zestctl_bin}" clients >&2 || true
    return 1
  fi
  if ! grep -q 'special_tag:magic' <<<"${line}"; then
    echo "windowrule-scratchpad-magic-silent: expected special_tag:magic for ${win_hex}" >&2
    echo "${line}" >&2
    DISPLAY="${display}" "${zestctl_bin}" clients >&2 || true
    return 1
  fi
  if ! grep -q 'floating:no' <<<"${line}"; then
    echo "windowrule-scratchpad-magic-silent: expected floating:no for ${win_hex}" >&2
    echo "${line}" >&2
    return 1
  fi
}

win_a="$(map_client branch-a ScratchMagicA scratch-magic-a)"
if ! wait_client_line "${win_a}"; then
  echo "windowrule-scratchpad-magic-silent: client A not listed" >&2
  exit 1
fi
assert_special_tag "${win_a}"

win_b="$(map_client branch-b ScratchMagicB scratch-magic-b)"
if ! wait_client_line "${win_b}"; then
  echo "windowrule-scratchpad-magic-silent: client B not listed" >&2
  exit 1
fi
# Second silent map must not destroy the special BSP (regression: handoff dropped taken root).
assert_special_tag "${win_a}"
assert_special_tag "${win_b}"

if ! DISPLAY="${display}" "${zestctl_bin}" dispatch special magic; then
  echo "windowrule-scratchpad-magic-silent: dispatch special magic failed" >&2
  exit 1
fi
sleep 0.35

assert_special_tag "${win_a}"
assert_special_tag "${win_b}"

if ! DISPLAY="${display}" "${zestctl_bin}" workspaces | grep -E 'name:special:magic' | grep -q 'windows:2'; then
  echo "windowrule-scratchpad-magic-silent: expected special:magic windows:2 after overlay open" >&2
  DISPLAY="${display}" "${zestctl_bin}" workspaces >&2 || true
  exit 1
fi

if ! DISPLAY="${display}" "${zestctl_bin}" clients | grep -E "win:(${win_a}|${win_b})" | grep -q 'focused:yes'; then
  echo "windowrule-scratchpad-magic-silent: expected a special client focused after overlay open" >&2
  DISPLAY="${display}" "${zestctl_bin}" clients >&2 || true
  exit 1
fi

echo "windowrule-scratchpad-magic-silent: pass"
