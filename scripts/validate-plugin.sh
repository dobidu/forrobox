#!/usr/bin/env bash
# ============================================================================
#  Forró Box — pluginval, as a gate whose verdict is its own
#
#  Fetches pluginval (pinned by version AND hash, never committed: it lives in
#  the gitignored build/tools/) and validates the VST3 at strictness 10 with its
#  GUI tests.
#
#  Usage:
#    scripts/validate-plugin.sh                    Linux Debug + Linux Release
#    scripts/validate-plugin.sh --skip-gui         the same, without a display
#    scripts/validate-plugin.sh --windows BUNDLE   one Windows bundle (a WSL
#                                                  path); build-windows.sh calls it
#
#  Environment:
#    JUCE_PATH   JUCE checkout for configuring build-debug (default: build-linux's)
#
#  A run PASSES only when pluginval exits 0 AND its log holds no failed test, no
#  "JUCE Assertion failure" and no "*** Leaked objects". pluginval's own verdict
#  is not enough: measured at Phase 11 planning, it exits 0 and prints SUCCESS
#  over a Debug build logging 500 assertions and a leak at unload.
# ============================================================================
set -euo pipefail

VERSION=1.0.4
STRICTNESS=10
TIMEOUT_MS=300000   # per-test silence limit; Debug is ~4x slower than Release

# pluginval publishes no checksums, so these are the hashes of the zips as first
# downloaded (2026-09-30). A different file under the same URL is REFUSED.
declare -A URL=(
  [linux]="https://github.com/Tracktion/pluginval/releases/download/v$VERSION/pluginval_Linux.zip"
  [windows]="https://github.com/Tracktion/pluginval/releases/download/v$VERSION/pluginval_Windows.zip")
declare -A SHA256=(
  [linux]=c01c49d8063965c4c2dea8324468336768f5c9139e0b1caebde14c2400b55352
  [windows]=c08e61ce3b96db41636f8ec7e76f4c7e2c13ebdac7fa1b5a1f52b4f32ec715ab)

PROJECT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOLS="$PROJECT/build/tools/pluginval-$VERSION"
LOGS="$PROJECT/build/pluginval"

# ── arguments ───────────────────────────────────────────────────────────────
SKIP_GUI=0
WINDOWS_BUNDLE=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --skip-gui) SKIP_GUI=1 ;;
    --windows)  [[ $# -ge 2 ]] || { echo "FATAL: --windows needs a bundle path" >&2; exit 2; }
                WINDOWS_BUNDLE="$2"; shift ;;
    -h|--help)  sed -n '2,23p' "$0"; exit 0 ;;
    *) echo "FATAL: unrecognised argument '$1'. A dropped flag must not look like success." >&2
       exit 2 ;;
  esac
  shift
done

# ── fetch: the binary is only ever run from a zip whose hash was checked ────
# Echoes the unpacked directory.
fetch_pluginval() {
  local platform="$1" zip="$TOOLS/pluginval_${1^}.zip" dir="$TOOLS/$1" have
  mkdir -p "$TOOLS"

  if [[ ! -f "$zip" ]]; then
    echo "fetching pluginval $VERSION ($platform)" >&2
    curl -fsSL -o "$zip.part" "${URL[$platform]}"
    mv "$zip.part" "$zip"
  fi

  have=$(sha256sum "$zip" | cut -d' ' -f1)
  if [[ "$have" != "${SHA256[$platform]}" ]]; then
    echo "FATAL: $zip does not match the pinned hash — refusing to run it." >&2
    echo "  pinned: ${SHA256[$platform]}" >&2
    echo "  got:    $have" >&2
    rm -f "$zip"; rm -rf "$dir"
    exit 1
  fi

  # Unpacked copy keyed to the verified hash, so a replaced zip cannot leave a
  # stale binary behind that was never checked.
  if [[ ! -f "$dir/.sha256" || "$(cat "$dir/.sha256")" != "$have" ]]; then
    rm -rf "$dir"; mkdir -p "$dir"
    (cd "$dir" && unzip -oq "$zip")
    echo "$have" > "$dir/.sha256"
  fi
  printf '%s' "$dir"
}

# ── judge: one place for the rules ──────────────────────────────────────────
# judge <label> <log> <exit code>; returns 0 for PASS.
judge() {
  local label="$1" log="$2" rc="$3" clean failed asserts leaks seed verdict=PASS
  clean="$(mktemp)"; tr -d '\r' < "$log" > "$clean"   # the Windows log is CRLF

  failed=$(grep -cE '^!!! Test .* failed|FAILED!!' "$clean" || true)
  asserts=$(grep -c 'JUCE Assertion failure' "$clean" || true)
  leaks=$(grep -c '\*\*\* Leaked objects' "$clean" || true)
  seed=$(grep -m1 -o 'Random seed: [^ ]*' "$clean" | cut -d' ' -f3 || true)

  (( rc == 0 && failed == 0 && asserts == 0 && leaks == 0 )) || verdict=FAIL

  echo "── pluginval: $label"
  echo "   verdict:    $verdict"
  echo "   exit code:  $rc   (pluginval's own; not sufficient on its own)"
  echo "   strictness: $STRICTNESS   gui tests: $([[ $SKIP_GUI == 1 ]] && echo SKIPPED || echo on)   seed: ${seed:-?}"
  echo "   log:        $log"
  if [[ $verdict == FAIL ]]; then
    (( failed == 0 ))  || { echo "   failed tests: $failed"; grep -E '^!!! Test .* failed|FAILED!!' "$clean" | sort | uniq -c | sed 's/^/     /'; }
    (( asserts == 0 )) || { echo "   JUCE assertions: $asserts"
                            grep -o 'JUCE Assertion failure in [^ ]*' "$clean" | sed 's/JUCE Assertion failure in //' \
                              | sort | uniq -c | sort -rn | sed 's/^/     /'; }
    (( leaks == 0 ))   || { echo "   leaks at unload:"; grep '\*\*\* Leaked objects' "$clean" | sed 's/^/     /'; }
  fi
  rm -f "$clean"
  [[ $verdict == PASS ]]
}

gui_args() { [[ $SKIP_GUI == 1 ]] && echo --skip-gui-tests || true; }

# validate <label> <bundle as the validator sees it> <pluginval> ; returns the verdict.
validate() {
  local label="$1" bundle="$2" exe="$3" log rc=0
  mkdir -p "$LOGS"
  log="$LOGS/$label-$(date +%Y%m%d-%H%M%S).log"
  # Captured to a file, never piped: build-windows.sh's 08-02 finding.
  "$exe" --strictness-level "$STRICTNESS" --timeout-ms "$TIMEOUT_MS" $(gui_args) \
         --validate "$bundle" > "$log" 2>&1 || rc=$?
  judge "$label" "$log" "$rc"
}

# ── Windows: one bundle, from a Windows-local copy of the validator ─────────
if [[ -n "$WINDOWS_BUNDLE" ]]; then
  [[ -d "$WINDOWS_BUNDLE" ]] || echo "WARNING: no bundle at $WINDOWS_BUNDLE — pluginval will fail on it" >&2
  dir="$(fetch_pluginval windows)"
  # Not run over the UNC share, and never under C:\Program Files.
  profile="$(wslpath -u "$(cmd.exe /c 'echo %USERPROFILE%' 2>/dev/null | tr -d '\r')")"
  win_tools="$profile/forrobox-tools/pluginval-$VERSION"
  mkdir -p "$win_tools"
  cmp -s "$dir/pluginval.exe" "$win_tools/pluginval.exe" || cp "$dir/pluginval.exe" "$win_tools/pluginval.exe"
  validate windows-release "$(wslpath -w "$WINDOWS_BUNDLE" 2>/dev/null || echo "$WINDOWS_BUNDLE")" \
           "$win_tools/pluginval.exe"
  exit $?
fi

# ── Linux: Debug (where assertions exist) and Release (what ships) ─────────
if [[ $SKIP_GUI == 0 && -z "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]]; then
  echo "FATAL: no display, so pluginval's Editor tests cannot run. Pass --skip-gui to run" >&2
  echo "       without them — the verdict will say so." >&2
  exit 1
fi

dir="$(fetch_pluginval linux)"

JUCE_DIR="${JUCE_PATH:-$(sed -n 's/^JUCE_PATH:PATH=//p' "$PROJECT/build-linux/CMakeCache.txt" 2>/dev/null)}"
if [[ ! -f "$PROJECT/build-debug/CMakeCache.txt" ]]; then
  echo "configuring build-debug (JUCE_PATH=${JUCE_DIR:-<FetchContent>})"
  cmake -S "$PROJECT" -B "$PROJECT/build-debug" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
        ${JUCE_DIR:+-DJUCE_PATH="$JUCE_DIR"} > /dev/null
fi

mkdir -p "$LOGS"
FAILURES=0
for tree in debug:Debug linux:Release; do
  name="${tree%%:*}" config="${tree##*:}" build="$PROJECT/build-${tree%%:*}"
  echo; echo "=== build $name ($config)"
  cmake --build "$build" --target ForroBox_VST3 > "$LOGS/build-$name.log" 2>&1 \
    || { echo "FATAL: ForroBox_VST3 did not build in $build — see $LOGS/build-$name.log" >&2; exit 1; }
  bundle="$build/ForroBox_artefacts/$config/VST3/ForroBox.vst3"
  validate "linux-${config,,}" "$bundle" "$dir/pluginval" || FAILURES=$((FAILURES + 1))
done

echo
if (( FAILURES > 0 )); then
  echo "pluginval gate: FAILED ($FAILURES of 2 targets)"
  exit 1
fi
echo "pluginval gate: PASSED (2 of 2 targets)"
