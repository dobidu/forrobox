#!/usr/bin/env bash
# Forró Box — the Linux floor, read from the binaries themselves (15-02).
#
#   scripts/check-glibc-floor.sh <glibc-floor> <binary>...
#
# Fails if any binary needs a glibc newer than <glibc-floor>, or ANY libstdc++
# version (GLIBCXX_/CXXABI_) — the portable build links libstdc++ statically, so
# a libstdc++ dependency means that did not apply. The claim is about the bytes
# that ship (`objdump -T`), not about a build setting. Used by
# scripts/build-linux-portable.sh and by CI.
set -euo pipefail

FLOOR="${1:?usage: check-glibc-floor.sh <glibc-floor> <binary>...}"
shift
(( $# > 0 )) || { echo "usage: check-glibc-floor.sh <glibc-floor> <binary>..." >&2; exit 2; }

bad=0
for bin in "$@"; do
  [[ -f "$bin" ]] || { echo "FATAL: missing $bin" >&2; bad=1; continue; }
  symbols=$(objdump -T "$bin")
  max=$(grep -o 'GLIBC_[0-9.]*' <<<"$symbols" | sed 's/GLIBC_//' | sort -uV | tail -1)
  cxx=$(grep -oE '(GLIBCXX|CXXABI)_[0-9.]+' <<<"$symbols" | sort -uV | tail -1 || true)

  if [[ "$(printf '%s\n%s\n' "$max" "$FLOOR" | sort -V | tail -1)" != "$FLOOR" ]]; then
    echo "FATAL: $(basename "$bin") needs GLIBC_$max, above the $FLOOR floor" >&2; bad=1; continue
  fi
  if [[ -n "$cxx" ]]; then
    echo "FATAL: $(basename "$bin") depends on libstdc++ ($cxx) — it must be linked statically" >&2; bad=1; continue
  fi
  echo "  $(basename "$bin"): glibc floor $max (<= $FLOOR), no libstdc++ dependency"
done
exit "$bad"
