#!/usr/bin/env bash
# Forró Box — the macOS build, proven (17-01).
#
# Builds the Release AU, VST3 and standalone — universal (arm64 + x86_64), for
# macOS 11 and later (CMakeLists.txt) — plus the test suite, then PROVES them:
#   1. universal: every shipped binary carries x86_64 AND arm64 (lipo);
#   2. the floor: each slice's LC_BUILD_VERSION minos is 11.0 (otool);
#   3. signed: ad-hoc (`codesign -s -`) — Apple Silicon will not load unsigned
#      code — and `codesign --verify --deep --strict` passes;
#   4. Apple's own validator: the AU installed, `auval -v aumu Frbx Frbx`;
#   5. pluginval on the VST3 (scripts/validate-plugin.sh --macos);
#   6. the WHOLE suite.
# Judged by its exit code, like the Windows and Linux build scripts. Runs on a
# Mac (CI's macos job); JUCE from JUCE_PATH, else scripts/fetch-juce.sh.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${FORROBOX_MACOS_BUILD_DIR:-$ROOT/build-macos}"
MIN_MACOS="11.0"

[[ "$(uname -s)" == "Darwin" ]] || { echo "FATAL: build-macos.sh runs on macOS" >&2; exit 1; }

JUCE_DIR="${JUCE_PATH:-$ROOT/build/juce-src}"
[[ -f "$JUCE_DIR/CMakeLists.txt" ]] || "$ROOT/scripts/fetch-juce.sh" "$JUCE_DIR"

GENERATOR=("Unix Makefiles"); command -v ninja >/dev/null 2>&1 && GENERATOR=(Ninja)
JOBS="$(sysctl -n hw.ncpu)"

echo "=== configure + build (Release, ${GENERATOR[*]}, $JOBS jobs)"
cmake -S "$ROOT" -B "$BUILD" -G "${GENERATOR[@]}" -DCMAKE_BUILD_TYPE=Release -DJUCE_PATH="$JUCE_DIR" >/dev/null
cmake --build "$BUILD" --parallel "$JOBS" \
      --target ForroBox_AU ForroBox_VST3 ForroBox_Standalone ForroBoxTests

ART="$BUILD/ForroBox_artefacts/Release"
AU="$ART/AU/ForroBox.component"
VST3="$ART/VST3/ForroBox.vst3"
APP="$ART/Standalone/ForroBox.app"
BUNDLES=("$AU" "$VST3" "$APP")
for b in "${BUNDLES[@]}"; do [[ -d "$b" ]] || { echo "FATAL: missing bundle $b" >&2; exit 1; }; done

echo "=== universal + floor"
for b in "${BUNDLES[@]}"; do
  bin="$b/Contents/MacOS/ForroBox"
  archs="$(lipo -archs "$bin")"
  [[ " $archs " == *" x86_64 "* && " $archs " == *" arm64 "* ]] \
    || { echo "FATAL: $(basename "$b") is not universal (lipo: $archs)" >&2; exit 1; }
  for arch in x86_64 arm64; do
    minos="$(otool -arch "$arch" -l "$bin" | awk '/LC_BUILD_VERSION/{f=1} f && $1=="minos" {print $2; exit}')"
    [[ "$minos" == "$MIN_MACOS" ]] \
      || { echo "FATAL: $(basename "$b") $arch minos is '$minos', expected $MIN_MACOS" >&2; exit 1; }
  done
  echo "  $(basename "$b"): $archs, minos $MIN_MACOS"
done

echo "=== ad-hoc signature"
for b in "${BUNDLES[@]}"; do
  codesign --force --deep --sign - "$b"
  codesign --verify --deep --strict "$b"
  echo "  signed + verified: $(basename "$b")"
done

echo "=== auval"
COMPONENTS="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$COMPONENTS"
rm -rf "$COMPONENTS/ForroBox.component"
cp -R "$AU" "$COMPONENTS/"
killall -9 AudioComponentRegistrar 2>/dev/null || true   # make it rescan
sleep 2
AUVAL_LOG="$BUILD/auval.log"
auval -v aumu Frbx Frbx > "$AUVAL_LOG" 2>&1 || true
if ! grep -q "AU VALIDATION SUCCEEDED" "$AUVAL_LOG"; then
  echo "FATAL: auval did not succeed:" >&2; tail -40 "$AUVAL_LOG" >&2; exit 1
fi
echo "  AU VALIDATION SUCCEEDED"

echo "=== pluginval (strictness 10)"
"$ROOT/scripts/validate-plugin.sh" --macos "$VST3"

echo "=== the suite"
SUITE_LOG="$BUILD/suite.log"
(cd "$BUILD" && ./ForroBoxTests) > "$SUITE_LOG" 2>&1 || { tail -40 "$SUITE_LOG" >&2; grep "  FAIL  " "$SUITE_LOG" | head -60 >&2; exit 1; }
result="$(grep -E "checks passed" "$SUITE_LOG" | tail -1)"
echo "  $result"
grep -q "OK" <<<"$result" || { grep "  FAIL  " "$SUITE_LOG" | head -60 >&2; exit 1; }

echo "=== macOS build OK — $ART"
