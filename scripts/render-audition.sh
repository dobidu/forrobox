#!/usr/bin/env bash
# ============================================================================
#  Renders each regional profile to a WAV for A/B listening against the
#  HTML/CSS/JS prototype. Phase 3's goal is that the grooves audibly match, and
#  that is a judgement a person makes — this exists so the person has something
#  to play.
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
echo "Compare against the prototype at the same BPM and swing:"
echo "  open 'Forró Box (standalone).html', pick the matching profile, press play"
echo
echo "Expect it to sound MECHANICAL and loud: CACHAÇA (03-02) and the character"
echo "bus, limiter and master (03-03) are not built yet."
ls -la "$out_dir"
