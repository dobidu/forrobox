---
phase: 11-validation
plan: 01
subsystem: testing
tags: [pluginval, vst3, validation, gate, msvc, bash]

requires:
  - phase: 10-build-tooling
    provides: "build-windows.sh judged by exit code, with the exit probe armed; that probe is what traced the hang"
provides:
  - "scripts/validate-plugin.sh: pluginval v1.0.4, hash-pinned, strictness 10 with GUI tests, verdict read from the log"
  - "build-windows.sh validates the Windows Release VST3 before any install"
  - "the MSVC post-main hang traced to JUCE's drag pool"
affects: [11-02 drag-pool hang, 11-03 bypass latency, 11-04 Typeface leak, every later plan's regression check]

tech-stack:
  added: ["pluginval v1.0.4 (test tool only; fetched into build/tools, never linked, shipped or committed)"]
  patterns:
    - "A validator's own SUCCESS is not a pass: judge exit code AND log (failed tests, JUCE assertions, leaks)"
    - "A downloaded tool runs only from a zip whose hash matched the pin; the unpacked copy is keyed to that hash"

key-files:
  created: [scripts/validate-plugin.sh]
  modified: [scripts/build-windows.sh]

key-decisions:
  - "The gate lands RED on Linux Debug by design; 11-03 and 11-04 turn it green"
  - "No display is a FAILURE, not a silent skip: both Debug findings appear only with GUI tests on"
  - "The drag-pool hang becomes 11-02, before bypass (user)"

duration: ~1 session
completed: 2026-09-30
description: "pluginval gate on three targets, judged from the log. It is red on Linux Debug for exactly the two real findings, and on the way it traced the MSVC hang to JUCE's drag pool"
type: Summary
about: "Forró Box"
---

# Phase 11 Plan 01: pluginval as a repeatable gate

**One command validates the VST3 at strictness 10, GUI tests included, and judges the verdict from the log.** On Linux Debug, pluginval exits 0 and prints SUCCESS, but the log shows 503 assertions and 3 leaked classes, so the gate FAILS it. Linux Release and Windows Release PASS. Along the way, the Phase 10 hang recurred and the exit probe named its frame.

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: fetched, pinned, never committed | **Pass** | Fetched into `build/tools/pluginval-1.0.4/`, checked by SHA-256, unpacked copy keyed to the hash. A re-run doesn't download again. One corrupted byte → refused with both hashes printed, and the bad zip deleted. `git status` shows only the plan's own files |
| AC-2: the verdict is the gate's | **Pass** | FAIL on a non-zero exit, a failed test, `JUCE Assertion failure` or `*** Leaked objects`. The report lists each assertion site with its count and each leak line. Logs go to `build/pluginval/<target>-<timestamp>.log`, with the seed |
| AC-3: three targets, strictness 10, GUI on | **Pass, with one concern** | Linux Debug and Release are built and validated; `build-debug` is configured automatically with build-linux's `JUCE_PATH`. With no display the gate fails unless `--skip-gui` is passed, and the verdict then prints `SKIPPED`. The Windows mode runs a copy under `%USERPROFILE%\forrobox-tools\`. **The `build-windows.sh` step could not run end to end:** the MSVC suite hangs before reaching it (below). The step's logic is proven in isolation, and the Windows mode was run directly on the freshly built bundle |
| AC-4: red for exactly the known findings | **Pass** | Linux Debug FAIL: 500 × `juce_AudioProcessor.cpp:599` + 3 × `juce_LeakedObjectDetector.h:116` (6 Typeface, 6 FTFaceWrapper, 1 FTLibWrapper), with pluginval's own exit code 0. Linux Release PASS. Windows Release PASS. A missing bundle FAILS through the exit code, and a corrupted zip is REFUSED |

## Findings

**1. Both Debug findings appear only with GUI tests on, every time.** Four runs: with GUI, 503 assertions and 6 leak lines on two different seeds. With `--skip-gui`, zero and zero. The bypass assertions are logged under pluginval's "Parameter thread safety" test and the leaks at unload, so what matters is the GUI tests having run, not the seed. That's why the gate refuses to skip quietly without a display: a headless run would be green.

**2. The MSVC post-main hang recurred, three runs out of three, and the exit probe traced it.**
- **Where it stops:** the last stage printed is `keepTimersAlive destroyed`; `juceInit destroyed` never prints.
- **The main thread:** `main` → `~ScopedJuceInitialiser_GUI` → `DeletedAtShutdown::deleteAll` → `DragAndDropHelpers::ThreadPoolHolder` destructor → `ThreadPool::removeAllJobs (true, -1)` → wait. So the hang is inside `shutdownJuce_GUI`, not in DLL detach as 10-01 suspected from the working set.
- **JUCE's own explanation** (`juce_DragAndDrop_windows.cpp:307`): *"Wait forever if there's a job running. The user needs to cancel the transfer in the GUI."* The job is the OLE `DoDragDrop` started by the drag-export check at `UiTest.cpp:7649`, which drives `DragMidiButton` past its threshold into a real `performExternalDragDropOfFiles`.
- **Not yet known:**
  - The pool worker's own stack does not appear among the 64 dumped threads.
  - A cursor moved with `SetCursorPos` did not release the wait. I've left a hypothesis about real user input untested rather than state it.
  - Why it was 0 of 13 in 10-01 and 3 of 3 today is not explained.
- **Evidence:** `build/pluginval/msvc-hang-2026-09-30.log` (gitignored, local).
- **Next:** inserted as **11-02** with the user.

**3. pluginval creates the plugin on the message thread**, so the `loadProfile` assert (now 11-05) needs its own reproduction.

**4. Steinberg's vst3 validator is skipped** (`validator path hasn't been set`). Recorded as deferred, because it needs the VST3 SDK, which would be a new dependency.

## Files Created/Modified

| File | Change | Purpose |
|------|--------|---------|
| `scripts/validate-plugin.sh` | Created | Fetch, pin, build, validate and judge, with a `--windows BUNDLE` mode and `--skip-gui` |
| `scripts/build-windows.sh` | Modified | Validates the Release bundle after the tests and before any install; a failure exits non-zero; help text updated |

## Deviations from Plan

| Type | Count | Impact |
|------|-------|--------|
| Verification gap | 1 | `build-windows.sh` end to end: blocked by the hang. Covered by the direct Windows run and an isolated test of the step's failure logic. Closes with 11-02 |
| Scope additions | 0 | The hang was diagnosed, not fixed. The fix went back to the user as a new plan |

## Next Phase Readiness

**Ready:** 11-02 starts from a named frame and a 3-of-3 reproduction. That's the first time this hang has been reproducible on demand.

**Concerns:** Until 11-02 lands, `build-windows.sh` is red for a reason unrelated to the code under change.

**Blockers:** None for 11-02.

---
*Phase: 11-validation, Plan: 01*
*Completed: 2026-09-30*
