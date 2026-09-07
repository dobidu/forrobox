#!/usr/bin/env bash
# ============================================================================
#  Forró Box — Windows x64 VST3 build, driven from WSL
#
#  Uses the Windows CMake and MSVC 2022 through WSL interop. The SOURCE stays on
#  the WSL side and is read over the UNC share (measured: 30 KB in 22 ms, 303
#  files in 216 ms — reads are not the slow part). The BUILD DIRECTORY is on the
#  Windows filesystem, because MSVC's thousands of small artifact writes are what
#  actually crawls over a network share.
#
#  Usage:
#    scripts/build-windows.sh              build and run the tests
#    scripts/build-windows.sh --install    also install to the per-user VST3 dir
# ============================================================================
set -euo pipefail

INSTALL=0
[[ "${1:-}" == "--install" ]] && INSTALL=1

# ── locations ───────────────────────────────────────────────────────────────
DISTRO="${WSL_DISTRO_NAME:-$(wsl.exe -l -q 2>/dev/null | tr -d '\r\0' | head -1)}"
[[ -n "$DISTRO" ]] || { echo "FATAL: cannot determine the WSL distro name"; exit 1; }

CMAKE_EXE="/mnt/c/Program Files/CMake/bin/cmake.exe"
[[ -x "$CMAKE_EXE" ]] || { echo "FATAL: Windows cmake.exe not found at $CMAKE_EXE"; exit 1; }

PROJECT_LINUX="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JUCE_LINUX="${JUCE_PATH:-$HOME/JUCE}"

# The distro name matters: it registers as "Ubuntu-24.04", and \\wsl.localhost\Ubuntu
# silently resolves to nothing.
to_unc() { printf '\\\\wsl.localhost\\%s%s' "$DISTRO" "$(printf '%s' "$1" | tr '/' '\\')"; }
SRC_UNC="$(to_unc "$PROJECT_LINUX")"
JUCE_UNC="$(to_unc "$JUCE_LINUX")"

WIN_BUILD='C:\Users\carlo\forrobox-build'
WIN_BUILD_WSL='/mnt/c/Users/carlo/forrobox-build'
WIN_MIRROR='C:\Users\carlo\forrobox-win'
WIN_MIRROR_WSL='/mnt/c/Users/carlo/forrobox-win'
VST3_DIR_WSL='/mnt/c/Users/carlo/AppData/Local/Programs/Common/VST3'
LOG="$PROJECT_LINUX/scripts/build-windows.log"

run() { echo "+ $*" | tee -a "$LOG"; "$@" 2>&1 | tee -a "$LOG"; return "${PIPESTATUS[0]}"; }

: > "$LOG"
echo "distro:       $DISTRO"        | tee -a "$LOG"
echo "source (UNC): $SRC_UNC"       | tee -a "$LOG"
echo "JUCE   (UNC): $JUCE_UNC"      | tee -a "$LOG"
echo "build dir:    $WIN_BUILD"     | tee -a "$LOG"
echo                                | tee -a "$LOG"

# ── configure, with a documented fallback ───────────────────────────────────
SOURCE_MODE="unc"
if ! run "$CMAKE_EXE" -S "$SRC_UNC" -B "$WIN_BUILD" \
        -G "Visual Studio 17 2022" -A x64 \
        -DJUCE_PATH="$JUCE_UNC" -DFORROBOX_TESTS=ON; then
  echo                                                              | tee -a "$LOG"
  echo "!! UNC-source configure failed — falling back to a mirror." | tee -a "$LOG"
  echo "!! A mirrored build can go stale against the real source,"  | tee -a "$LOG"
  echo "!! so this is reported loudly rather than done silently."   | tee -a "$LOG"
  SOURCE_MODE="mirror"
  mkdir -p "$WIN_MIRROR_WSL"
  rsync -a --delete \
    --exclude 'build*/' --exclude '.git/' --exclude '.paul/' --exclude '.claude/' \
    "$PROJECT_LINUX/" "$WIN_MIRROR_WSL/"
  # JUCE must be reachable from Windows too; mirror it only if the UNC path failed.
  JUCE_ARG="$JUCE_UNC"
  run "$CMAKE_EXE" -S "$WIN_MIRROR" -B "$WIN_BUILD" \
      -G "Visual Studio 17 2022" -A x64 \
      -DJUCE_PATH="$JUCE_ARG" -DFORROBOX_TESTS=ON
fi

# ── build ───────────────────────────────────────────────────────────────────
run "$CMAKE_EXE" --build "$WIN_BUILD" --config Release --parallel

# ── our warnings only; JUCE's are not ours to fix ───────────────────────────
echo                                                        | tee -a "$LOG"
OURS=$(grep -iE 'warning' "$LOG" | grep -E 'src\\|src/|tests\\|tests/' || true)
if [[ -n "$OURS" ]]; then
  echo "WARNINGS FROM OUR SOURCES:"; echo "$OURS"
else
  echo "No warnings originating in src/ or tests/."
fi

# ── run the 01-02 suite under a third compiler ──────────────────────────────
TESTS_EXE=$(find "$WIN_BUILD_WSL" -name 'ForroBoxTests.exe' -type f | head -1)
if [[ -n "$TESTS_EXE" ]]; then
  echo; echo "=== ForroBoxTests under MSVC ==="
  "$TESTS_EXE" | tee -a "$LOG"
else
  echo "FATAL: ForroBoxTests.exe not found under $WIN_BUILD_WSL"; exit 1
fi

# ── locate the built bundle ─────────────────────────────────────────────────
BUNDLE=$(find "$WIN_BUILD_WSL" -type d -name 'ForroBox.vst3' | head -1)
[[ -n "$BUNDLE" ]] || { echo "FATAL: no ForroBox.vst3 bundle produced"; exit 1; }
DLL="$BUNDLE/Contents/x86_64-win/ForroBox.vst3"
echo; echo "bundle: $BUNDLE"
file "$DLL" | sed 's/^/  /'

# ── install (opt-in) ────────────────────────────────────────────────────────
if [[ "$INSTALL" == "1" ]]; then
  echo; echo "=== install to the per-user VST3 folder ==="
  # Never C:\Program Files\Common Files\VST3 — that needs elevation.
  mkdir -p "$VST3_DIR_WSL"
  rm -rf "$VST3_DIR_WSL/ForroBox.vst3"      # replace, never merge into a stale bundle
  cp -r "$BUNDLE" "$VST3_DIR_WSL/"
  INSTALLED="$VST3_DIR_WSL/ForroBox.vst3/Contents/x86_64-win/ForroBox.vst3"
  a=$(sha256sum "$DLL"       | cut -d' ' -f1)
  b=$(sha256sum "$INSTALLED" | cut -d' ' -f1)
  echo "  built:     $a"
  echo "  installed: $b"
  [[ "$a" == "$b" ]] && echo "  hashes match" || { echo "  FATAL: hash mismatch"; exit 1; }
  file "$INSTALLED" | sed 's/^/  /'

  MI="$VST3_DIR_WSL/ForroBox.vst3/Contents/Resources/moduleinfo.json"
  python3 - "$MI" <<'PYEOF'
import sys
d = open(sys.argv[1], 'rb').read()
nonascii = sum(1 for b in d if b > 0x7F)
print(f"  moduleinfo: 'Forro Box' present: {b'Forro Box' in d}")
print(f"  moduleinfo: non-ASCII bytes: {nonascii}")
print(f"  moduleinfo: old corruption sequence present: {bytes([0xef,0xbf,0x83]) in d}")
PYEOF
fi

echo; echo "source mode: $SOURCE_MODE"
echo "log: $LOG"
