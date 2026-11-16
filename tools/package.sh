#!/bin/bash
# Build DtBlkFx (Release) and zip the AU + VST3 with README, LICENSE and the FFTW notice into
# release/DtBlkFx-mac-<version>.zip. The version comes from project(... VERSION ...) in CMakeLists.txt.
#
#   tools/package.sh            (run tools/build_fftw_universal.sh first for a universal zip)
#
# Bundles are ad-hoc signed (codesign -s - --deep --force) and checked with codesign -v. They are
# not notarised, so people who download the zip have to clear the quarantine flag (see README).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

VERSION="$(sed -n 's/^project(DtBlkFx VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
[ -n "$VERSION" ] || { echo "error: no version in CMakeLists.txt" >&2; exit 1; }
NAME="DtBlkFx-mac-$VERSION"
BUILD="$ROOT/build"
ART="$BUILD/DtBlkFx_artefacts/Release"

if [ ! -f "$BUILD/build.ninja" ]; then
    cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release
fi
cmake --build "$BUILD" --target DtBlkFx_AU DtBlkFx_VST3

AU="$ART/AU/DtBlkFx.component"
VST3="$ART/VST3/DtBlkFx.vst3"
for b in "$AU" "$VST3"; do
    xattr -cr "$b" # iCloud / File Provider folders add FinderInfo, which codesign refuses
    codesign -s - --deep --force "$b"
    codesign -v --strict --deep "$b"
    echo "$(basename "$b"): $(lipo -archs "$b/Contents/MacOS/DtBlkFx"), signature ok"
done

# fresh staging folder (an old one is moved to _trash, never deleted)
mkdir -p release _trash
STAGE="release/$NAME"
if [ -e "$STAGE" ]; then mv "$STAGE" "_trash/$NAME-staging-$(date +%Y%m%d-%H%M%S)"; fi
if [ -e "release/$NAME.zip" ]; then mv "release/$NAME.zip" "_trash/$NAME-$(date +%Y%m%d-%H%M%S).zip"; fi
mkdir -p "$STAGE"
ditto --norsrc --noextattr --noqtn "$AU" "$STAGE/DtBlkFx.component"
ditto --norsrc --noextattr --noqtn "$VST3" "$STAGE/DtBlkFx.vst3"
cp README.md CHANGELOG.md LICENSE "$STAGE/"
cp docs/FFTW_COPYRIGHT "$STAGE/FFTW_COPYRIGHT.txt"

# ditto keeps the bundle structure and signatures intact (plain zip can break symlinks)
xattr -cr "$STAGE"
ditto -c -k --norsrc --noextattr --noqtn --keepParent "$STAGE" "release/$NAME.zip"
mv "$STAGE" "_trash/$NAME-staging-$(date +%Y%m%d-%H%M%S)"

echo "release/$NAME.zip ($(du -h "release/$NAME.zip" | cut -f1))"
