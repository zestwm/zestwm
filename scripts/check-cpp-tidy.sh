#!/usr/bin/env bash
# Run clang-tidy on C++ translation units using Meson's compile_commands.json.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

build_dir="${BUILD_DIR:-build}"
compile_db="${build_dir}/compile_commands.json"

usage() {
  cat <<'EOF'
Usage:
  scripts/check-cpp-tidy.sh [file.cpp ...]

Environment:
  BUILD_DIR   Meson build directory (default: build)

With no file arguments, checks all src/*.cpp files.
Requires: meson setup <build_dir> (compile_commands.json).
EOF
}

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "check-cpp-tidy: missing required command '$1'" >&2
    exit 2
  fi
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help) usage; exit 0 ;;
    *) break ;;
  esac
done

need_cmd clang-tidy

if [[ ! -f "${compile_db}" ]]; then
  echo "check-cpp-tidy: missing ${compile_db}; run 'meson setup ${build_dir}' first" >&2
  exit 2
fi

paths=("$@")
if [[ ${#paths[@]} -eq 0 ]]; then
  while IFS= read -r path; do
    paths+=("${path}")
  done < <(find src -type f -name '*.cpp' | LC_ALL=C sort)
fi

if [[ ${#paths[@]} -eq 0 ]]; then
  echo "check-cpp-tidy: no translation units to check" >&2
  exit 0
fi

failed=0
for path in "${paths[@]}"; do
  if [[ ! -f "${path}" ]]; then
    echo "check-cpp-tidy: missing file '${path}'" >&2
    failed=1
    continue
  fi
  case "${path}" in
    *.cpp) ;;
    *)
      echo "check-cpp-tidy: skip non-.cpp path '${path}'" >&2
      continue
      ;;
  esac
  if ! clang-tidy -p "${build_dir}" "${path}"; then
    failed=1
  fi
done

exit "${failed}"
