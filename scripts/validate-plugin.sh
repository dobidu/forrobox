#!/usr/bin/env bash
# ============================================================================
#  Forró Box — pluginval, as a gate whose verdict is its own
#
#  Fetches pluginval (pinned by version AND hash, never committed: it lives in
#  the gitignored build/tools/) and validates the VST3 at strictness 10 with its
#  GUI tests.
#
#  Usage:
#    scripts/validate-plugin.sh                    Linux Debug + Linux Release, and
#                                                  the whole suite in Debug
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
# judge <kind> <label> <log> <exit code> <failed-test pattern>; returns 0 for PASS.
# The same rules for a pluginval run and for the Debug suite: a non-zero exit, a
# failed test, a JUCE assertion or a leak report anywhere in the log is a FAIL.
# The log, not the exit code alone, because the suite's teardown and JUCE's
# shutdown write only to stderr. Every grep tolerates finding nothing: under
# pipefail a clean run must not look like a failure.
judge() {
  local kind="$1" label="$2" log="$3" rc="$4" failed_pattern="$5"
  local clean failed asserts leaks verdict=PASS
  clean="$(mktemp)"; tr -d '\r' < "$log" > "$clean"   # the Windows log is CRLF

  failed=$(grep -cE "$failed_pattern" "$clean" || true)
  asserts=$(grep -c 'JUCE Assertion failure' "$clean" || true)
  leaks=$(grep -c '\*\*\* Leaked objects' "$clean" || true)

  (( rc == 0 && failed == 0 && asserts == 0 && leaks == 0 )) || verdict=FAIL

  echo "── $kind: $label"
  echo "   verdict:    $verdict"
  if [[ $kind == pluginval ]]; then
    local seed; seed=$(grep -m1 -o 'Random seed: [^ ]*' "$clean" | cut -d' ' -f3 || true)
    echo "   exit code:  $rc   (pluginval's own; not sufficient on its own)"
    echo "   strictness: $STRICTNESS   gui tests: $([[ $SKIP_GUI == 1 ]] && echo SKIPPED || echo on)   seed: ${seed:-?}"
  else
    echo "   exit code:  $rc"
    { grep -E '^assertions: |checks passed' "$clean" || true; } | sed 's/^/   /'
  fi
  echo "   log:        $log"
  if [[ $verdict == FAIL ]]; then
    (( failed == 0 ))  || { echo "   failed: $failed"
                            { grep -E "$failed_pattern" "$clean" || true; } | sort | uniq -c | head -20 | sed 's/^/     /'; }
    (( asserts == 0 )) || { echo "   JUCE assertions in the log: $asserts"
                            { grep -o 'JUCE Assertion failure in [^ ]*' "$clean" || true; } \
                              | sed 's/JUCE Assertion failure in //' | sort | uniq -c | sort -rn | sed 's/^/     /'; }
    (( leaks == 0 ))   || { echo "   leaks at unload:"; { grep '\*\*\* Leaked objects' "$clean" || true; } | sed 's/^/     /'; }
  fi
  rm -f "$clean"
  [[ $verdict == PASS ]]
}

PLUGINVAL_FAILED='^!!! Test .* failed|FAILED!!'
SUITE_FAILED='^  FAIL  '

GUI_ARGS=(); [[ $SKIP_GUI == 1 ]] && GUI_ARGS=(--skip-gui-tests)

# validate <label> <bundle as the validator sees it> <pluginval> ; returns the verdict.
validate() {
  local label="$1" bundle="$2" exe="$3" log rc=0
  mkdir -p "$LOGS"
  log="$LOGS/$label-$(date +%Y%m%d-%H%M%S).log"
  # Captured to a file, never piped: build-windows.sh's 08-02 finding.
  "$exe" --strictness-level "$STRICTNESS" --timeout-ms "$TIMEOUT_MS" "${GUI_ARGS[@]}" \
         --validate "$bundle" > "$log" 2>&1 || rc=$?
  judge pluginval "$label" "$log" "$rc" "$PLUGINVAL_FAILED"
}

# ── Windows: one bundle, from a Windows-local copy of the validator ─────────
if [[ -n "$WINDOWS_BUNDLE" ]]; then
  [[ -d "$WINDOWS_BUNDLE" ]] || echo "WARNING: no bundle at $WINDOWS_BUNDLE — pluginval will fail on it" >&2
  dir="$(fetch_pluginval windows)"
  # Not run over the UNC share, and never under C:\Program Files. build-windows.sh
  # passes the profile it already resolved and checked; run alone, resolve it.
  profile="${FORROBOX_WIN_PROFILE:-}"
  if [[ -z "$profile" ]]; then
    raw="$(cmd.exe /c 'echo %USERPROFILE%' 2>/dev/null | tr -d '\r')"
    [[ "$raw" == *:* ]] || { echo "FATAL: could not resolve %USERPROFILE% via interop" >&2; exit 1; }
    profile="$(wslpath -u "$raw")"
  fi
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
# build-debug logs its assertions (FORROBOX_LOG_ASSERTIONS), so the suite run
# below can fail on one. An existing tree without the option is reconfigured.
# A NEW tree is created with Ninja and build-linux's JUCE_PATH; an EXISTING one
# keeps its own generator and paths and only gains the option — forcing -G on a
# tree made with another generator is a hard cmake error.
if [[ ! -f "$PROJECT/build-debug/CMakeCache.txt" ]]; then
  echo "configuring build-debug (JUCE_PATH=${JUCE_DIR:-<FetchContent>}, assertions logged)"
  cmake -S "$PROJECT" -B "$PROJECT/build-debug" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
        -DFORROBOX_LOG_ASSERTIONS=ON ${JUCE_DIR:+-DJUCE_PATH="$JUCE_DIR"} > /dev/null
elif ! grep -qiE '^FORROBOX_LOG_ASSERTIONS:BOOL=(ON|1|TRUE|YES|Y)$' "$PROJECT/build-debug/CMakeCache.txt"; then
  echo "build-debug: turning on assertion logging"
  cmake -S "$PROJECT" -B "$PROJECT/build-debug" -DFORROBOX_LOG_ASSERTIONS=ON > /dev/null
fi

mkdir -p "$LOGS"
FAILURES=0
for tree in debug:Debug linux:Release; do
  name="${tree%%:*}" config="${tree##*:}" build="$PROJECT/build-$name"
  # The Debug tree builds the suite in the same pass: one ninja run schedules
  # the shared objects once.
  targets=(ForroBox_VST3); [[ $name == debug ]] && targets+=(ForroBoxTests)
  echo; echo "=== build $name ($config): ${targets[*]}"
  cmake --build "$build" --target "${targets[@]}" > "$LOGS/build-$name.log" 2>&1 \
    || { echo "FATAL: ${targets[*]} did not build in $build — see $LOGS/build-$name.log" >&2; exit 1; }
  bundle="$build/ForroBox_artefacts/$config/VST3/ForroBox.vst3"
  validate "linux-${config,,}" "$bundle" "$dir/pluginval" || FAILURES=$((FAILURES + 1))
done

# ── the whole suite in Debug, where every JUCE assertion is a failing check ──
#  The Release suites cannot see a jassert; this run is the only place one
#  counts. The harness turns each unexpected assertion into a failing check
#  (tests/TestHarness.h), and judge() applies the log rules to what follows.
echo
suite_log="$LOGS/linux-debug-suite-$(date +%Y%m%d-%H%M%S).log"
suite_rc=0
(cd "$PROJECT/build-debug" && ./ForroBoxTests) > "$suite_log" 2>&1 || suite_rc=$?
judge "debug suite" linux-debug "$suite_log" "$suite_rc" "$SUITE_FAILED" || FAILURES=$((FAILURES + 1))

echo
if (( FAILURES > 0 )); then
  echo "validation gate: FAILED ($FAILURES of 3 verdicts)"
  exit 1
fi
echo "validation gate: PASSED (3 of 3 verdicts: pluginval Debug, pluginval Release, Debug suite)"
