#!/usr/bin/env bash
# Forró Box — a shallow clone of the JUCE release this project pins (16-03).
#
#   scripts/fetch-juce.sh <dir>
#
# The tag is read from CMakeLists.txt's FORROBOX_JUCE_TAG — the one place it is
# pinned, the same value the build's FetchContent fallback uses — so CI and a
# fresh clone cannot build against different JUCEs. Used by every CI job.
set -euo pipefail

DEST="${1:?usage: fetch-juce.sh <dir>}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TAG=$(sed -nE 's/^set\(FORROBOX_JUCE_TAG "([^"]+)"\)/\1/p' "$ROOT/CMakeLists.txt")
[[ -n "$TAG" ]] || { echo "FATAL: no FORROBOX_JUCE_TAG in CMakeLists.txt" >&2; exit 1; }

git -c advice.detachedHead=false clone --quiet --depth 1 --branch "$TAG" https://github.com/juce-framework/JUCE.git "$DEST"
echo "JUCE $TAG -> $DEST"
