#!/usr/bin/env bash
# Forró Box — the suite's verdict, from its log (17-02).
#
#   scripts/judge-suite.sh <suite.log>
#
# PASS only when the log's last "checks passed" line says OK; on a failure it
# prints the failing checks. One rule for every platform's build script and CI
# step, so they cannot disagree on what a passing suite is.
set -euo pipefail

LOG="${1:?usage: judge-suite.sh <suite.log>}"
[[ -f "$LOG" ]] || { echo "FATAL: no suite log at $LOG" >&2; exit 1; }

result="$(grep -E "checks passed" "$LOG" | tail -1 || true)"
if [[ -z "$result" ]]; then
  echo "FATAL: the suite printed no result line (crashed or hung?) — last lines:" >&2
  tail -30 "$LOG" >&2
  exit 1
fi
echo "  $result"
if ! grep -q "OK" <<<"$result"; then
  grep "  FAIL  " "$LOG" | head -60 >&2 || true
  exit 1
fi
