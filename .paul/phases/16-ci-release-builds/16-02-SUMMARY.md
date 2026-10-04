---
phase: 16-ci-release-builds
plan: 02
subsystem: ci
tags: [github-actions, ci, pluginval, linux, windows, gates]

requires:
  - phase: 15-portable-builds
    provides: "build-linux-portable.sh, check-pe-runtime.sh, headless-x.sh"
  - phase: 16-ci-release-builds
    plan: 01
    provides: "package-release.py (used by 16-03)"
provides:
  - ".github/workflows/ci.yml: linux-gate, linux-portable, windows on push to main and PRs"
  - "validate-plugin.sh --windows runs natively under Git Bash; check-pe-runtime.sh takes OBJDUMP"
affects: [16-03 (the tag job reuses these jobs), 17 (macOS joins the workflow)]

key-files:
  created: [.github/workflows/ci.yml]
  modified: [scripts/validate-plugin.sh, scripts/check-pe-runtime.sh, README.md]

key-decisions:
  - "CI runs on every push to main and every PR (user, Phase 16 planning)"
  - "Every CI step calls the repo's own scripts, so CI and the local machine cannot drift"

duration: ~1 session
completed: 2026-10-04
description: "GitHub Actions runs this machine's gates on every push to main and every pull request — green on the first run, and proven to go red on a deliberately failing check"
type: Summary
about: "Forró Box"
---

# Phase 16 Plan 02: CI runs the gates

**Every push to main and every PR now faces the gates this machine runs.**

| Job | Runs | Run time |
|-----|------|----------|
| `linux-gate` | `validate-plugin.sh` under `headless-x.sh`: pluginval Linux Debug + Release, the Debug suite | ~9 min |
| `linux-portable` | `build-linux-portable.sh`: the 22.04 build, the glibc 2.35 floor, the clean-22.04 suite | ~7 min |
| `windows` | MSVC Release; the suite; `check-pe-runtime.sh` (llvm-objdump); pluginval | ~9 min |

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: green on main | **Pass** | Green on the FIRST run (37191883714). The logs show: gate 3/3 (Debug suite 5147/5147); floor 2.35 on all three binaries and the clean suite 5143/5143; Windows suite 5143/5143, runtime imports clean, pluginval PASS |
| AC-2: red when it should be | **Pass** | PR #1 (one `checkEqual` 16 → 17) failed ALL THREE jobs (run 37192434623), naming "the default step count is 16 (expected 17, got 16)". The PR was closed unmerged and the branch deleted |
| AC-3: the local paths still work | **Pass** | The local gate gave 3/3; `build-windows.sh` (WSL) passed: suite, runtime check, pluginval |

## Changes
- **`validate-plugin.sh --windows`:** with no `wslpath` and `MSYSTEM` set (Git Bash on a runner),
  it runs the hash-checked `pluginval.exe` in place, with the bundle path from `cygpath -w`. The
  WSL path is unchanged.
- **`check-pe-runtime.sh`:** `OBJDUMP` override. `llvm-objdump -p` prints the same `DLL Name:`
  lines, verified locally on the dynamic v0.2 exe (rejected) and today's static one (clean).
- **`ci.yml`:**
  - actions pinned by commit SHA (checkout v7.0.1, upload-artifact v7.0.1);
  - `contents: read`;
  - superseded runs cancelled;
  - per-job timeouts;
  - logs uploaded on failure;
  - JUCE 8.0.12 shallow-cloned per job.
- **README:** the CI badge and one line on what CI runs.

## Deviations

| Type | Count | Notes |
|------|-------|-------|
| Iterations | 0 | No runner-specific fixes were needed |
| Process slip | 1 | The plan-time STATE/ledger update had failed an assertion while an unchained `echo ok` reported success. It was caught at APPLY, and both entries were written then. Later state updates chain with `&&` |
| Caching | 0 | Not added: every job is under 10 minutes |

## Skill audit
`/code-review` was not required: CI configuration and build scripts.

## Next
**16-03:** the tag job, which packages on both runners with `package-release.py` and creates a
DRAFT release for the user to publish.

---
*Phase: 16-ci-release-builds, Plan: 02 · Completed: 2026-10-04*
