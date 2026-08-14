#!/usr/bin/env bash
# Build release assets for a published GitHub Release (source, binary tar, .deb).
#
# Usage:
#   scripts/build-release-assets.sh <version> <outdir>
#
# version: semver without leading v (e.g. 0.1.0)
# outdir:  directory that will receive the artifacts
set -euo pipefail

if [[ $# -lt 2 ]]; then
  echo "usage: $0 <version> <outdir>" >&2
  exit 2
fi

VERSION="$1"
OUTDIR="$2"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PREFIX="zestwm-${VERSION}"

BUILD_DEB=1
if [[ "${ZESTWM_NO_DEB:-0}" == "1" ]]; then
  BUILD_DEB=0
fi

mkdir -p "$OUTDIR"
OUTDIR="$(cd "$OUTDIR" && pwd)"

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "missing required command: $1" >&2
    exit 1
  }
}

need_cmd meson
need_cmd ninja
need_cmd tar
need_cmd strip
need_cmd sha256sum

# --- source tarball (git archive when available, else filtered tar) ---
SOURCE_TAR="${OUTDIR}/${PREFIX}.tar.gz"
if git -C "$ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  git -C "$ROOT" archive --format=tar.gz --prefix="${PREFIX}/" -o "$SOURCE_TAR" HEAD
else
  tar -C "$ROOT" --exclude='./build*' --exclude='./.git' --exclude='./.cursor' \
    --transform "s,^\\./,${PREFIX}/," -czf "$SOURCE_TAR" .
fi

# --- release build ---
BUILD_DIR="${OUTDIR}/build-release"
rm -rf "$BUILD_DIR"
meson setup "$BUILD_DIR" "$ROOT" --buildtype=release -Db_sanitize=none \
  -Dunit_tests=false -Dintegration_tests=false
meson compile -C "$BUILD_DIR"
strip -o "${BUILD_DIR}/zestwm.stripped" "${BUILD_DIR}/zestwm"
strip -o "${BUILD_DIR}/zestctl.stripped" "${BUILD_DIR}/zestctl"
mv -f "${BUILD_DIR}/zestwm.stripped" "${BUILD_DIR}/zestwm"
mv -f "${BUILD_DIR}/zestctl.stripped" "${BUILD_DIR}/zestctl"

# --- binary tarball ---
BIN_STAGE="${OUTDIR}/stage-bin"
rm -rf "$BIN_STAGE"
mkdir -p "${BIN_STAGE}/${PREFIX}/bin" "${BIN_STAGE}/${PREFIX}/share/doc/zestwm/examples"
cp -a "${BUILD_DIR}/zestwm" "${BUILD_DIR}/zestctl" "${BIN_STAGE}/${PREFIX}/bin/"
cp -a "${ROOT}/LICENSE" "${ROOT}/README.md" "${BIN_STAGE}/${PREFIX}/"
cp -a "${ROOT}/examples/zestwm.conf" "${BIN_STAGE}/${PREFIX}/share/doc/zestwm/examples/"
BIN_TAR="${OUTDIR}/${PREFIX}-linux-x86_64.tar.gz"
tar -C "$BIN_STAGE" -czf "$BIN_TAR" "$PREFIX"

# --- .deb (amd64, dynamically linked; skipped when disabled or dpkg-deb is unavailable) ---
DEB_FILE=""
if [[ $BUILD_DEB -eq 1 ]] && command -v dpkg-deb >/dev/null 2>&1; then
  DEB_ROOT="${OUTDIR}/stage-deb"
  rm -rf "$DEB_ROOT"
  mkdir -p "${DEB_ROOT}/DEBIAN" "${DEB_ROOT}/usr/bin" \
    "${DEB_ROOT}/usr/share/doc/zestwm/examples" \
    "${DEB_ROOT}/usr/share/doc/zestwm"
  install -m755 "${BUILD_DIR}/zestwm" "${BUILD_DIR}/zestctl" "${DEB_ROOT}/usr/bin/"
  install -m644 "${ROOT}/examples/zestwm.conf" "${DEB_ROOT}/usr/share/doc/zestwm/examples/"
  install -m644 "${ROOT}/README.md" "${DEB_ROOT}/usr/share/doc/zestwm/"
  install -m644 "${ROOT}/LICENSE" "${DEB_ROOT}/usr/share/doc/zestwm/copyright"

  cat >"${DEB_ROOT}/DEBIAN/control" <<EOF
Package: zestwm
Version: ${VERSION}
Section: x11
Priority: optional
Architecture: amd64
Maintainer: zestwm contributors <noreply@users.noreply.github.com>
Depends: libxcb1, libxcb-randr0, libxcb-cursor0, libcairo2, libpangocairo-1.0-0, libpango-1.0-0, libglib2.0-0t64 | libglib2.0-0, libfontconfig1, libstdc++6
Description: Workspace-first tiling window manager for X11/XLibre
 ZestWM is a small tiling WM focused on stable workspace identity, BSP
 tiling with tab groups, special overlay scratchpads, and file-based
 configuration (zestwm.conf).
EOF

  DEB_FILE="${OUTDIR}/zestwm_${VERSION}_amd64.deb"
  dpkg-deb --root-owner-group --build "$DEB_ROOT" "$DEB_FILE"
else
  echo "warning: dpkg-deb not found; skipping .deb (expected on non-Debian hosts)" >&2
fi

# --- checksums ---
(
  cd "$OUTDIR"
  sums=("$(basename "$SOURCE_TAR")" "$(basename "$BIN_TAR")")
  if [[ -n "$DEB_FILE" ]]; then
    sums+=("$(basename "$DEB_FILE")")
  fi
  sha256sum "${sums[@]}" >SHA256SUMS-linux.txt
)

echo "Built:"
echo "  $SOURCE_TAR"
echo "  $BIN_TAR"
if [[ -n "$DEB_FILE" ]]; then
  echo "  $DEB_FILE"
fi
echo "  ${OUTDIR}/SHA256SUMS-linux.txt"
