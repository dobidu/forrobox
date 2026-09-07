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
#    scripts/build-windows.sh --install    also install where the host scans
#
#  Environment:
#    FORROBOX_VST3_DIR   install target override, wins over host discovery.
#                        Needed for any host other than Ableton, since discovery
#                        reads Ableton's PluginScanner.txt.
#    JUCE_PATH           JUCE checkout (default ~/JUCE)
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

PROJECT_LINUX="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JUCE_LINUX="${JUCE_PATH:-$HOME/JUCE}"

# ── path translation ────────────────────────────────────────────────────────
#  wslpath -w already does Linux->Windows exactly right: /mnt/<drive> becomes a
#  drive letter, anything else becomes a \\wsl.localhost\<distro> UNC path, and it
#  works for paths that do not exist yet (the build dirs at configure time) and
#  for paths with spaces. It also resolves the distro itself, so nothing here
#  needs WSL_DISTRO_NAME.
#  Conversions are cached in variables: wslpath is a fork, and these paths are
#  fixed for the run.

# Windows-side locations, derived rather than hardcoded: a hardcoded username
# makes --install copy into a directory no DAW scans, on any other machine,
# while still exiting 0.
# One cmd.exe spawn, not three: each spawn costs ~200 ms of interop.
mapfile -t _WIN_VARS < <(cmd.exe /c 'echo %USERPROFILE%&echo %LOCALAPPDATA%&echo %APPDATA%' 2>/dev/null | tr -d '\r')
WIN_USERPROFILE_RAW="${_WIN_VARS[0]:-}"
WIN_LOCALAPPDATA_RAW="${_WIN_VARS[1]:-}"
WIN_APPDATA_RAW="${_WIN_VARS[2]:-}"
[[ "$WIN_USERPROFILE_RAW" == *:* && "$WIN_LOCALAPPDATA_RAW" == *:* && "$WIN_APPDATA_RAW" == *:* ]] \
  || { echo "FATAL: could not resolve Windows user paths via interop" >&2; exit 1; }

USERPROFILE_WSL="$(wslpath -u "$WIN_USERPROFILE_RAW")"
APPDATA_WSL="$(wslpath -u "$WIN_APPDATA_RAW")"

# ── S1: computed once; the fallback target and the stale sweep must agree ────
FALLBACK_VST3_DIR_WSL="$(wslpath -u "$WIN_LOCALAPPDATA_RAW")/Programs/Common/VST3"

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
  VST3_DIR_WSL="$FALLBACK_VST3_DIR_WSL"
  VST3_SRC="FALLBACK — host scan folders unknown; the host may not read this"
fi

# Separate build directories per source mode. Reusing one makes CMake refuse the
# fallback outright ("does not match the source used to generate cache"), which
# would break the fallback in exactly the case it exists for.
BUILD_UNC_WSL="$USERPROFILE_WSL/forrobox-build-unc"
BUILD_MIRROR_WSL="$USERPROFILE_WSL/forrobox-build-mirror"
MIRROR_SRC_WSL="$USERPROFILE_WSL/forrobox-src-mirror"
MIRROR_JUCE_WSL="$USERPROFILE_WSL/forrobox-juce-mirror"

PROJECT_WIN="$(wslpath -w "$PROJECT_LINUX")"
JUCE_WIN="$(wslpath -w "$JUCE_LINUX")"

# Shared configure flags in one place. The fallback branch is rarely exercised,
# so a flag added to the primary call and forgotten in the fallback would make
# the two source modes build different things with nothing to catch the drift.
COMMON_CMAKE_ARGS=(-G "Visual Studio 17 2022" -A x64 -DFORROBOX_TESTS=ON)

LOG="$PROJECT_LINUX/scripts/build-windows.log"
# The group's status is that of "$@", so PIPESTATUS[0] is still the real exit code.
run() { { echo "+ $*"; "$@"; } 2>&1 | tee -a "$LOG"; return "${PIPESTATUS[0]}"; }

: > "$LOG"
{ echo "distro:        ${WSL_DISTRO_NAME:-<resolved by wslpath>}"
  echo "source:        $PROJECT_WIN"
  echo "JUCE:          $JUCE_WIN"
  echo "config:        $CONFIG"
  if [[ "$INSTALL" == "1" ]]; then
    echo "VST3 install:  $VST3_DIR_WSL"
    echo "               ($VST3_SRC)"
  else
    echo "VST3 install:  no (pass --install; target would be $VST3_DIR_WSL)"
  fi
  echo; } | tee -a "$LOG"

# ── configure ───────────────────────────────────────────────────────────────
SOURCE_MODE="unc"
BUILD_WSL="$BUILD_UNC_WSL"
SRC_USED_WIN="$PROJECT_WIN"
if ! run "$CMAKE_EXE" -S "$PROJECT_WIN" -B "$(wslpath -w "$BUILD_UNC_WSL")" \
        "${COMMON_CMAKE_ARGS[@]}" -DJUCE_PATH="$JUCE_WIN"; then
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

  SRC_USED_WIN="$(wslpath -w "$MIRROR_SRC_WSL")"
  run "$CMAKE_EXE" -S "$SRC_USED_WIN" -B "$(wslpath -w "$BUILD_MIRROR_WSL")" \
      "${COMMON_CMAKE_ARGS[@]}" -DJUCE_PATH="$(wslpath -w "$MIRROR_JUCE_WSL")"
fi

# ── build ───────────────────────────────────────────────────────────────────
run "$CMAKE_EXE" --build "$(wslpath -w "$BUILD_WSL")" --config "$CONFIG" --parallel

# ── warnings, in three honest buckets ───────────────────────────────────────
#  "src/" alone is not "ours": JUCE vendors LV2_SDK/{serd,sord,sratom,lilv}/src
#  and oboe/src, so an unanchored match would file JUCE's warnings under ours.
echo | tee -a "$LOG"

# Match on the DIAGNOSTIC CODE, not the severity word. This toolchain is pt-BR,
# where cl.exe says "aviso C4996" — an English-only grep reported a clean build
# while 17 warnings existed. Codes are locale-invariant; VSLANG above only keeps
# the log readable.
ALL=$(grep -inE '\b(C[0-9]{4}|MSB[0-9]{4}|LNK[0-9]{4})\b' "$LOG" || true)

# Anchor on the source path actually configured, not a literal "forrobox": the
# checkout can live in any directory (a fork, a worktree, a CI checkout), and a
# stale literal would make this bucket silently empty. -iF because MSBuild
# lowercases some paths, and to avoid escaping backslashes into a regex.
OURS=$(printf '%s\n' "$ALL" | grep -iF -e "${SRC_USED_WIN}\\src\\" -e "${SRC_USED_WIN}\\tests\\" || true)
BUILDSYS=$(printf '%s\n' "$ALL" | grep -oiE 'MSB[0-9]+' | sort | uniq -c || true)

echo "=== diagnostics ==="
echo "  total:            $(printf '%s\n' "$ALL" | grep -c . || true)"
echo "  from our sources: $(printf '%s\n' "$OURS" | grep -c . || true)"
echo "  anchored on:      ${SRC_USED_WIN}\\{src,tests}\\"
[[ -n "$OURS" ]] && { echo "  --- ours (must be fixed):"; printf '%s\n' "$OURS" | sed 's/^/    /'; }
[[ -n "$BUILDSYS" ]] && {
  echo "  --- build-system (not our code, but not filtered away either):"
  printf '%s\n' "$BUILDSYS" | sed 's/^/    /'
  echo "    MSB8064 records dependency paths lowercased; the WSL filesystem is"
  echo "    case-sensitive, so it warns incremental builds may misbehave."; }

# ── run the 01-02 suite under a third compiler ──────────────────────────────
# -print -quit, not | head -1: find gets SIGPIPE on its second write once head
# exits, the substitution yields 141, and pipefail+errexit kill the script right
# after a successful build with no diagnostic.
mapfile -t TEST_EXES < <(find "$BUILD_WSL/$CONFIG" -name 'ForroBox*Tests.exe' -type f | sort)
(( ${#TEST_EXES[@]} > 0 )) || { echo "FATAL: no ForroBox*Tests.exe under $BUILD_WSL/$CONFIG" >&2; exit 1; }

# Guard against the reverse failure: a renamed or dropped target would silently
# shrink the set and still look green. CMake defines two test targets.
EXPECTED_TEST_EXES=2
(( ${#TEST_EXES[@]} == EXPECTED_TEST_EXES )) || {
  echo "FATAL: expected $EXPECTED_TEST_EXES test executables, found ${#TEST_EXES[@]}:" >&2
  printf '  %s\n' "${TEST_EXES[@]}" >&2
  exit 1
}

for TESTS_EXE in "${TEST_EXES[@]}"; do
  echo; echo "=== $(basename "$TESTS_EXE" .exe) under MSVC ($CONFIG) ==="
  "$TESTS_EXE" | tee -a "$LOG"
done

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

  # Refuse BEFORE destroying anything. Windows locks a loaded DLL, so with the
  # plugin open in a host the removal fails partway and leaves a bundle with a
  # binary but no moduleinfo.json — broken, and broken silently.
  TARGET_DLL="$VST3_DIR_WSL/ForroBox.vst3/Contents/x86_64-win/ForroBox.vst3"
  if [[ -f "$TARGET_DLL" ]] && ! python3 -c "
import sys
try:
    open(sys.argv[1], 'r+b').close()
except OSError:
    sys.exit(1)
" "$TARGET_DLL" 2>/dev/null; then
    echo "  FATAL: the installed binary is locked by a running process." >&2
    echo "         A host has the plugin loaded. Close it (or remove the plugin" >&2
    echo "         from the track) and re-run. Nothing was modified." >&2
    HOLDER=$(powershell.exe -NoProfile -Command \
      "Get-Process | Where-Object {\$_.ProcessName -match 'Live|Ableton|Reaper|Bitwig|Cubase'} | ForEach-Object { \$_.ProcessName }" \
      2>/dev/null | tr -d '\r' | sort -u | paste -sd, | sed 's/,/, /g')
    [[ -n "$HOLDER" ]] && echo "         Likely holder: $HOLDER" >&2
    exit 1
  fi

  rm -rf "$VST3_DIR_WSL/ForroBox.vst3"      # replace, never merge into a stale bundle
  cp -r "$BUNDLE" "$VST3_DIR_WSL/"

  # Sweep any copy left in a directory the host does not scan. Two installed
  # copies means the next "it didn't pick up my change" is unattributable.
  STALE="$FALLBACK_VST3_DIR_WSL/ForroBox.vst3"
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
