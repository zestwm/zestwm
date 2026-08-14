#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-}"
if [[ -z "${build_dir}" ]]; then
  echo "xdg-autostart: missing build directory argument" >&2
  exit 2
fi

zestwm_bin="${build_dir}/zestwm"
zestctl_bin="${build_dir}/zestctl"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=tests/integration/xserver_common.inc.sh
source "${repo_root}/tests/integration/xserver_common.inc.sh"

base_conf_path="${repo_root}/examples/zestwm.conf"
probe_conf_path="${repo_root}/tests/integration/.xdg-autostart.conf"

if [[ ! -x "${zestwm_bin}" || ! -x "${zestctl_bin}" ]]; then
  echo "xdg-autostart: missing built binaries in ${build_dir}" >&2
  exit 2
fi
if [[ ! -f "${base_conf_path}" ]]; then
  echo "xdg-autostart: missing config fixture ${base_conf_path}" >&2
  exit 2
fi

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "xdg-autostart: missing required command '$1'" >&2
    exit 77
  fi
}

zestwm_require_nested_x11 "xdg-autostart-probe"
need_cmd xdpyinfo

tmp_root="$(mktemp -d -t zestwm-xdg-autostart-XXXXXX)"
tmp_home="${tmp_root}/home"
autostart_dir="${tmp_home}/.config/autostart"
mkdir -p "${autostart_dir}"

run_marker="${tmp_root}/run.marker"
only_marker="${tmp_root}/only.marker"
sanitize_marker="${tmp_root}/sanitize.marker"
skip_marker="${tmp_root}/skip.marker"
bad_tryexec_marker="${tmp_root}/badtry.marker"
touch_marker="${tmp_root}/touch.target"

cat > "${autostart_dir}/run.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=RunMarker
Exec=/usr/bin/touch ${run_marker}
EOF

cat > "${autostart_dir}/onlyshowin.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=OnlyShowInMarker
OnlyShowIn=zestwm;
Exec=/usr/bin/touch ${only_marker}
EOF

cat > "${autostart_dir}/sanitize.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=SanitizeMarker
Exec=/usr/bin/touch ${sanitize_marker} %f
EOF

cat > "${autostart_dir}/hiddenskip.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=HiddenSkip
Hidden=true
Exec=/usr/bin/touch ${skip_marker}
EOF

cat > "${autostart_dir}/notshowin.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=NotShowInSkip
NotShowIn=zestwm;
Exec=/usr/bin/touch ${skip_marker}
EOF

cat > "${autostart_dir}/tryexecskip.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=TryExecSkip
TryExec=/definitely/not/present
Exec=/usr/bin/touch ${bad_tryexec_marker}
EOF

cat > "${autostart_dir}/quoted.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=QuoteSanitize
Exec="/usr/bin/touch" "${touch_marker}" %F
EOF

cp "${base_conf_path}" "${probe_conf_path}"

display_num=230
while [[ ${display_num} -lt 290 ]]; do
  if [[ ! -S "/tmp/.X11-unix/X${display_num}" ]]; then
    break
  fi
  display_num=$((display_num + 1))
done
if [[ ${display_num} -ge 290 ]]; then
  echo "xdg-autostart: unable to allocate free X11 display" >&2
  rm -rf "${tmp_root}"
  rm -f "${probe_conf_path}"
  exit 2
fi

display=":${display_num}"
xvfb_pid=""
wm_pid=""

cleanup() {
  if [[ -n "${wm_pid}" ]] && kill -0 "${wm_pid}" 2>/dev/null; then
    kill "${wm_pid}" 2>/dev/null || true
    wait "${wm_pid}" 2>/dev/null || true
  fi
  if [[ -n "${xvfb_pid}" ]] && kill -0 "${xvfb_pid}" 2>/dev/null; then
    kill "${xvfb_pid}" 2>/dev/null || true
    wait "${xvfb_pid}" 2>/dev/null || true
  fi
  rm -rf "${tmp_root}"
  rm -f "${probe_conf_path}"
}
trap cleanup EXIT

zestwm_start_nested_x11 "${display}"

for _ in {1..40}; do
  if ! kill -0 "${xvfb_pid}" 2>/dev/null; then
    echo "xdg-autostart: nested X server exited during startup (skipping on this host)" >&2
    exit 77
  fi
  if DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
  echo "xdg-autostart: nested X server did not become ready (skipping on this host)" >&2
  exit 77
fi

HOME="${tmp_home}" XDG_CURRENT_DESKTOP="zestwm" DISPLAY="${display}" \
  "${zestwm_bin}" -c "${probe_conf_path}" >/tmp/zestwm-xdg-autostart.log 2>&1 &
wm_pid=$!

wait_for_wm_ready() {
  for _ in {1..120}; do
    if HOME="${tmp_home}" XDG_CURRENT_DESKTOP="zestwm" DISPLAY="${display}" "${zestctl_bin}" version >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  return 1
}

wait_for_file() {
  local path="$1"
  for _ in {1..120}; do
    if [[ -f "${path}" ]]; then
      return 0
    fi
    sleep 0.1
  done
  return 1
}

if ! wait_for_wm_ready; then
  echo "xdg-autostart: zestwm did not become ready on ${display}" >&2
  exit 2
fi

if ! wait_for_file "${run_marker}"; then
  echo "xdg-autostart: expected run marker missing" >&2
  exit 1
fi
if ! wait_for_file "${only_marker}"; then
  echo "xdg-autostart: expected OnlyShowIn marker missing" >&2
  exit 1
fi
if ! wait_for_file "${sanitize_marker}"; then
  echo "xdg-autostart: expected sanitize marker missing" >&2
  exit 1
fi
if ! wait_for_file "${touch_marker}"; then
  echo "xdg-autostart: expected quoted command marker missing" >&2
  exit 1
fi

if [[ -f "${skip_marker}" ]]; then
  echo "xdg-autostart: hidden/notshowin entry should not run" >&2
  exit 1
fi
if [[ -f "${bad_tryexec_marker}" ]]; then
  echo "xdg-autostart: TryExec-gated entry should not run" >&2
  exit 1
fi

echo "xdg-autostart: pass"
