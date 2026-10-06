#!/usr/bin/env bash
# Forró Box — the Linux PORTABLE release build (15-02).
#
# Builds the Release VST3, standalone and test suite inside an Ubuntu 22.04
# container (docker/linux-portable.Dockerfile, `build` stage) with
# libstdc++/libgcc linked statically, then PROVES the result rather than
# trusting the setting:
#   1. the floor — scripts/check-glibc-floor.sh: no binary needs a glibc newer
#      than 2.35, nor any libstdc++ version at all;
#   2. a clean machine — the `runtime` stage, a 22.04 with the shared libraries
#      only and no compiler: every library resolves (ldd) and the whole suite
#      passes, headless (scripts/headless-x.sh).
# Judged by its exit code, like scripts/build-windows.sh. ~/JUCE is mounted
# READ-ONLY. Output: build-portable/ (gitignored).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JUCE_DIR="${JUCE_PATH:-$HOME/JUCE}"
BUILD_DIR="${FORROBOX_PORTABLE_BUILD_DIR:-build-portable}"
PORTABLE="${FORROBOX_PORTABLE_LINUX:-ON}"
GLIBC_FLOOR="2.35"
DOCKERFILE="$ROOT/docker/linux-portable.Dockerfile"

[[ -f "$JUCE_DIR/CMakeLists.txt" ]] || { echo "FATAL: no JUCE checkout at $JUCE_DIR (set JUCE_PATH)" >&2; exit 1; }

echo "=== images"
docker build -q --target build   -t forrobox-linux-build:22.04   -f "$DOCKERFILE" "$ROOT/docker" >/dev/null
docker build -q --target runtime -t forrobox-linux-runtime:22.04 -f "$DOCKERFILE" "$ROOT/docker" >/dev/null

echo "=== build (Release, FORROBOX_PORTABLE_LINUX=$PORTABLE) in $BUILD_DIR"
docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp \
  -v "$ROOT:/src" -v "$JUCE_DIR:/juce:ro" -w /src forrobox-linux-build:22.04 bash -c "
    set -euo pipefail
    cmake -S . -B '$BUILD_DIR' -G Ninja -DCMAKE_BUILD_TYPE=Release -DJUCE_PATH=/juce \
          -DFORROBOX_PORTABLE_LINUX=$PORTABLE >/dev/null
    cmake --build '$BUILD_DIR'"

ART="$BUILD_DIR/ForroBox_artefacts/Release"
SO="$ART/VST3/ForroBox.vst3/Contents/x86_64-linux/ForroBox.so"
STANDALONE="$ART/Standalone/ForroBox"
TESTS="$BUILD_DIR/ForroBoxTests"

echo "=== floor"
(cd "$ROOT" && scripts/check-glibc-floor.sh "$GLIBC_FLOOR" "$SO" "$STANDALONE" "$TESTS")

# The build dir is mounted READ-ONLY; the suite runs from /tmp because it writes
# ui-renders/ into its working directory. `docs/` is mounted where the build
# compiled its path (/src/docs): the suite parses the groove format's page
# (19-01), and it is a source file, not a build output.
echo "=== clean ubuntu:22.04 (runtime only): ldd + the suite"
docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp \
  -v "$ROOT/$BUILD_DIR:/build:ro" -v "$ROOT/scripts:/scripts:ro" -v "$JUCE_DIR:/juce:ro" \
  -v "$ROOT/docs:/src/docs:ro" \
  forrobox-linux-runtime:22.04 bash -c '
    set -euo pipefail
    for bin in /build/ForroBox_artefacts/Release/VST3/ForroBox.vst3/Contents/x86_64-linux/ForroBox.so \
               /build/ForroBox_artefacts/Release/Standalone/ForroBox; do
      if ldd "$bin" | grep -q "not found"; then
        echo "FATAL: unresolved libraries in $bin:" >&2; ldd "$bin" | grep "not found" >&2; exit 1
      fi
      echo "  ldd clean: $(basename "$bin")"
    done
    mkdir -p /tmp/run && cd /tmp/run
    /scripts/headless-x.sh /juce /build/ForroBoxTests > suite.log 2>&1 || true
    /scripts/judge-suite.sh suite.log
  '

echo "=== portable build OK — artefacts in $ROOT/$ART"
