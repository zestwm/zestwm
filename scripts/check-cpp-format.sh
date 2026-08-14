#!/usr/bin/env bash
# Verify (or fix) C++ formatting against repo-root .clang-format.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

usage() {
  cat <<'EOF'
Usage:
  scripts/check-cpp-format.sh [--fix] [file ...]

With no file arguments, checks all src/*.cpp, src/*.hpp, and src/*.h.
Without --fix, exits non-zero when formatting differs.
EOF
}

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "check-cpp-format: missing required command '$1'" >&2
    exit 2
  fi
}

fix=0
paths=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --fix) fix=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) paths+=("$1"); shift ;;
  esac
done

need_cmd clang-format

if [[ ${#paths[@]} -eq 0 ]]; then
  while IFS= read -r path; do
    paths+=("${path}")
  done < <(find src -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) | LC_ALL=C sort)
fi

if [[ ${#paths[@]} -eq 0 ]]; then
  echo "check-cpp-format: no C++ files to check" >&2
  exit 0
fi

failed=0
for path in "${paths[@]}"; do
  if [[ ! -f "${path}" ]]; then
    echo "check-cpp-format: missing file '${path}'" >&2
    failed=1
    continue
  fi
  case "${path}" in
    *.cpp|*.hpp|*.h) ;;
    *)
      echo "check-cpp-format: skip non-C++ path '${path}'" >&2
      continue
      ;;
  esac
  if [[ ${fix} -eq 1 ]]; then
    clang-format -i "${path}"
    continue
  fi
  if ! clang-format --dry-run -Werror "${path}" >/dev/null 2>&1; then
    echo "check-cpp-format: formatting drift in ${path}" >&2
    clang-format --dry-run -Werror "${path}" >&2 || true
    failed=1
  fi
done

if [[ ${failed} -ne 0 ]]; then
  echo "check-cpp-format: run 'scripts/check-cpp-format.sh --fix' or 'meson compile -C build cpp-format-fix'" >&2
  exit 1
fi
