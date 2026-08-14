#!/usr/bin/env bash
# Build a Debian binary package (.deb) from a local source tarball.
#
# Usage:
#   scripts/build-deb-pkg.sh <upstream-version> <debian-revision> <source-tar.gz> <outdir>
#
# upstream-version: e.g. 6.8
# debian-revision:  e.g. 1
# source-tar.gz:    gzipped source tarball with a single top-level directory
# outdir:           directory that will receive the .deb
set -euo pipefail

if [[ $# -lt 4 ]]; then
  echo "usage: $0 <upstream-version> <debian-revision> <source-tar.gz> <outdir>" >&2
  exit 2
fi

UPSTREAM="$1"
REVISION="$2"
SOURCE_TAR="$3"
OUTDIR="$4"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEB_VERSION="${UPSTREAM}-${REVISION}"

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "missing required command: $1" >&2
    exit 1
  }
}

need_cmd dpkg-buildpackage
need_cmd tar
need_cmd sha256sum

mkdir -p "$OUTDIR"
OUTDIR="$(cd "$OUTDIR" && pwd)"
SOURCE_TAR="$(cd "$(dirname "$SOURCE_TAR")" && pwd)/$(basename "$SOURCE_TAR")"

WORK="$(mktemp -d "${TMPDIR:-/tmp}/zestwm-deb.XXXXXX")"
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

# Extract the source; expect a single top-level directory.
tar -xzf "$SOURCE_TAR" -C "$WORK"
SRC="$(find "$WORK" -mindepth 1 -maxdepth 1 -type d | head -n1)"
if [[ -z "$SRC" ]]; then
  echo "source tarball did not contain a top-level directory" >&2
  exit 1
fi

# Force the canonical source directory name when the tarball top-level differs.
RENAMED="${WORK}/zestwm-${UPSTREAM}"
if [[ "$SRC" != "$RENAMED" ]]; then
  mv "$SRC" "$RENAMED"
  SRC="$RENAMED"
fi

# Copy Debian packaging files into the source tree.
cp -aT "${ROOT}/packaging/debian" "${SRC}/debian"
chmod +x "${SRC}/debian/rules"

# Generate changelog from the template.
DATE_RFC2822="$(LC_ALL=C date -R)"
sed \
  -e "s/{{VERSION}}/${DEB_VERSION}/g" \
  -e "s/{{DATE}}/${DATE_RFC2822}/g" \
  "${SRC}/debian/changelog.template" >"${SRC}/debian/changelog"
rm -f "${SRC}/debian/changelog.template"

# Provide the upstream orig.tar.gz expected by dpkg-buildpackage for 3.0 (quilt).
ln -s "$SOURCE_TAR" "${WORK}/zestwm_${UPSTREAM}.orig.tar.gz"

# Build an unsigned binary package.
( cd "$SRC" && dpkg-buildpackage -us -uc -b )

# Collect the .deb produced in the parent of the source tree.
shopt -s nullglob
deb_files=("${WORK}"/*.deb)
if [[ ${#deb_files[@]} -eq 0 ]]; then
  echo "dpkg-buildpackage produced no .deb under $WORK" >&2
  exit 1
fi

cp -a "${deb_files[@]}" "$OUTDIR/"
(
  cd "$OUTDIR"
  for f in *.deb; do
    sha256sum "$f" >>SHA256SUMS-deb.txt
  done
)

echo "Built Debian package(s):"
printf '  %s\n' "${deb_files[@]}"
