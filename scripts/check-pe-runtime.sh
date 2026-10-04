#!/usr/bin/env bash
# Forró Box — no MSVC runtime DLL in a Windows binary (15-01).
#
#   scripts/check-pe-runtime.sh <binary>...
#
# The MSVC runtime is linked statically (CMakeLists.txt), so no shipped binary
# may import the VC++ runtime or the UCRT forwarders — a user without the
# redistributable would get "VCRUNTIME140.dll was not found" at load. Read from
# the PE import table itself (`objdump -p`), so the claim is about the bytes
# that ship. Used by scripts/build-windows.sh and by CI.
set -euo pipefail

(( $# > 0 )) || { echo "usage: check-pe-runtime.sh <binary>..." >&2; exit 2; }

# llvm-objdump on a Windows CI runner, which has no GNU binutils; both print the
# import table's "DLL Name:" lines for -p.
OBJDUMP="${OBJDUMP:-objdump}"

bad=0
for bin in "$@"; do
  [[ -f "$bin" ]] || { echo "FATAL: expected binary missing: $bin" >&2; bad=1; continue; }
  hits=$("$OBJDUMP" -p "$bin" | grep -i 'DLL Name:' | grep -Ei 'vcruntime|msvcp|concrt|api-ms-win-crt' || true)
  if [[ -n "$hits" ]]; then
    echo "FATAL: $(basename "$bin") imports a runtime DLL (the static runtime did not apply):" >&2
    sed 's/^[[:space:]]*/    /' <<<"$hits" >&2
    bad=1
  fi
done
(( bad == 0 )) || exit 1
echo "runtime imports: clean ($# binaries — no VC++ redistributable needed)"
