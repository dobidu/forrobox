---
phase: 19-groove-files
plan: 02
subsystem: ui
tags: [groove-files, export, import, file-chooser, drag-and-drop, async]

requires:
  - phase: 19-groove-files
    provides: "19-01: readGrooveFile / writeGrooveFile, UserGrooveLibrary::importFiles, processor grooveForExport / importGrooves"
provides:
  - "Gear menu GROOVES band: Export groove… / Import grooves…, async choosers"
  - ".forrogroove files dropped anywhere on the plugin are imported; the whole plugin lights during the drag"
  - "Screen report (EXPORTADO / ERRO, 1 GROOVE IMPORTADO / N IMPORTADOS / IMPORTADO COMO CÓPIA / JÁ EXISTIA / NADA IMPORTADO); a warning box only for failures, refused files capped at 12 lines; MEUS shown after an import"
  - "launchChooser: the one async FileChooser helper in Chassis (read results before the reset)"
  - "Chassis::showWarning: the testable warning seam, now also for sample-load failures"
affects: [v0.3 milestone close]

key-files:
  created: []
  modified: [src/Chassis.cpp, src/Chassis.h, src/SettingsMenu.cpp, src/SettingsMenu.h, src/PluginProcessor.h, src/PluginProcessor.cpp, tests/UiTest.cpp, tests/UserGroovesTest.cpp]

key-decisions:
  - "Import does not change the playing groove; it shows MEUS (plan, unchanged by the user)"
  - "The groove exported is captured when the dialog opens, not when it closes"
  - "A forced .forrogroove extension landing on an existing file asks SUBSTITUIR O ARQUIVO? on the preset screen"
  - "A failed export names its reason in a warning box, as a refused import does"

patterns-established:
  - "An async FileChooser held in a unique_ptr: read fc's results BEFORE resetting the pointer — fc IS that chooser (launchChooser)"
  - "User-facing failures go through Chassis::showWarning so tests can capture them"

duration: 2 sessions (2026-10-06 Task 1; 2026-11-09/10 checkpoint, review, simplify)
completed: 2026-11-10
description: "Sharing a groove from the plugin: Export groove… and Import grooves… in the gear menu, .forrogroove drag-and-drop anywhere, a short screen report and a warning only for failures — after the Ableton checkpoint caught an export that silently saved nothing"
type: Summary
about: "Forró Box"
---

# Phase 19 Plan 02: Groove files on screen

**A user can hand a groove to a friend and take one back without leaving the plugin.** The
checkpoint in Ableton caught the one bug the suite could not see: Export groove… wrote nothing.

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: export from the menu | **Pass** | Async save chooser in Documents, suggested name, `*.forrogroove`, a second open ignored; EXPORTADO / ERRO: NÃO EXPORTADO plus the reason in a warning; a bare name gets the extension and asks before replacing an existing file |
| AC-2: import from the menu | **Pass** | New + identical + damaged → 1 GROOVE IMPORTADO, MEUS shown, one dialog naming the damaged file and why; identical only → JÁ EXISTIA, no dialog, tab unchanged; a lone copy → IMPORTADO COMO CÓPIA; 30 refusals → one box listing 12 + "and 18 more" |
| AC-3: drag and drop | **Pass** | Groove files welcome anywhere, the whole plugin lights; mixed with a WAV → refused; one audio file onto a strip unchanged |
| AC-4: human-verify in Ableton | **Pass** | Approved 2026-11-10 after the export fix (`forroeletrico.forrogroove` written and inspected: v1, `FORRÓ ELÉTRICO`, 128 BPM, all lanes) |

**Verification:** Linux GCC 5482/5482, Clang 5482/5482, Debug 5487/5487; WSL Windows 5483/5483 +
pluginval strictness 10 PASS, installed to `D:\VST3` (hashes match) for the review-fix build. CI
`d4fc1a7` (run 42851381977) green on 4 jobs after a macOS rerun (see Deferred); CI `da2ce59`
(/simplify) was cancelled by the next push; `14eb42c` (same code + docs) run 42859055297 green on
all 4 jobs first time.

**Skill audit:** /code-review ✓ (medium, 10 findings), /simplify ✓ (4 angles), /graphify ✓ (19-01
planning). All required skills invoked ✓.

## Task Commits

| Task | Commit | Type | Description |
|------|--------|------|-------------|
| Task 1: menu items, choosers, report, drop | `8718d0e` | wip | Paused at the checkpoint |
| Checkpoint fix + Task 2 /code-review | `d4fc1a7` | fix | Chooser read-before-reset; 8 review fixes |
| UNIFY /simplify | `da2ce59` | refactor | launchChooser, snapshot in the callback, loadUserSampleOrWarn, processor helpers removed |

## Deviations from Plan

| Type | Count | Impact |
|------|-------|--------|
| Auto-fixed | 1 | Essential — the feature did not work |
| Scope additions | 3 | Small, from /code-review: overwrite confirm, copy report, drag highlight |
| Deferred | 3 | Logged below |

**1. Export saved nothing (found at the checkpoint).** Both groove chooser callbacks reset
`grooveChooser` before reading `fc` — which IS that chooser — so `getResult()` read freed memory
and came back empty. The tests drive the `exportGrooveTo` seam directly and never the callback,
so the suite was green. Fixed by reading first (`d4fc1a7`); at /simplify the rule moved into one
`launchChooser` helper so it is written once.

**/code-review (medium):** 10 findings; 8 fixed (forced-extension overwrite, groove snapshot at
open, export failure reason, copy report, screen-fit count, refused list cap, whole-plugin drag
highlight, stale kGrooveBase comment); 2 duplication findings went to /simplify.

**/simplify (4 angles):** fixed launchChooser (3 copies → 1), the exportSnapshot member and its
second capture per export, the sample-load warning (2 inline copies → `loadUserSampleOrWarn` via
`showWarning`), the nested-ternary report, the -2 drag sentinel → `grooveDragActive`, misplaced
doc comments, and removed `ForroBoxAudioProcessor::exportGroove` / `suggestedExportFileName`
(test-only after the snapshot). Skipped: the `refreshFromState` + `showMine` double layout per
import (trivial); the 5-line path copy in `filesDropped`.

### Deferred Items

- **MIDI export can overwrite silently** (`src/DragMidiButton.cpp:~455`): it forces `.mid` after
  the dialog the same way and never re-checks, so typing "groove" replaces an existing
  "groove.mid" unasked. Found by the /simplify altitude review; outside this plan's files.
- **SidePanel (IR) and DragMidiButton choosers and warning boxes** still hand-written; move them to
  `launchChooser` / a shared warning once the helper is lifted out of Chassis.cpp.
- **Flaky seqlock contention check** (`tests/StateRoundTripTest.cpp:2925`): 0 contended of 200000
  on the macOS runner once; passed on rerun. Second flake of this family after 17-01 — make it
  deterministic.

## Next Phase Readiness

**Ready:** Phase 19 complete, the last phase of v0.3. VERSION is already 0.3.0.
**Concerns:** the three deferred items above; none blocks the release.
**Blockers:** None.

---
*Phase: 19-groove-files, Plan: 02*
*Completed: 2026-11-10*
