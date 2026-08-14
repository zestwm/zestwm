#!/usr/bin/env bash
# Record a short GIF/MP4 demo of ZestWM inside an isolated Xvfb/Xephyr session.
# The demo shows: BSP tiling, workspace switching, and tabbed grouping.
# Output is an optimized looping GIF.

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=tests/integration/xserver_common.inc.sh
source "${repo_root}/tests/integration/xserver_common.inc.sh"

build_dir="${BUILD_DIR:-build}"
output_file="${1:-${repo_root}/docs/zestwm-demo.gif}"
output_format="gif"

if [[ "${output_file}" == *.mp4 ]]; then
    output_format="mp4"
fi

zestwm_bin="${repo_root}/${build_dir}/zestwm"
zestctl_bin="${repo_root}/${build_dir}/zestctl"

duration=21
fps=12
scale=720

need_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "demo-gif: missing required command '$1'" >&2
        exit 2
    fi
}

need_cmd Xvfb
need_cmd ffmpeg
need_cmd python3
need_cmd xsetroot
need_cmd xdotool
need_cmd xdpyinfo

if ! python3 -c 'import tkinter' >/dev/null 2>&1; then
    echo "demo-gif: python3 tkinter module not available" >&2
    exit 2
fi

if [[ ! -x "${zestwm_bin}" || ! -x "${zestctl_bin}" ]]; then
    if [[ -d "${repo_root}/${build_dir}" ]]; then
        echo "demo-gif: building ${build_dir}..."
        (cd "${repo_root}" && meson compile -C "${build_dir}")
    else
        echo "demo-gif: missing build directory ${build_dir}; run 'meson setup ${build_dir}' first" >&2
        exit 2
    fi
fi

if [[ ! -x "${zestwm_bin}" || ! -x "${zestctl_bin}" ]]; then
    echo "demo-gif: cannot find ${zestwm_bin} or ${zestctl_bin}" >&2
    exit 2
fi

demo_home="$(mktemp -d /tmp/zestwm-demo.XXXXXX)"
xdg_config="${demo_home}/.config"
xdg_state="${demo_home}/.local/state"
xdg_cache="${demo_home}/.cache"
xdg_data="${demo_home}/.local/share"
mkdir -p "${xdg_config}/zestwm" "${xdg_state}" "${xdg_cache}" "${xdg_data}"

demo_client="${demo_home}/demo-client.py"
cat > "${demo_client}" <<'EOF'
import sys
import tkinter as tk

title, bg, fg = sys.argv[1:4]
root = tk.Tk()
root.title(title)
root.configure(bg=bg)
label = tk.Label(
    root,
    text=title,
    bg=bg,
    fg=fg,
    font=('sans', 18),
    anchor='center',
)
label.pack(expand=True, fill='both')
root.protocol('WM_DELETE_WINDOW', root.destroy)
root.mainloop()
EOF

demo_conf="${xdg_config}/zestwm/zestwm.conf"
cat > "${demo_conf}" <<EOF
font = monospace:size=10

layout = tree [T]

workspace = web
workspace = code
workspace = chat

general {
    col.active_border = #005577
    col.inactive_border = #444444
    dim_special = 0.2
}

group {
    groupbar {
        enabled = true
        render_titles = true
        position = top
        col.active = #eeeeee
        col.inactive = #bbbbbb
        col.background = #222222
    }
}

binds {
    Alt+Return { spawn python3 ${demo_client} Client black white; }
    Alt+1 { workspace web; }
    Alt+2 { workspace code; }
    Alt+3 { workspace chat; }
    Alt+g { groupmode; }
    Alt+y { changegroupactive f; }
    Alt+u { changegroupactive b; }
    Alt+Shift+q { quit; }
}
EOF

display_num=100
while [[ -S "/tmp/.X11-unix/X${display_num}" ]]; do
    display_num=$((display_num + 1))
done
display=":${display_num}"

demo_pids=()
xvfb_pid=""
zestwm_pid=""
ffmpeg_pid=""

run_zestctl() {
    "${zestctl_bin}" "$@"
}

press_key() {
    xdotool key "$1"
}

spawn_client() {
    local label="$1"
    local bg="$2"
    local fg="$3"
    python3 "${demo_client}" "${label}" "${bg}" "${fg}" &
    demo_pids+=("$!")
}

cleanup() {
    if [[ -n "${ffmpeg_pid}" ]]; then
        kill "${ffmpeg_pid}" 2>/dev/null || true
        wait "${ffmpeg_pid}" 2>/dev/null || true
    fi
    if [[ -n "${zestwm_pid}" ]]; then
        kill "${zestwm_pid}" 2>/dev/null || true
        wait "${zestwm_pid}" 2>/dev/null || true
    fi
    if [[ ${#demo_pids[@]} -gt 0 ]]; then
        kill "${demo_pids[@]}" 2>/dev/null || true
    fi
    if [[ -n "${xvfb_pid}" ]]; then
        kill "${xvfb_pid}" 2>/dev/null || true
        wait "${xvfb_pid}" 2>/dev/null || true
    fi
    rm -rf "${demo_home}"
}
trap cleanup EXIT

zestwm_require_nested_x11 "demo-gif"

# Start the nested X server. xserver_common.inc.sh sets DISPLAY and xvfb_pid.
zestwm_start_nested_x11 "${display}" "/tmp/zestwm-demo-xvfb.log"

# Wait for the X server to be ready.
for _ in {1..50}; do
    if DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
        break
    fi
    sleep 0.1
done
if ! DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
    echo "demo-gif: nested X server did not become ready" >&2
    exit 2
fi

xsetroot -solid "#2b2b2b"

# Start recording before the WM launches to capture the full startup.
mkdir -p "$(dirname "$(realpath -m "${output_file}")")"
raw_video="${demo_home}/demo.mp4"
ffmpeg -y -hide_banner -loglevel error \
    -f x11grab -draw_mouse 0 -r 20 -video_size 1280x720 -i "${display}.0+0,0" \
    -t "${duration}" -c:v libx264 -pix_fmt yuv420p -an "${raw_video}" &
ffmpeg_pid=$!

# Run the WM in an isolated environment.
# --reload skips XDG autostart, which avoids starting host agents (e.g. xfce-polkit)
# inside the nested X server.
HOME="${demo_home}" \
    XDG_CONFIG_HOME="${xdg_config}" \
    XDG_STATE_HOME="${xdg_state}" \
    XDG_CACHE_HOME="${xdg_cache}" \
    XDG_DATA_HOME="${xdg_data}" \
    "${zestwm_bin}" --reload -c "${demo_conf}" >/tmp/zestwm-demo-wm.log 2>&1 &
zestwm_pid=$!

# Wait until zestwm responds to zestctl.
ready=0
for _ in {1..50}; do
    if run_zestctl version >/dev/null 2>&1; then
        ready=1
        break
    fi
    sleep 0.1
done
if [[ ${ready} -eq 0 ]]; then
    echo "demo-gif: zestwm did not become ready" >&2
    exit 2
fi

# Demo choreography.
spawn_client 'Client 1' 'black' 'white'
sleep 2.5

spawn_client 'Client 2' '#1e1e1e' '#00ff00'
sleep 2.5

# Enable groupmode on the focused leaf so the next client becomes a tab.
press_key 'Alt+g'
sleep 1.0

spawn_client 'Client 3' '#1e1e1e' '#ffaa00'
sleep 2.5

# Cycle through the grouped tabs to show the groupbar.
press_key 'Alt+u'
sleep 1.5

press_key 'Alt+y'
sleep 1.5

run_zestctl dispatch workspace code
sleep 1.5

spawn_client 'Client 4' 'white' 'black'
sleep 2.0

# Wait for ffmpeg to finish recording.
if ! wait "${ffmpeg_pid}"; then
    echo "demo-gif: ffmpeg recording failed" >&2
    exit 2
fi

if [[ "${output_format}" == "mp4" ]]; then
    cp "${raw_video}" "${output_file}"
    echo "demo-gif: saved ${output_file}"
else
    ffmpeg -y -hide_banner -loglevel error -i "${raw_video}" -vf "
        fps=${fps},
        scale=${scale}:-1:flags=lanczos,
        split[s0][s1];
        [s0]palettegen=max_colors=128[p];
        [s1][p]paletteuse=dither=bayer
    " -loop 0 "${output_file}"
    echo "demo-gif: saved ${output_file}"
fi
