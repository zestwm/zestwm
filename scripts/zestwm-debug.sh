#!/usr/bin/env bash
# Debug helper for zestwm crash/leak triage.
# Responsibilities:
# - Capture non-interactive gdb backtraces to disk.
# - Run leak checks outside ptrace (LSan-compatible path).
# - Keep expected reload signals (SIGHUP) non-stopping in gdb.

set -euo pipefail

usage() {
    echo "Usage:"
    echo "  $0 gdb-bt [output_file] [-- <zestwm args...>]"
    echo "  $0 xinit-gdb-bt [output_file] [display] [vt] [-- <zestwm args...>]"
    echo "  $0 leak [output_prefix] [-- <zestwm args...>]"
    echo
    echo "Defaults:"
    echo "  gdb-bt output_file: /tmp/zestwm-gdb-btfull.txt"
    echo "  xinit-gdb-bt output_file: /tmp/zestwm-gdb-btfull.txt"
    echo "  xinit-gdb-bt display/vt: :1 vt2"
    echo "  leak output_prefix: /tmp/zestwm-asan"
}

build_dir="${BUILD_DIR:-build-asan}"
bin="${BIN_PATH:-${build_dir}/zestwm}"

if [[ $# -lt 1 ]]; then
    usage
    exit 1
fi

mode="$1"
shift

if [[ ! -x "${bin}" ]]; then
    echo "error: binary not found or not executable: ${bin}" >&2
    echo "hint: build first (e.g. 'make asan' or meson compile -C ${build_dir})" >&2
    exit 1
fi

case "${mode}" in
gdb-bt)
    out="${1:-/tmp/zestwm-gdb-btfull.txt}"
    if [[ $# -gt 0 ]]; then
        shift
    fi
    extra_args=()
    if [[ $# -gt 0 ]]; then
        if [[ "$1" != "--" ]]; then
            echo "error: expected '--' before zestwm args" >&2
            exit 1
        fi
        shift
        extra_args=("$@")
    fi
    ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" \
    gdb -q -batch \
        -ex "set pagination off" \
        -ex "set debuginfod enabled off" \
        -ex "handle SIGPIPE nostop noprint pass" \
        -ex "handle SIGHUP nostop noprint pass" \
        -ex "run" \
        -ex "bt full" \
        -ex "thread apply all bt full" \
        --args "${bin}" "${extra_args[@]}" \
        > "${out}" 2>&1 || true
    echo "gdb backtrace saved: ${out}"
    ;;
xinit-gdb-bt)
    out="${1:-/tmp/zestwm-gdb-btfull.txt}"
    if [[ $# -gt 0 ]]; then
        shift
    fi
    display="${1:-:1}"
    if [[ $# -gt 0 ]]; then
        shift
    fi
    vt="${1:-vt2}"
    if [[ $# -gt 0 ]]; then
        shift
    fi
    extra_args=()
    if [[ $# -gt 0 ]]; then
        if [[ "$1" != "--" ]]; then
            echo "error: expected '--' before zestwm args" >&2
            exit 1
        fi
        shift
        extra_args=("$@")
    fi
    ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" \
    xinit /usr/bin/gdb -q -batch \
        -ex "set pagination off" \
        -ex "set debuginfod enabled off" \
        -ex "handle SIGPIPE nostop noprint pass" \
        -ex "handle SIGHUP nostop noprint pass" \
        -ex "run" \
        -ex "bt full" \
        -ex "thread apply all bt full" \
        --args "${bin}" "${extra_args[@]}" -- "${display}" "${vt}" \
        > "${out}" 2>&1 || true
    echo "xinit+gdb backtrace saved: ${out}"
    ;;
leak)
    out_prefix="${1:-/tmp/zestwm-asan}"
    if [[ $# -gt 0 ]]; then
        shift
    fi
    extra_args=()
    if [[ $# -gt 0 ]]; then
        if [[ "$1" != "--" ]]; then
            echo "error: expected '--' before zestwm args" >&2
            exit 1
        fi
        shift
        extra_args=("$@")
    fi
    ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:log_path=${out_prefix}}" \
    LSAN_OPTIONS="${LSAN_OPTIONS:-verbosity=1:log_threads=1}" \
        "${bin}" "${extra_args[@]}"
    ;;
*)
    echo "error: unknown mode '${mode}'" >&2
    usage
    exit 1
    ;;
esac
