#!/usr/bin/env bash
# Forró Box — a macOS bundle is universal, built for the floor, and signed (17-02).
#
#   scripts/verify-macos-bundles.sh <bundle>...
#
# For each bundle's Contents/MacOS/ForroBox: every architecture CMakeLists.txt
# asks for is present (lipo), each slice's LC_BUILD_VERSION minos equals the
# deployment target (otool), and `codesign --verify --deep --strict` passes.
# The expected values are READ from CMakeLists.txt's CMAKE_OSX_ARCHITECTURES and
# CMAKE_OSX_DEPLOYMENT_TARGET — one source. Run after the build
# (scripts/build-macos.sh) and again on the unzipped release package (CI), so
# what ships is checked, not only what was built.
set -euo pipefail

(( $# > 0 )) || { echo "usage: verify-macos-bundles.sh <bundle>..." >&2; exit 2; }
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARCHS=$(sed -nE 's/^set\(CMAKE_OSX_ARCHITECTURES "([^"]+)".*/\1/p' "$ROOT/CMakeLists.txt" | tr ';' ' ')
MIN_MACOS=$(sed -nE 's/^set\(CMAKE_OSX_DEPLOYMENT_TARGET "([^"]+)".*/\1/p' "$ROOT/CMakeLists.txt")
[[ -n "$ARCHS" && -n "$MIN_MACOS" ]] || { echo "FATAL: no CMAKE_OSX_* defaults in CMakeLists.txt" >&2; exit 1; }

for bundle in "$@"; do
  bin="$bundle/Contents/MacOS/ForroBox"
  [[ -f "$bin" ]] || { echo "FATAL: no binary in $bundle" >&2; exit 1; }
  have="$(lipo -archs "$bin")"
  for arch in $ARCHS; do
    [[ " $have " == *" $arch "* ]] || { echo "FATAL: $(basename "$bundle") lacks $arch (lipo: $have)" >&2; exit 1; }
    minos="$(otool -arch "$arch" -l "$bin" | awk '/LC_BUILD_VERSION/{f=1} f && $1=="minos" {print $2; exit}')"
    [[ "$minos" == "$MIN_MACOS" ]] \
      || { echo "FATAL: $(basename "$bundle") $arch minos is '$minos', expected $MIN_MACOS" >&2; exit 1; }
  done
  codesign --verify --deep --strict "$bundle"
  echo "  $(basename "$bundle"): $have, minos $MIN_MACOS, signature verified"
done
