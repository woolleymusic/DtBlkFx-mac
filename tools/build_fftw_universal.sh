#!/bin/bash
# Build a universal (arm64 + x86_64) static single-precision FFTW 3.3.11 for DtBlkFx.
#
#   tools/build_fftw_universal.sh
#
# Downloads the official tarball from fftw.org, checks it against the published MD5 (fftw.org
# publishes an .md5sum next to the tarball) and the SHA-256 Homebrew pins for the same file,
# builds float / static / PIC for each architecture with MACOSX_DEPLOYMENT_TARGET 11.0, and lipos
# the two into third_party/fftw-universal/{include,lib}. CMake picks that folder up automatically.
#
#   arm64:  --enable-neon
#   x86_64: --enable-sse2 --enable-avx --enable-avx2 (FFTW checks the CPU at run time, so the AVX
#           code paths are only used on machines that have them)
#
# Everything it downloads and builds stays in third_party/ (gitignored).
set -euo pipefail

VERSION=3.3.11
TARBALL="fftw-${VERSION}.tar.gz"
URL="https://www.fftw.org/${TARBALL}"
MD5_URL="https://www.fftw.org/${TARBALL}.md5sum"
EXPECTED_MD5="40ec8d0447d03b8f01f8c90aa77bd16f"   # published at ${MD5_URL}
EXPECTED_SHA256="5630c24cdeb33b131612f7eb4b1a9934234754f9f388ff8617458d0be6f239a1" # Homebrew fftw.rb

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TP="$ROOT/third_party"
WORK="$TP/build"
OUT="$TP/fftw-universal"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
export MACOSX_DEPLOYMENT_TARGET=11.0

mkdir -p "$WORK" "$OUT/include" "$OUT/lib"
cd "$WORK"

# --- download and verify
if [ ! -f "$TARBALL" ]; then
    echo "downloading $URL"
    curl -fL --retry 3 -o "$TARBALL.part" "$URL"
    mv "$TARBALL.part" "$TARBALL"
fi
published_md5="$(curl -fsSL "$MD5_URL" | awk '{print $1}')"
actual_md5="$(md5 -q "$TARBALL")"
actual_sha256="$(shasum -a 256 "$TARBALL" | awk '{print $1}')"
echo "md5     $actual_md5 (published: $published_md5)"
echo "sha256  $actual_sha256"
if [ "$published_md5" != "$EXPECTED_MD5" ]; then
    echo "error: the MD5 published by fftw.org ($published_md5) is not the one this script expects" >&2
    exit 1
fi
if [ "$actual_md5" != "$EXPECTED_MD5" ] || [ "$actual_sha256" != "$EXPECTED_SHA256" ]; then
    echo "error: $TARBALL does not match the published checksum; delete it and retry" >&2
    exit 1
fi

if [ ! -d "fftw-${VERSION}" ]; then
    tar xzf "$TARBALL"
fi
# FFTW's configure refuses a source path with spaces in it (this repo usually lives under one), so
# the builds go through a symlink with a plain path
LINK="$(mktemp -d "${TMPDIR:-/tmp}/dtblkfx-fftw.XXXXXX")/w"
ln -s "$WORK" "$LINK"
trap 'rm -f "$LINK"; rmdir "$(dirname "$LINK")" 2>/dev/null || true' EXIT
SRC="$LINK/fftw-${VERSION}"

# --- one build per architecture (out of tree, so the source stays clean)
build_arch() {
    local arch="$1" host="$2"; shift 2
    local obj="$LINK/obj-$arch" prefix="$LINK/install-$arch"
    mkdir -p "$obj"
    (
        cd "$obj"
        [ -f Makefile ] && make distclean >/dev/null 2>&1 || true
        CC="clang -arch $arch" \
        CFLAGS="-O3 -fPIC -mmacosx-version-min=$MACOSX_DEPLOYMENT_TARGET" \
            "$SRC/configure" --host="$host" --prefix="$prefix" \
            --enable-float --enable-static --disable-shared --with-pic \
            --disable-fortran --disable-doc --disable-dependency-tracking "$@" > configure.log
        make -j"$JOBS" > make.log
        make install > install.log
    )
    echo "built $arch: $(lipo -info "$prefix/lib/libfftw3f.a")"
}

build_arch arm64 aarch64-apple-darwin --enable-neon
build_arch x86_64 x86_64-apple-darwin --enable-sse2 --enable-avx --enable-avx2

lipo -create "$WORK/install-arm64/lib/libfftw3f.a" "$WORK/install-x86_64/lib/libfftw3f.a" \
     -output "$OUT/lib/libfftw3f.a"
cp "$WORK/install-arm64/include/fftw3.h" "$OUT/include/fftw3.h"
cp "$SRC/COPYING" "$OUT/COPYING"
echo "FFTW $VERSION float static PIC, arm64 (NEON) + x86_64 (SSE2/AVX/AVX2), macOS $MACOSX_DEPLOYMENT_TARGET+" > "$OUT/BUILD_INFO.txt"

lipo -info "$OUT/lib/libfftw3f.a"
echo "done: $OUT"
