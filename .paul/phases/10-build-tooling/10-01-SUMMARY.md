---
phase: 10-build-tooling
plan: 01
subsystem: testing
tags: [msvc, wsl-interop, dbghelp, exit-probe, build-windows]

requires:
  - phase: 09-content-convolution
    provides: "b55135e's timeout workaround, and 09-06 as the first plan whose MSVC run hung"
provides:
  - "tests/ExitProbe.h — stage markers and a thread-dumping watchdog for a hung teardown, self-tested on Windows"
  - "build-windows.sh judging the suite by its real exit code, with a 180 s timeout that FAILS"
  - "The hang's measured status: environmental, not reproduced in 13 runs of the identical binary"
affects: [phase 11 validation — pluginval rides the same MSVC run; any plan that runs build-windows.sh]

tech-stack:
  added: []
  patterns:
    - "Environment reaches a Windows process from WSL only through WSLENV"
    - "An intermittent failure is instrumented where it will recur, not only on demand"

key-files:
  created: [tests/ExitProbe.h, tests/ExitProbe.cpp]
  modified: [tests/TestMain.cpp, scripts/build-windows.sh, CMakeLists.txt]

key-decisions:
  - "arm-and-restore (user, at the checkpoint): probe armed on every run, exit code is the judge, a hang is a FAILURE"
  - "No fast exit: it would treat a cause that is not proven"

patterns-established:
  - "A hang is a failure: nothing in the build is judged by a line printed by a process that had to be killed"

duration: ~2h40m
started: 2026-09-25T07:50:00-03:00
completed: 2026-09-25T10:40:00-03:00
description: "The MSVC suite is judged by its real exit code again; the post-main hang did not reproduce and is now instrumented on every run"
type: Summary
about: "Forró Box"
---

# Phase 10 Plan 01: The MSVC post-`main` hang — Summary

**The build script judges the MSVC suite by its real exit code again, and `--install` runs end to
end in 130 s instead of 15+ minutes. The hang itself did NOT reproduce — 13 of 13 clean runs of the
identical binary that hung 17 minutes at the v0.1 tag — so it is instrumented on every run rather
than cured.**

## Performance

| Metric | Value |
|--------|-------|
| Duration | ~2h40m |
| Tasks | 3 auto + 1 checkpoint; Task 2 BLOCKED on reproduction |
| Files | 2 created, 3 modified; `src/` untouched |
| MSVC run, before → after | ~15 min (900 s dead wait) → 130 s whole script, suite ~33 s |

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: the blocking stage is measured | **Not met — instrument built and proven, nothing to measure** | 13/13 runs exited 0. The probe prints every stage (four locals, `atexit`, statics) and its self-test was caught by the watchdog on Windows with a stack naming `std::_Associated_state::_Wait ← main` |
| AC-2: cause confirmed by control | **Not met** | No reproduction, so no control. Recorded at the checkpoint rather than faked |
| AC-3: exits on its own with its real code | **Pass** | Exit 0 in ~33 s, every run. Non-zero path proven with a stand-in that exits 1 — see deviations |
| AC-4: script judges by exit status | **Pass** | pass→0, fail→1, watchdog→3 with its message, still-alive→1 with the last stage, the module list, and the Windows process actually killed. The "judged by the line it printed" branch is deleted. `--install`: hashes match, moduleinfo clean |
| AC-5: no change to what the plugin does | **Pass** | 4974/4974 on GCC 13, Clang 18, MSVC 2022; zero warnings from our sources; six gates green. `src/` untouched, so no `/code-review` or audition proof was triggered |

## What was measured

- **The window is one commit, and the code is not the whole cause.** MSVC exited 0 through 09-05
  (4824 checks); 09-07's SUMMARY is the first to read the log "because the process hangs (09-06's
  finding)". But the binary built 2026-09-24 12:51 — the one that hung at the tag — exited 0 in
  ~33 s **thirteen times** on 2026-09-25: from its build directory, from the repo (a UNC working
  directory to Windows), in a 10-run loop, and under `build-windows.sh` itself. Same bytes,
  different outcome: the trigger is the environment.
- **The 17-minute reading was the WINDOWS process, not the interop relay.** "State S" is a Linux
  state, which made the relay a suspect, but the relay measures ~2 MB RSS and the live exe hundreds
  of MB. A 233 KB working set is a Windows process that has released almost everything — blocked in
  `ExitProcess`'s DLL-detach stage, after every other thread (a watchdog included) is gone.
- **Which DLL is unknown.** The module list names `d2d1`, `dxgi`, `d3d11`, `dcomp` — JUCE 8's
  Direct2D, loaded by the headless UI tests — so a GPU-driver detach is *plausible*. It is not
  confirmed and is not written anywhere as a cause.
- **Ruled out by reading:** the suite launches no external process (the ABOUT links and all three
  file choosers are deliberately asserted without being triggered); `Settings` takes no
  cross-process lock; the 08-02 pipe hang is a different bug — output has gone to a file since.
- **`WSLENV` is required** for a Linux-side variable to reach a Windows process. The first armed
  run printed no markers for exactly that reason.

## Accomplishments

- `tests/ExitProbe.h` / `.cpp`: markers written to the raw stderr HANDLE (not a CRT stream a hang
  could strand); a detached watchdog that suspends each thread in turn, walks it with `StackWalk64`,
  resolves symbols only after resuming it, and ends the process with code 3. Off unless
  `FORROBOX_EXIT_PROBE` is set; `--exit-probe-selftest` refuses without it rather than hanging
- The test target links `/DEBUG` under MSVC only, so frames are named — the plugin's flags unchanged
- `run_tests()` rewritten: exit status is the judge; still alive at 180 s (~5× the measured suite) is
  a FAILURE that captures the last stage marker and `tasklist /M`, then kills the WINDOWS process
  with `taskkill`, not only the relay

## Task Commits

| Task | Commit | Type | Description |
|------|--------|------|-------------|
| Tasks 1 + 3 | `82fc8ee` | feat | Exit probe, `/DEBUG` on the test target, `run_tests()` by exit code |
| Task 2 | — | — | Blocked: nothing reproduced to control against |

## Files Created/Modified

| File | Change | Purpose |
|------|--------|---------|
| `tests/ExitProbe.h`, `tests/ExitProbe.cpp` | Created | The probe; kept out of the JUCE TUs because it needs `<windows.h>` and `<dbghelp.h>` in full |
| `tests/TestMain.cpp` | Modified | `Stage` markers beside the four locals (order unchanged), watchdog armed after the summary, the self-test |
| `scripts/build-windows.sh` | Modified | Probe armed via `WSLENV`; `run_tests()` by exit status; 900 s → 180 s |
| `CMakeLists.txt` | Modified | `ExitProbe.cpp` in the test target; `/DEBUG` link option, test target, MSVC only |

## Decisions Made

| Decision | Rationale | Impact |
|----------|-----------|--------|
| **arm-and-restore** (user, at the checkpoint) | The hang would not reproduce, so a cure had no cause to act on; a fast exit would have treated an unproven one and hidden future teardown bugs | A recurrence costs ~3 min and leaves the stage and module list, where it used to cost 15 min and leave nothing |
| Watchdog armed on EVERY run, not on demand | The plan made the probe opt-in; an intermittent hang is only caught if the instrument is already there when it happens | The script sets the variable; a normal run gains eight marker lines in the log |

## Deviations from Plan

| Type | Count | Impact |
|------|-------|--------|
| ACs not met | 2 (AC-1, AC-2) | Explicit at the checkpoint; the user chose to restore the law and instrument |
| Method substituted | 1 | Failing-check mutation done with a stand-in exiting 1, not a rebuilt binary with a forced failure. `run_tests` takes one branch for any non-zero status, so the path is covered — but it is not the literal mutation the plan named |
| Scope additions | 1 | `--exit-probe-selftest` and its refusal guard — the probe had to be proven on a case it must catch, per this project's rule for instruments |
| Auto-fixed | 2 | `getenv` raised C4996 under MSVC → `GetEnvironmentVariableA`; the self-test hung forever with the probe off → refuses with exit 2 |

**Probe default flipped from the plan.** The plan said "OFF by default"; it IS off in the binary,
but the script now always arms it. Recorded above as a decision, not hidden in the diff.

## Issues Encountered

| Issue | Resolution |
|-------|------------|
| No markers on the first armed MSVC run | WSL does not forward environment to Windows processes; `WSLENV` added |
| Harness self-test returned 1, not 3 | My 5 s harness timeout equalled the watchdog's 5 s; rerun at 20 s gave 3. Harness artefact, not a script bug |
| A stray `PING.EXE` survived the hang test | The harness named a wrapper script, so the kill-by-image-name missed; rerun with a correctly named wrapper killed the real process. In production the name is always `ForroBoxTests.exe` |

## Skill Audit

| Skill | Status | Notes |
|-------|--------|-------|
| /code-review | not triggered | Required only if `src/` changed; it did not |
| /simplify | pending | Required at Phase 10's close, not per plan |
| /graphify | not applicable | No spec or prototype question |

## Next Phase Readiness

**Ready:**
- Every later MSVC verification costs ~2 minutes; `--install` works end to end
- Phase 11's pluginval run can sit on a script that tells a hang from a pass

**Concerns:**
- The hang's cause is unknown and environmental. If it recurs, the build FAILS (by design) and
  `--install` needs a rerun. The Deferred Issues entry stays open as "instrumented, not reproduced"
- Open question for the user: was a DAW with Forró Box loaded during the 09-06…v0.1 runs?

**Blockers:** None

---
*Phase: 10-build-tooling, Plan: 01*
*Completed: 2026-09-25*
