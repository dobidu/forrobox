---
phase: 20-carried-fixes
plan: 01
subsystem: ui
tags: [file-chooser, async, warnings, midi-export, seqlock, test-determinism]

requires:
  - phase: 19-groove-files
    provides: "launchChooser (in Chassis.cpp), Chassis::showWarning, the forced-extension replace confirm for groove files"
provides:
  - "src/AsyncFileChooser: launchChooser, showWarningBox, saveWithForcedExtension — every file dialog and warning box in src/ goes through it"
  - "LOAD IR… and the MIDI export report failures through Chassis::showWarning (seams SidePanel::loadImpulseResponseOrWarn, DragMidiButton::exportTo)"
  - "MIDI export: a forced .mid over an existing file asks SUBSTITUIR O ARQUIVO? (Chassis::confirmReplaceOnScreen, shared with the groove export)"
  - "A deterministic held-lock contention test; the saturated-writer test asserts only what holds for every interleaving"
affects: [23-factory-irs (LOAD IR… now opens through launchChooser)]

key-files:
  created: [src/AsyncFileChooser.h, src/AsyncFileChooser.cpp]
  modified: [CMakeLists.txt, src/Chassis.cpp, src/Chassis.h, src/SidePanel.cpp, src/SidePanel.h, src/DragMidiButton.cpp, src/DragMidiButton.h, src/FooterBar.h, src/PatternSnapshot.h, tests/UiTest.cpp, tests/StateRoundTripTest.cpp]

key-decisions:
  - "The forced-extension rule lives in one function; a null confirm is never a yes (nothing is written)"
  - "Components keep a showWarning hook wired explicitly by the chassis — not a parent lookup, which the codebase does not use"
  - "The seqlock test holds the lock through a test-only friend; the holder is on another thread so a waiting reader FAILS instead of hanging"

patterns-established:
  - "Every file dialog opens through forrobox::launchChooser; every save forces its extension through saveWithForcedExtension"
  - "A concurrency property is proved by constructing the race (hold the lock), never by hoping a scheduler produces it"

duration: 1 session (2026-11-10)
completed: 2026-11-10
description: "v0.3's three leftovers closed: one async chooser and warning for every dialog, a MIDI export that asks before replacing a file, and a seqlock contention check that cannot flake"
type: Summary
about: "Forró Box"
---

# Phase 20 Plan 01: Carried fixes

**The three issues v0.3 deferred are closed.** Every dialog opens through one helper. The MIDI
export no longer replaces a file without asking. The test that went red at random on macOS CI now
proves its point by construction.

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: one chooser helper, one warning | **Pass** | `make_unique<juce::FileChooser>` and `NativeMessageBox::showAsync` each appear once in src/, in AsyncFileChooser.cpp. Titles, start folders, patterns and flags are unchanged |
| AC-2: IR failure through the seam | **Pass** | A non-audio file raises 1 warning titled "Could not load impulse response" that names the file, and the stored IR is unchanged. A real IR raises no warning |
| AC-3: MIDI never replaces unasked | **Pass** | A bare name over an existing file shows SUBSTITUIR O ARQUIVO?: Esc keeps the bytes, Enter replaces them. A name typed with .mid is not asked twice. A new bare name gets no prompt. A failed write raises 1 warning naming the path |
| AC-4: contention proved deterministically | **Pass** | Against a held lock, refresh returns false in well under 500 ms, contention is exactly 1, and the old table is kept. After release, the new table arrives. A waiting-reader mutation fails in 2 s without hanging |
| AC-5: human-verify in Ableton | **Pass** | Approved 2026-11-10 (Windows, D:\VST3) |

**Verification:**
- **Suites:** GCC 5503/5503, Clang 5503/5503, Debug 5509/5509, and 20/20 consecutive Linux runs.
- **Windows (WSL):** 5503/5503, pluginval strictness 10 PASS, installed to D:\VST3 with matching hashes.
- **Gates:** geometry, charset, theme and profiles all green.
- **CI:** run 42890425928 on `748ec73` green on all 4 jobs. The `/simplify` commit `f6df2a3` is covered by its own CI run.

**Mutation proofs (4, each reverted):**
- MIDI confirm without the `forced` guard
- MIDI write before asking
- A reader that waits (`tryEnter` spun to success)
- The shared helper without the `forced` guard

**Skill audit:**
- /graphify ○: skipped deliberately and recorded in the plan, because the scope was 3 named call sites and 1 test, all read directly.
- /code-review ✓ (medium, 8 candidates)
- /simplify ✓ (4 angles)

## Task Commits

| Task | Commit | Type | Description |
|------|--------|------|-------------|
| Tasks 1–3 + /code-review fixes | `748ec73` | feat | AsyncFileChooser, IR/MIDI seams, MIDI replace-confirm, deterministic contention |
| APPLY state | `1b50857` | docs | Checkpoint approved, CI green |
| UNIFY /simplify | `f6df2a3` | refactor | saveWithForcedExtension, confirmReplaceOnScreen, confirmBox removed |

## Deviations from Plan

| Type | Count | Impact |
|------|-------|--------|
| Auto-fixed | 2 | Test-side only |
| Scope changes | 1 | `confirmBox` was planned as the unwired fallback, then removed at /simplify |
| Deferred | 4 | Logged below |

- **The Debug suite caught a JUCE assertion in the unwritable-path case.** `replaceWithData` asserts in `juce_TemporaryFile.cpp:115` when it cannot create its temp file. The test declares it with `ExpectAssertions (1, "juce_TemporaryFile.cpp", …)`, and the production code already checks the return value.
- **`loadFileAsData` appends to its block.** A reused block made a correct write look wrong, so each read now uses a fresh block.
- **`confirmBox` was dropped.** /simplify found that nothing in the shipped plugin could reach it, because every MIDI button sits in a chassis that wires the on-screen prompt. With no confirm wired, `saveWithForcedExtension` now writes nothing and asserts, where the MIDI copy used to fall through to a blind write.

**/code-review (medium):** 8 candidates.
- Fixed 3: the headerBar-null drop (later made moot by /simplify), the 2 s hold changed to a reader-done flag with a 2 s cap, and the byte copy.
- Duplication (1) went to /simplify.
- Deferred 3: the two prompt-behaviour items as one, plus the MIDI success feedback.
- Accepted 1: the TemporaryFile assertion dependency. The Debug suite runs only on Linux, and Release compiles assertions out.

**/simplify (4 angles):**
- Fixed:
  - Forced-extension rule: now in one function (`saveWithForcedExtension`)
  - Replace prompt: one copy (`Chassis::confirmReplaceOnScreen`)
  - Removed: the dead fallback capture and the unreachable `confirmBox`
  - MIDI bytes: moved instead of copied
  - Saturated-test comments: duplicated and overstated text trimmed
- Skipped:
  - **Parent lookup:** children could find the Chassis via `findParentComponentOfClass` instead of being wired. Skipped because it is a pattern the codebase does not use; explicit wiring is the convention (`onProfileLoaded`).
  - **Shared test temp folder:** making one temp-folder helper for all tests is a separate cleanup.

### Deferred Items

- **The preset-screen prompt drops whatever it replaces.** `HeaderBar::openPrompt` closes an open prompt without a callback, and a click away counts as cancel. This applies to both the groove and MIDI exports and was the behaviour approved in 19-02. A pending replace or name entry can be lost silently.
- **The MIDI export gives no success feedback.** Groove export shows EXPORTADO / ERRO, but MIDI shows nothing, so the two flows that share the prompt differ.
- **One test asserts a JUCE internal.** The unwritable-path test expects the assertion at `juce_TemporaryFile.cpp`. A JUCE update may move it. The fix is cheap: update the site name.
- **Temp folders in tests are made by hand about 10 times.** `TempFolder` in UserGroovesTest.cpp could be shared through TestHarness.h.

## Next Phase Readiness

**Ready:** Phase 20 is complete. Phase 23's `LOAD IR…` change (opening in the factory folder) is one argument to `launchChooser`.
**Concerns:** the four deferred items above; none blocks v0.4.
**Blockers:** None.

---
*Phase: 20-carried-fixes, Plan: 01*
*Completed: 2026-11-10*
