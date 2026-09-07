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

CONFIG=Release          # every artifact lookup is scoped to this, never "whatever
                        # find hits first" — a stale Debug build must not be able
                        # to masquerade as the verified one.

# ── arguments ───────────────────────────────────────────────────────────────
INSTALL=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --install) INSTALL=1 ;;
    -h|--help) sed -n '2,14p' "$0"; exit 0 ;;
    *) echo "FATAL: unrecognised argument '$1'. A dropped flag must not look like success." >&2
       exit 2 ;;
  esac
  shift
done

# ── toolchain ───────────────────────────────────────────────────────────────
# MSBuild and cl.exe here are pt-BR: a localized cl.exe emits "aviso C4996",
# not "warning C4996", so an English-only grep would report a clean build while
# real warnings scrolled past. Force English diagnostics AND match both anyway.
export VSLANG=1033

CMAKE_EXE="/mnt/c/Program Files/CMake/bin/cmake.exe"
[[ -x "$CMAKE_EXE" ]] || { echo "FATAL: Windows cmake.exe not found at $CMAKE_EXE" >&2; exit 1; }

DISTRO="${WSL_DISTRO_NAME:-}"
[[ -n "$DISTRO" ]] || { echo "FATAL: WSL_DISTRO_NAME is unset" >&2; exit 1; }

PROJECT_LINUX="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JUCE_LINUX="${JUCE_PATH:-$HOME/JUCE}"

# ── path translation ────────────────────────────────────────────────────────
# A /mnt/<drive>/ path is already on Windows; routing it through the UNC share
# would silently push every read back over the network the design avoids.
to_win() {
  local p="$1"
  if [[ "$p" =~ ^/mnt/([a-z])(/.*)?$ ]]; then
    local drive="${BASH_REMATCH[1]}" rest="${BASH_REMATCH[2]:-/}"
    printf '%s:%s' "$(printf '%s' "$drive" | tr 'a-z' 'A-Z')" "$(printf '%s' "$rest" | tr '/' '\\')"
  else
    printf '\\\\wsl.localhost\\%s%s' "$DISTRO" "$(printf '%s' "$p" | tr '/' '\\')"
  fi
}

# Windows-side locations, derived rather than hardcoded: a hardcoded username
# makes --install copy into a directory no DAW scans, on any other machine,
# while still exiting 0.
WIN_USERPROFILE_RAW="$(cmd.exe /c 'echo %USERPROFILE%' 2>/dev/null | tr -d '\r\n')"
WIN_LOCALAPPDATA_RAW="$(cmd.exe /c 'echo %LOCALAPPDATA%' 2>/dev/null | tr -d '\r\n')"
[[ "$WIN_USERPROFILE_RAW" == *:* && "$WIN_LOCALAPPDATA_RAW" == *:* ]] \
  || { echo "FATAL: could not resolve Windows user paths via interop" >&2; exit 1; }

USERPROFILE_WSL="$(wslpath -u "$WIN_USERPROFILE_RAW")"
APPDATA_WSL="$(wslpath -u "$(cmd.exe /c 'echo %APPDATA%' 2>/dev/null | tr -d '\r\n')")"

# ── where does the host ACTUALLY look? ──────────────────────────────────────
#  Do not assume. The VST3 convention permits %LOCALAPPDATA%\Programs\Common\VST3,
#  and installing there worked perfectly — into a directory Ableton never reads.
#  Live logs every folder it scans in PluginScanner.txt, so ask it instead of
#  guessing. A writable "(custom)" entry is preferred: the global folder is
#  under C:\Program Files and needs elevation.
discover_vst3_dir() {
  local newest scanned line win
  newest=$(find "$APPDATA_WSL/Ableton" -maxdepth 3 -name 'PluginScanner.txt' -printf '%T@ %p\n' 2>/dev/null \
           | sort -rn | head -1 | cut -d' ' -f2-)
  [[ -n "$newest" ]] || return 1
  # last scan pass only, so a folder the user has since removed is not resurrected
  scanned=$(grep -a 'VST3: scanning plugins in' "$newest" | tail -20)
  while IFS= read -r line; do
    win=$(printf '%s' "$line" | sed -n 's/.*scanning plugins in "\([^"]*\)".*/\1/p')
    [[ -n "$win" ]] || continue
    local wsl; wsl=$(wslpath -u "$win" 2>/dev/null) || continue
    [[ -d "$wsl" && -w "$wsl" ]] || continue          # skip Program Files: not writable
    printf '%s' "$wsl"; return 0
  done <<< "$(printf '%s\n' "$scanned" | grep '(custom)' ; printf '%s\n' "$scanned" | grep -v '(custom)')"
  return 1
}

if [[ -n "${FORROBOX_VST3_DIR:-}" ]]; then
  VST3_DIR_WSL="$FORROBOX_VST3_DIR"; VST3_SRC="override"
elif VST3_DIR_WSL="$(discover_vst3_dir)"; then
  VST3_SRC="discovered from the host's scanner record"
else
  VST3_DIR_WSL="$(wslpath -u "$WIN_LOCALAPPDATA_RAW")/Programs/Common/VST3"
  VST3_SRC="FALLBACK — host scan folders unknown; the host may not read this"
fi

# Separate build directories per source mode. Reusing one makes CMake refuse the
# fallback outright ("does not match the source used to generate cache"), which
# would break the fallback in exactly the case it exists for.
BUILD_UNC_WSL="$USERPROFILE_WSL/forrobox-build-unc"
BUILD_MIRROR_WSL="$USERPROFILE_WSL/forrobox-build-mirror"
MIRROR_SRC_WSL="$USERPROFILE_WSL/forrobox-src-mirror"
MIRROR_JUCE_WSL="$USERPROFILE_WSL/forrobox-juce-mirror"

LOG="$PROJECT_LINUX/scripts/build-windows.log"
run() { echo "+ $*" | tee -a "$LOG"; "$@" 2>&1 | tee -a "$LOG"; return "${PIPESTATUS[0]}"; }

: > "$LOG"
{ echo "distro:        $DISTRO"
  echo "source:        $(to_win "$PROJECT_LINUX")"
  echo "JUCE:          $(to_win "$JUCE_LINUX")"
  echo "config:        $CONFIG"
  echo "VST3 install:  $VST3_DIR_WSL"
  echo "               ($VST3_SRC)"
  echo; } | tee -a "$LOG"

# ── configure ───────────────────────────────────────────────────────────────
SOURCE_MODE="unc"
BUILD_WSL="$BUILD_UNC_WSL"
if ! run "$CMAKE_EXE" -S "$(to_win "$PROJECT_LINUX")" -B "$(to_win "$BUILD_UNC_WSL")" \
        -G "Visual Studio 17 2022" -A x64 \
        -DJUCE_PATH="$(to_win "$JUCE_LINUX")" -DFORROBOX_TESTS=ON; then
  { echo
    echo "!! UNC-source configure failed. Falling back to a local mirror."
    echo "!! Reported loudly, not switched silently: a mirrored build can go"
    echo "!! stale against the real source."; } | tee -a "$LOG"
  SOURCE_MODE="mirror"
  BUILD_WSL="$BUILD_MIRROR_WSL"

  # The fallback exists because Windows could not read the share, so JUCE has to
  # be mirrored too — passing the same UNC path would fail for the same reason.
  mkdir -p "$MIRROR_SRC_WSL" "$MIRROR_JUCE_WSL"
  rsync -a --delete --exclude 'build*/' --exclude '.git/' --exclude '.paul/' \
        --exclude '.claude/' "$PROJECT_LINUX/" "$MIRROR_SRC_WSL/"
  rsync -a --delete "$JUCE_LINUX/" "$MIRROR_JUCE_WSL/"

  run "$CMAKE_EXE" -S "$(to_win "$MIRROR_SRC_WSL")" -B "$(to_win "$BUILD_MIRROR_WSL")" \
      -G "Visual Studio 17 2022" -A x64 \
      -DJUCE_PATH="$(to_win "$MIRROR_JUCE_WSL")" -DFORROBOX_TESTS=ON
fi

# ── build ───────────────────────────────────────────────────────────────────
run "$CMAKE_EXE" --build "$(to_win "$BUILD_WSL")" --config "$CONFIG" --parallel

# ── warnings, in three honest buckets ───────────────────────────────────────
#  "src/" alone is not "ours": JUCE vendors LV2_SDK/{serd,sord,sratom,lilv}/src
#  and oboe/src, so an unanchored match would file JUCE's warnings under ours.
echo | tee -a "$LOG"
ALL=$(grep -inE '(warning|aviso) [A-Z]+[0-9]+' "$LOG" || true)
OURS=$(printf '%s\n' "$ALL" | grep -iE 'forrobox[\\/](src|tests)[\\/]' || true)
BUILDSYS=$(printf '%s\n' "$ALL" | grep -oiE 'MSB[0-9]+' | sort | uniq -c || true)

echo "=== warnings ==="
echo "  total:            $(printf '%s\n' "$ALL" | grep -c . || true)"
echo "  from our sources: $(printf '%s\n' "$OURS" | grep -c . || true)"
[[ -n "$OURS" ]] && { echo "  --- ours (must be fixed):"; printf '%s\n' "$OURS" | sed 's/^/    /'; }
[[ -n "$BUILDSYS" ]] && {
  echo "  --- build-system (not from our code, but not filtered away either):"
  printf '%s\n' "$BUILDSYS" | sed 's/^/    /'
  echo "    MSB8064 records dependency paths lowercased; the WSL filesystem is"
  echo "    case-sensitive, so it warns incremental builds may misbehave."; }

# ── run the 01-02 suite under a third compiler ──────────────────────────────
# -print -quit, not | head -1: find gets SIGPIPE on its second write once head
# exits, the substitution yields 141, and pipefail+errexit kill the script right
# after a successful build with no diagnostic.
TESTS_EXE=$(find "$BUILD_WSL/$CONFIG" -name 'ForroBoxTests.exe' -type f -print -quit 2>/dev/null || true)
[[ -n "$TESTS_EXE" ]] || { echo "FATAL: no ForroBoxTests.exe under $BUILD_WSL/$CONFIG" >&2; exit 1; }
echo; echo "=== ForroBoxTests under MSVC ($CONFIG) ==="
"$TESTS_EXE" | tee -a "$LOG"

# ── locate the built bundle, scoped to the config we just built ─────────────
BUNDLE="$BUILD_WSL/ForroBox_artefacts/$CONFIG/VST3/ForroBox.vst3"
[[ -d "$BUNDLE" ]] || { echo "FATAL: no $CONFIG VST3 bundle at $BUNDLE" >&2; exit 1; }
DLL=$(find "$BUNDLE/Contents" -name 'ForroBox.vst3' -type f -print -quit 2>/dev/null || true)
[[ -n "$DLL" ]] || { echo "FATAL: no binary inside $BUNDLE/Contents" >&2; exit 1; }
echo; echo "bundle: $BUNDLE"; file "$DLL" | sed 's/^/  /'

# ── install (opt-in) ────────────────────────────────────────────────────────
if [[ "$INSTALL" == "1" ]]; then
  echo; echo "=== install to the host's VST3 folder ==="
  # Never C:\Program Files\Common Files\VST3 — that needs elevation.
  echo "  target: $VST3_DIR_WSL"
  echo "  source: $VST3_SRC"
  [[ "$VST3_SRC" == FALLBACK* ]] && echo "  !! the host may not scan this directory"

  mkdir -p "$VST3_DIR_WSL"
  rm -rf "$VST3_DIR_WSL/ForroBox.vst3"      # replace, never merge into a stale bundle
  cp -r "$BUNDLE" "$VST3_DIR_WSL/"

  # Sweep any copy left in a directory the host does not scan. Two installed
  # copies means the next "it didn't pick up my change" is unattributable.
  STALE="$(wslpath -u "$WIN_LOCALAPPDATA_RAW")/Programs/Common/VST3/ForroBox.vst3"
  if [[ -d "$STALE" && "$STALE" != "$VST3_DIR_WSL/ForroBox.vst3" ]]; then
    echo "  removing stale copy in an unscanned directory: $STALE"
    rm -rf "$STALE"
  fi

  INSTALLED=$(find "$VST3_DIR_WSL/ForroBox.vst3/Contents" -name 'ForroBox.vst3' -type f -print -quit)
  a=$(sha256sum "$DLL" | cut -d' ' -f1); b=$(sha256sum "$INSTALLED" | cut -d' ' -f1)
  echo "  built:     $a"
  echo "  installed: $b"
  [[ "$a" == "$b" ]] || { echo "  FATAL: hash mismatch" >&2; exit 1; }
  echo "  hashes match"
  file "$INSTALLED" | sed 's/^/  /'

  # This asserts. Printing "corruption present: True" and exiting 0 would let the
  # exact regression CMakeLists.txt documents slip through as a pass.
  python3 - "$VST3_DIR_WSL/ForroBox.vst3/Contents/Resources/moduleinfo.json" <<'PYEOF'
import sys, os
path = sys.argv[1]
if not os.path.isfile(path):
    print(f"  FATAL: moduleinfo.json missing at {path}"); sys.exit(1)
d = open(path, 'rb').read()
nonascii = [b for b in d if b > 0x7F]
corrupt  = bytes([0xEF, 0xBF, 0x83]) in d
name     = b'Forro Box' in d
print(f"  moduleinfo: 'Forro Box' present:  {name}")
print(f"  moduleinfo: non-ASCII bytes:      {len(nonascii)}")
print(f"  moduleinfo: corruption sequence:  {corrupt}")
if not name or nonascii or corrupt:
    print("  FATAL: moduleinfo regression"); sys.exit(1)
print("  moduleinfo clean")
PYEOF
fi

echo; echo "source mode: $SOURCE_MODE"; echo "log: $LOG"
