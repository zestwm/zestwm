# Shared nested X11 launcher for integration probes.
# Default: Xvfb first (probes tuned for it; some flows differ under Xephyr).
# Set ZESTWM_PREFER_XEPHYR=1 (run_xephyr_suite.sh does this) to prefer Xephyr when installed.
# Sourced by probe scripts after `repo_root` is set. Sets `xvfb_pid` for cleanup (historical name).

if [[ "${ZESTWM_TEST_TRACE:-0}" == "1" ]]; then
  export PS4='+ ${BASH_SOURCE##*/}:${LINENO}:${FUNCNAME[0]:-main}: '
  set -x
fi

# Preserve host session display for safety checks. Probes must never target this display.
# Keep caller-provided value when available (e.g. suite wrapper exporting original host DISPLAY).
: "${ZESTWM_HOST_DISPLAY:=${DISPLAY:-}}"

zestwm_require_nested_x11() {
  if command -v Xephyr >/dev/null 2>&1 || command -v Xvfb >/dev/null 2>&1; then
    return 0
  fi
  echo "${1:-zestwm-probe}: need Xephyr or Xvfb" >&2
  exit 77
}

zestwm_start_nested_x11() {
  local disp="$1"
  local log="${2:-/dev/null}"
  local use_xephyr=0
  local backend=""
  if [[ -n "${ZESTWM_HOST_DISPLAY}" && "${disp}" == "${ZESTWM_HOST_DISPLAY}" ]]; then
    echo "zestwm_start_nested_x11: refusing host display '${disp}'" >&2
    exit 2
  fi
  if [[ "${ZESTWM_PREFER_XEPHYR:-}" == "1" ]] && command -v Xephyr >/dev/null 2>&1; then
    use_xephyr=1
  fi
  if [[ "${use_xephyr}" -eq 1 ]]; then
    # -ac: each probe uses its own :N (never the login session DISPLAY).
    Xephyr "${disp}" -screen 1280x720x24 -ac -nolisten tcp >"${log}" 2>&1 &
    xvfb_pid=$!
    backend="Xephyr"
  elif command -v Xvfb >/dev/null 2>&1; then
    Xvfb "${disp}" -screen 0 1280x720x24 -nolisten tcp -extension GLX >"${log}" 2>&1 &
    xvfb_pid=$!
    backend="Xvfb"
  elif command -v Xephyr >/dev/null 2>&1; then
    Xephyr "${disp}" -screen 1280x720x24 -ac -nolisten tcp >"${log}" 2>&1 &
    xvfb_pid=$!
    backend="Xephyr"
  else
    echo "zestwm_start_nested_x11: need Xvfb or Xephyr" >&2
    exit 77
  fi
  # Force subsequent commands in probe process to use nested display by default.
  export DISPLAY="${disp}"
  if [[ "${ZESTWM_NESTED_X11_DEBUG:-0}" == "1" ]]; then
    echo "zestwm_start_nested_x11: backend=${backend} host_display='${ZESTWM_HOST_DISPLAY:-<empty>}' nested_display='${DISPLAY}' pid=${xvfb_pid}" >&2
  fi
}
