#!/usr/bin/env bash
# Forró Box — the macOS build, proven (17-01).
#
# Builds the Release AU, VST3 and standalone — universal (arm64 + x86_64), for
# macOS 11 and later (CMakeLists.txt) — plus the test suite, then PROVES them:
#   1-3. signed ad hoc (`codesign -s -`) — Apple Silicon will not load unsigned
#      code — then scripts/verify-macos-bundles.sh: universal, minos at the
#      floor, `codesign --verify --deep --strict`;
#   4. Apple's own validator: the AU installed, `auval -v aumu Frbx Frbx`;
#   5. pluginval on the VST3 (scripts/validate-plugin.sh --macos);
#   6. the WHOLE suite.
# Judged by its exit code, like the Windows and Linux build scripts. Runs on a
# Mac (CI's macos job); JUCE from JUCE_PATH, else scripts/fetch-juce.sh.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${FORROBOX_MACOS_BUILD_DIR:-$ROOT/build-macos}"

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

echo "=== ad-hoc signature"
for b in "${BUNDLES[@]}"; do codesign --force --deep --sign - "$b"; done

echo "=== universal, floor, signature"
"$ROOT/scripts/verify-macos-bundles.sh" "${BUNDLES[@]}"

echo "=== auval"
COMPONENTS="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$COMPONENTS"
rm -rf "$COMPONENTS/ForroBox.component"
cp -R "$AU" "$COMPONENTS/"
killall -9 AudioComponentRegistrar 2>/dev/null || true   # make it rescan
# Polled, not slept: registration takes as long as it takes on a cold runner.
for _ in $(seq 60); do auval -a 2>/dev/null | grep -q "Frbx Frbx" && break; sleep 0.5; done
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
(cd "$BUILD" && ./ForroBoxTests) > "$SUITE_LOG" 2>&1 || true
"$ROOT/scripts/judge-suite.sh" "$SUITE_LOG"

echo "=== macOS build OK — $ART"
