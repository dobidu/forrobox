#!/usr/bin/env bash
# ============================================================================
#  Renders every GROOVE of every regional profile — sixteen of them since 09-04
#  — to a WAV and a MID, so a person can judge them. Phase 3 wrote this to A/B
#  four profiles against the HTML/CSS/JS prototype; 09-04 points it at the
#  groove banks, because twelve of the sixteen are drafted content that no UI
#  can reach until 09-06 builds the cycler.
#
#  The WAV goes through the SHIPPING chain — the real voices, CACHAÇA, the
#  character bus, the limiter and the master — so what you hear is what the
#  plugin plays. The MID carries the same groove's stored grid (un-humanised,
#  no ghosts, per 07-01) for auditioning against your own sounds.
#
#  Writes OUTSIDE the repository. Audio renders are output, not source, and the
#  project's boundaries keep generated audio out of git.
#
#  Usage: scripts/render-audition.sh [output-directory]
# ============================================================================
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

# The renderer lives in the one test executable — a second target would
# re-compile the whole JUCE module set.
binary="build-linux/ForroBoxTests"

if [[ ! -x "$binary" ]]; then
  echo "error: $binary not found. Build it first:" >&2
  echo "  cmake -B build-linux -G Ninja -DJUCE_PATH=\$HOME/JUCE -DCMAKE_BUILD_TYPE=Release" >&2
  echo "  cmake --build build-linux" >&2
  exit 1
fi

# Default under the session scratchpad when one is set, so renders never land
# in the working tree by accident.
default_out="${TMPDIR:-/tmp}/forrobox-audition"
out_dir="${1:-$default_out}"

mkdir -p "$out_dir"

"$binary" --render-audition "$out_dir"

echo
echo "Rendered to $out_dir"
echo "  <profile>__<groove>.wav   the shipping chain, four bars plus a tail"
echo "  <profile>__<groove>.mid   the stored grid, for your own kit"
echo
# THIS ADVICE WAS INVERTED AND STAYED THAT WAY FOR SIX PHASES. It told the
# listener to expect something MECHANICAL and loud because "CACHAÇA (03-02) and
# the character bus, limiter and master (03-03) are not built yet" — all three
# shipped the same week it was written. /code-review.
echo "The chain sets its own level: nothing here should clip, and the renderer"
echo "fails the run if anything does."
echo
echo "To compare a profile's DEFAULT groove against the prototype:"
echo "  open 'Forró Box (standalone).html', pick the matching profile, press play"
ls -la "$out_dir"
