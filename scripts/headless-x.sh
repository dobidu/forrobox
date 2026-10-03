#!/usr/bin/env bash
# Forró Box — run a command on a headless X display that JUCE can open windows
# on (15-02).
#
#   scripts/headless-x.sh <juce-dir> <command> [args...]
#
# A DESKTOP has a window manager, and the window manager creates the WM_* /
# _NET_WM_* / _MOTIF_WM_HINTS atoms. JUCE fetches those only if they already
# exist (XWindowSystemUtilities::Atoms::getIfExists) and sets window properties
# with them, so on a bare Xvfb a test that puts a component on the desktop passes
# atom 0 to XChangeProperty and the default X error handler exits (BadAtom). The
# suite is not at fault and neither is the plugin; the display is incomplete.
#
# So: Xvfb with -noreset (interned atoms survive the last client), and every
# window-manager atom name JUCE's X11 layer mentions — read from the JUCE source
# itself, so a JUCE upgrade cannot leave this list stale — interned up front.
# Needs Xvfb and xprop (x11-utils). Used by scripts/build-linux-portable.sh and CI.
set -euo pipefail

JUCE_DIR="${1:?usage: headless-x.sh <juce-dir> <command> [args...]}"
shift
X11_SOURCE="$JUCE_DIR/modules/juce_gui_basics/native/juce_XWindowSystem_linux.cpp"
[[ -f "$X11_SOURCE" ]] || { echo "FATAL: no JUCE X11 source at $X11_SOURCE" >&2; exit 1; }

DISPLAY_NUMBER="${HEADLESS_X_DISPLAY:-99}"
Xvfb ":$DISPLAY_NUMBER" -screen 0 1600x1200x24 -noreset >/dev/null 2>&1 &
XVFB_PID=$!
trap 'kill "$XVFB_PID" 2>/dev/null || true' EXIT
export DISPLAY=":$DISPLAY_NUMBER"

for _ in $(seq 50); do xprop -root >/dev/null 2>&1 && break; sleep 0.1; done

atoms=$(grep -oE '"(_NET_|_MOTIF_|_KDE_|_WIN_|WM_|KWM_)[A-Z0-9_]*"' "$X11_SOURCE" | tr -d '"' | sort -u)
[[ -n "$atoms" ]] || { echo "FATAL: no window-manager atom names found in $X11_SOURCE" >&2; exit 1; }

for atom in $atoms; do
  xprop -root -f FORROBOX_INTERN 32a -set FORROBOX_INTERN "$atom"
done

"$@"
