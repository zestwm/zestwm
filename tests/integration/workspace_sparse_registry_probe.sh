#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-}"
if [[ -z "${build_dir}" ]]; then
  echo "workspace-sparse-registry: missing build directory argument" >&2
  exit 2
fi

zestwm_bin="${build_dir}/zestwm"
zestctl_bin="${build_dir}/zestctl"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=tests/integration/xserver_common.inc.sh
source "${repo_root}/tests/integration/xserver_common.inc.sh"

base_conf_path="${repo_root}/examples/zestwm.conf"
probe_conf_path="${repo_root}/tests/integration/.workspace-sparse-registry.conf"

if [[ ! -x "${zestwm_bin}" || ! -x "${zestctl_bin}" ]]; then
  echo "workspace-sparse-registry: missing built binaries in ${build_dir}" >&2
  exit 2
fi
if [[ ! -f "${base_conf_path}" ]]; then
  echo "workspace-sparse-registry: missing config fixture ${base_conf_path}" >&2
  exit 2
fi

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "workspace-sparse-registry: missing required command '$1'" >&2
    exit 77
  fi
}

zestwm_require_nested_x11 "workspace-sparse-registry-probe"
need_cmd xdpyinfo

cp "${base_conf_path}" "${probe_conf_path}"
cat >> "${probe_conf_path}" <<'EOF'
workspace = 10
workspace = 3, sparse-ws
EOF

display_num=191
while [[ ${display_num} -lt 210 ]]; do
  if [[ ! -S "/tmp/.X11-unix/X${display_num}" ]]; then
    break
  fi
  display_num=$((display_num + 1))
done
if [[ ${display_num} -ge 210 ]]; then
  echo "workspace-sparse-registry: unable to allocate free X11 display" >&2
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
  rm -f "${probe_conf_path}"
}
trap cleanup EXIT

zestwm_start_nested_x11 "${display}"

for _ in {1..40}; do
  if ! kill -0 "${xvfb_pid}" 2>/dev/null; then
    echo "workspace-sparse-registry: nested X server exited during startup (skipping on this host)" >&2
    exit 77
  fi
  if DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
  echo "workspace-sparse-registry: nested X server did not become ready (skipping on this host)" >&2
  exit 77
fi

DISPLAY="${display}" "${zestwm_bin}" -c "${probe_conf_path}" >/tmp/zestwm-workspace-sparse-registry.log 2>&1 &
wm_pid=$!

for _ in {1..120}; do
  if DISPLAY="${display}" "${zestctl_bin}" version >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! DISPLAY="${display}" "${zestctl_bin}" version >/dev/null 2>&1; then
  echo "workspace-sparse-registry: zestwm did not respond on ${display}" >&2
  exit 2
fi

ws_out="$(DISPLAY="${display}" "${zestctl_bin}" workspaces)"

for _ in {1..200}; do
  ws_out="$(DISPLAY="${display}" "${zestctl_bin}" workspaces 2>/dev/null || true)"
  if printf '%s\n' "${ws_out}" | grep -qF 'name:web' && printf '%s\n' "${ws_out}" | awk '$2=="id:10" {found=1} END {exit found?0:1}'; then
    break
  fi
  sleep 0.05
done

ws_out="$(DISPLAY="${display}" "${zestctl_bin}" workspaces)"

if ! printf '%s\n' "${ws_out}" | awk '$2=="id:1" {for (i=1;i<=NF;i++) if ($i ~ /^name:/) {sub(/^name:/,"",$i); if ($i=="web") ok=1}} END {exit ok?0:1}'; then
  echo "workspace-sparse-registry: expected id:1 name web from base config" >&2
  printf '%s\n' "${ws_out}" >&2
  exit 1
fi

if ! printf '%s\n' "${ws_out}" | awk '$2=="id:3" {for (i=1;i<=NF;i++) if ($i ~ /^name:/) {sub(/^name:/,"",$i); if ($i=="sparse-ws") ok=1}} END {exit ok?0:1}'; then
  echo "workspace-sparse-registry: expected id:3 name sparse-ws after override" >&2
  printf '%s\n' "${ws_out}" >&2
  exit 1
fi

if ! printf '%s\n' "${ws_out}" | awk '$2=="id:7" {for (i=1;i<=NF;i++) if ($i ~ /^name:/) {sub(/^name:/,"",$i); if ($i=="7") ok=1}} END {exit ok?0:1}'; then
  echo "workspace-sparse-registry: expected sparse gap id:7 to keep default numeric name" >&2
  printf '%s\n' "${ws_out}" >&2
  exit 1
fi

if ! printf '%s\n' "${ws_out}" | awk '$2=="id:10" {found=1} END {exit found?0:1}'; then
  echo "workspace-sparse-registry: expected sparse target id:10 to exist" >&2
  printf '%s\n' "${ws_out}" >&2
  exit 1
fi

if ! DISPLAY="${display}" "${zestctl_bin}" dispatch workspace web >/dev/null 2>&1; then
  echo "workspace-sparse-registry: dispatch by bare name failed" >&2
  exit 1
fi

echo "workspace-sparse-registry: pass"
