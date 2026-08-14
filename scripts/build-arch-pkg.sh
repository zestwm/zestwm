#!/usr/bin/env bash
# Build an Arch Linux package (makepkg) from a local source tarball.
#
# Usage:
#   scripts/build-arch-pkg.sh <version> <source-tar.gz> <outdir> [giturl]
set -euo pipefail

if [[ $# -lt 3 ]]; then
  echo "usage: $0 <version> <source-tar.gz> <outdir> [giturl]" >&2
  exit 2
fi

VERSION="$1"
SOURCE_TAR="$2"
OUTDIR="$3"
GITURL="${4:-}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "missing required command: $1" >&2
    exit 1
  }
}

need_cmd makepkg
need_cmd sha256sum

SOURCE_TAR="$(cd "$(dirname "$SOURCE_TAR")" && pwd)/$(basename "$SOURCE_TAR")"
mkdir -p "$OUTDIR"
OUTDIR="$(cd "$OUTDIR" && pwd)"

WORK="$(mktemp -d "${TMPDIR:-/tmp}/zestwm-makepkg.XXXXXX")"
cleanup() {
  rm -rf "$WORK"
}
trap cleanup EXIT

cp -a "${ROOT}/packaging/arch/PKGBUILD" "$WORK/PKGBUILD"
cp -a "$SOURCE_TAR" "$WORK/zestwm-${VERSION}.tar.gz"

if [[ -n "$GITURL" ]]; then
  sed -i "s|^url=.*|url='${GITURL}'|" "$WORK/PKGBUILD"
fi

# Point makepkg at the local tarball and fill version + checksum.
SUM="$(sha256sum "$WORK/zestwm-${VERSION}.tar.gz" | awk '{print $1}')"
sed -i \
  -e "s/^pkgver=.*/pkgver=${VERSION}/" \
  -e "s|^source=.*|source=(\"zestwm-${VERSION}.tar.gz\")|" \
  -e "s/^sha256sums=.*/sha256sums=('${SUM}')/" \
  "$WORK/PKGBUILD"

# makepkg refuses to run as root; create a build user when needed.
run_makepkg() {
  (cd "$WORK" && makepkg -f --noconfirm --cleanbuild)
}

if [[ "$(id -u)" -eq 0 ]]; then
  need_cmd useradd
  useradd -m -u 10000 zestbuild 2>/dev/null || true
  chown -R zestbuild:zestbuild "$WORK"
  su -s /bin/bash zestbuild -c "cd '$WORK' && makepkg -f --noconfirm --cleanbuild"
else
  run_makepkg
fi

shopt -s nullglob
pkgs=("$WORK"/zestwm-*.pkg.tar.zst)
if [[ ${#pkgs[@]} -eq 0 ]]; then
  # Older makepkg may emit .pkg.tar.xz
  pkgs=("$WORK"/zestwm-*.pkg.tar.*)
fi
if [[ ${#pkgs[@]} -eq 0 ]]; then
  echo "makepkg produced no package under $WORK" >&2
  exit 1
fi

cp -a "${pkgs[@]}" "$OUTDIR/"
(
  cd "$OUTDIR"
  sha256sum "$(basename "${pkgs[0]}")" >SHA256SUMS-arch.txt
)
# Publish PKGBUILD + .SRCINFO next to the package for AUR mirrors.
if [[ -f "$WORK/.SRCINFO" ]]; then
  cp -a "$WORK/.SRCINFO" "$OUTDIR/PKGBUILD.SRCINFO"
else
  (cd "$WORK" && makepkg --printsrcinfo >"$OUTDIR/PKGBUILD.SRCINFO") || true
fi
cp -a "$WORK/PKGBUILD" "$OUTDIR/PKGBUILD"

echo "Built Arch package(s):"
printf '  %s\n' "${pkgs[@]}"
