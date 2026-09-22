---
phase: 08-polish
plan: 02
subsystem: ui
tags: [juce, vst3, propertiesfile, popupmenu, preferences, wsl]

requires:
  - phase: 04-ui-shell
    provides: "ForroBoxLookAndFeel's setMode/setCornerRadius/setAccentIntensity, left with no callers on purpose"
  - phase: 05-sequencer-grid
    provides: "KitOverlay's approved overlay treatment and cubicBezierEase"
  - phase: 08-polish
    provides: "08-01's constructor, whose restore-beats-default property this plan must not break"
provides:
  - "forrobox::Settings — the plugin's first global preferences store"
  - "A gear button and a PopupMenu carrying four live settings"
  - "An ABOUT overlay with clickable author and repository links"
  - "A working MSVC build script: the four-occurrence stall is diagnosed and fixed"
affects: [08-03 display font, any plan adding a user preference]

tech-stack:
  added: []
  patterns:
    - "Global preferences live in juce::PropertiesFile, opened per operation, never in the APVTS"
    - "Invented controls are allowed only with an explicit user decision, recorded at the site"

key-files:
  created: [src/Settings.h, src/Settings.cpp, src/GearButton.h, src/GearButton.cpp, src/AboutOverlay.h, src/AboutOverlay.cpp]
  modified: [src/Chassis.cpp, src/HeaderBar.cpp, src/PluginProcessor.cpp, src/PluginEditor.cpp, scripts/build-windows.sh]

key-decisions:
  - "A native PopupMenu, so the only invented geometry in the plan is one button"
  - "The store opens its file per operation, because a static PropertiesFile pins JUCE's TimerThread past shutdown"
  - "No shared base with KitOverlay: the solver they share was already hoisted at 07-02"
  - "The MSVC stall is the TEST EXE holding a pipe open, not a general WSL interop rule — the first diagnosis over-generalised and /simplify caught it"

patterns-established:
  - "Settings are a table of structs with the enum pinned to the keys by static_assert"
  - "A clickable row's hit area is the text's width, not the row's"

duration: ~5h
started: 2026-09-21T22:10:00-03:00
completed: 2026-09-22T00:40:00-03:00
description: "The gear menu: four settings that persist globally, an ABOUT panel with working links, and the MSVC stall finally diagnosed"
type: Summary
about: "Forró Box"
---

# Phase 8 Plan 02: The gear menu — Summary

**A gear beside the logo opens a menu. Theme, corner radius, accent intensity and default step count
persist globally and survive a host restart; the last item opens an ABOUT panel crediting both
authors with clickable links. And the MSVC stall that had cost four build cycles turned out not to
be the test binary at all.**

## Performance

| Metric | Value |
|--------|-------|
| Tasks | 4 of 4 + 2 checkpoints, both approved |
| Checks | 4053 → **4082**. One deleted, with its replacement named: a `check` whose every term was typed in the test file became four `static_assert`s beside the constants they guard |
| `/simplify` | 4 angles; 1 behavioural defect, 2 gate bypasses, 1 unfalsifiable check of mine |
| Mutations | 17, each confirmed applied on disk before its result was read |
| `/code-review` | twice, as the plan required — 9 findings |
| Files | 6 created, 16 modified |

## Acceptance Criteria Results

| Criterion | Status | Evidence |
|-----------|--------|----------|
| AC-1: settings persist globally, not per project | **Pass** | Round-trip through a real file; a second handle on the same path; `testSettingsAreNotProjectState` greps a saved project for all five keys |
| AC-2: a missing/unreadable/hostile file costs nothing | **Pass** | Absent, non-XML, out-of-range, and eight "almost numbers" (`-`, `+`, `--`, `1-2`, `3.5`, ` `, `0x40`, an overflowing value) |
| AC-3: each setting changes what it claims, immediately | **Pass** | Measured in pixels via `maxPixelDifference`, and restored exactly on the way back |
| AC-4: default steps seeds a fresh instance, never overrides a restore | **Pass** | Both directions plus the disagreeing case; M-T2 fires on the restore check |
| AC-5: ABOUT names both authors and the project | **Pass** | Ink per row against the panel's own empty padding; links resolved by `urlAt` |

## Verification

| | Result |
|---|---|
| GCC 13 / Clang 18 / MSVC 2022 | **4082 / 4082**, real exit 0, 0 warnings from our sources |
| Cross-checks | all five, run explicitly |
| Install | hashes match, moduleinfo clean, `/mnt/d/VST3` — and the script now completes **unattended** |
| Checkpoints | main one approved; the follow-up on links approved |

## What Was Built

| File | Change | Purpose |
|------|--------|---------|
| `src/Settings.{h,cpp}` | **Created** | The plugin's first global store. Five settings in one table, the enum pinned to the keys by `static_assert`, every read clamped and strictly parsed |
| `src/GearButton.{h,cpp}` | **Created** | The only invented control in the plugin. A `Path`, not a glyph — neither embedded font carries U+2699 |
| `src/AboutOverlay.{h,cpp}` | **Created** | Scrim, panel, the kit overlay's entrance curve, and three clickable links |
| `src/Chassis.{h,cpp}` | Modified | The menu: build, dispatch, apply, show |
| `src/HeaderBar.{h,cpp}` | Modified | Places the gear and forwards its click |
| `src/PluginProcessor.cpp` | Modified | The step-count seed, through `writeParameter` |
| `src/PluginEditor.cpp` | Modified | Applies the stored settings before the first paint |
| `scripts/build-windows.sh` | Modified | `run()` no longer pipes a Windows process |

## Decisions Made

| Decision | Rationale | Impact |
|----------|-----------|--------|
| A native `juce::PopupMenu`, and `KitOverlay`'s treatment for ABOUT | `PLANNING.md:902` calls the prototype's tweaks panel "prototype-only scaffolding — **not part of the plugin design**", and no gear exists in `forrobox.css`, `app.js` or the chassis layout. A menu JUCE draws against the existing `LookAndFeel` means the only invented geometry in the whole plan is one button | Taken with the user at planning |
| The store opens its file **per operation** | `juce::PropertiesFile` derives from `juce::Timer`, and `Timer` holds `SharedResourcePointer<TimerThread>` as a member (`juce_Timer.h:144`) — a static owning one pins the timer thread past `shutdownJuce_GUI()`, and `~TimerThread` asserts "a timer has outlived the platform event system". Also removes the pointer-swap race and stops a cross-process write reverting a setting it never touched | Verified in JUCE's source before acting |
| No shared base with `KitOverlay` | Its overlay-ness is a scrim, a curve and a close box; only the curve is real code, and 07-02 already hoisted `cubicBezierEase` into `Surface.h` for the DRAG MIDI pulse. 06-01's rule is to extract when a second real caller exists — here the second caller already has what it needs | Recorded in `AboutOverlay.h` |
| `PluginEditor` applies the stored settings, not `Chassis` | A `Chassis` is handed a `LookAndFeel` it does not own; seeding that from a global store is the job of whoever created it. Doing it in `attachParameters` made `ChassisRig { light }` silently dark and failed six light-theme checks | 08-01's lesson one level over |

## Deviations from Plan

| Type | Count | Impact |
|------|-------|--------|
| Scope additions | 2 | Clickable links (user request at the checkpoint); the `build-windows.sh` fix |
| Files outside `files_modified` | 3 | `scripts/build-windows.sh`, `src/GearButton.*`, `src/AboutOverlay.*` (the plan named the last two only as `AboutOverlay`) |
| Deferred | 6 | All in STATE.md with the named fix: a `SettingsMenu` extraction, the `PropertiesFile` choice, splitting `applyStoredSettings`, the step seed as a parameter default, what the test exe leaves running, and the `ABOUT.md` cross-check |

### 1. The MSVC stall was misdiagnosed four times, and the fix was in the build script

Recorded across 07-03, 08-01 and 08-02 as "the test exe hangs". It does not. Measured three ways on
the same binary:

| | |
|---|---|
| `exe > file` | exits 0 |
| `exe 2>&1 \| tee file` | **never exits** (timed out at 240 s) |
| `exe > file`, then `cat` | exits 0 |

`run()` piped every command through `tee`, so the script never reached its install, hash and
moduleinfo steps, and each occurrence cost a full MSVC cycle plus a manual `taskkill`. Capturing to a
temp file fixes it.

**Two wrong explanations were written down before the right one.** The first, recorded in STATE.md
hours earlier, was a non-joined JUCE `TimerThread`; running the binary three ways killed it. The
second, written into `run()`'s own comment, was "piping a Windows process into a Linux reader under
WSL interop hangs" — which `/simplify`'s altitude pass showed this script falsifies twice per run
(`cmd.exe | tr` at line 64 on every invocation, `powershell.exe` through three readers at line 327).
The explanation that survives is narrower: **this binary** leaves something holding the pipe's write
end, so `tee` never sees EOF. The fix is unchanged; the comment is not.

### 2. The first fix for that was worse than the bug

`run()`'s replacement did `"$@" > "$out" 2>&1` followed by `status=$?`. Under `set -euo pipefail` a
failing command aborts the shell **at that line**, so the `cat` and `tee` below never ran: a compile
error would have printed nothing but its own `+ cmake --build …` echo and reached neither the
terminal nor the log. Found by `/code-review`, reproduced before fixing, and fixed with
`|| status=$?`. **A loud failure was replaced with a silent one**, which is strictly worse, and it
would have shipped.

### 3. Clickable links, added after the checkpoint at the user's request

The plan said "NO LINK-OPENING unless it is trivial… a link that silently does nothing is worse than
text". The user asked for links at the checkpoint, which settles it. The mitigation for the plan's
objection is that the URL stays readable either way — the link is on top of the text, not instead of
it. The stored form became the FULL url with the display derived, because two fields would have been
two spellings of one address free to drift.

## Issues Encountered

| Issue | Resolution |
|-------|------------|
| The menu's accent ids collided with the step ids | `kAccentBase + 100` was `kStepsBase` exactly, so "Default step count → 16" set the accent to 100% and the step setting was unreachable from the UI. Ids are indices now, with a `static_assert` that no band runs into the next |
| The suite read the developer's real preferences | `PluginEditor` seeds from the store, so every editor test did. Isolated for the whole run in `TestMain` — and the first version of that guard did not work, because `ScopedTestFile` reopened the DEFAULT on destruction instead of the previous target. Caught by writing a Light theme into the real file: 13 checks failed with the guard supposedly in place |
| The preferences file landed in `~/Forró Box/` | JUCE resolves Linux as `~/<folderName>` verbatim. A plugin does not put a visible folder in somebody's home directory; it is `~/.config/Forró Box/` now. **Caught only because the plan required the path to be printed** |

## Instruments That Could Not See What They Measured

Two, both mine, both repeats of traps this project has already recorded.

| Instrument | Why it was blind |
|---|---|
| The ABOUT ink check | `contrastMass` SUMS over its area, so a 420 px band clears any small threshold on rounding noise alone. It passed with **every** `drawTracked` call disabled. 04-01's rule verbatim: a threshold an empty region also clears detects nothing. It measures each row against the panel's own empty padding now |
| The gear's hover check | `--fg-dim` is `--fg` at **alpha 0.5** — the same RGB. Rendered onto a transparent image, `getPixelAt` un-premultiplies and both states read `E8E8E8`; the whole difference lived in an alpha no brightness instrument could see. It reported 159.5 against 159.5 under MSVC while passing on Linux, i.e. it read rasteriser noise on both. 04-03's trap. It composites over the panel colour now — `838383` against `E8E8E8` |

## `/simplify` — what it found, and what was measured

Four angles. The **reuse** pass found a real behavioural defect; the **altitude** pass corrected my
own diagnosis of the MSVC hang; the **efficiency** pass priced four suspicions and three were free.

| Fixed | Why it mattered |
|---|---|
| `GearButton` dropped the right-click clause | `Surface.h`'s `isPlainClickInside` carries two: a release inside, and *a right-click belongs to the host*. The gear opened its menu on right-click AND swallowed the DAW's parameter context menu. `HitZone.h`'s header records this clause being dropped by "every container that hand-rolled its own hit test" — this was the next one |
| `about::kScrimPercent` and `settings::cornerRadiiPx` bypassed cross-check gates | `kit::kScrimOpacity` is pinned to css:555 by `verify-geometry.py:838`; `theme::kCornerRadius` is pinned to `--r` by `verify-theme.py:272`. A stylesheet change would have moved the enrolled constant and silently left mine — two overlays with different scrims, both gates green |
| A check of mine that could not fail | The id-collision guard read `check (300 + 4 <= 400, …)`, every term typed in the test file. It could not observe a renumbering in `src/`. Now four `static_assert`s beside the enum, confirmed to fire at compile time when a band overlaps |
| `settings::sameKey` re-implemented `ids::detail::sameId` | Two hand-rolled `strcmp`s with different loop shapes, one include apart, under a docstring naming the other |
| The link width was computed twice | Once for the hit area, once for the underline that shows where the hit area is — the exact failure `Layout`'s docstring exists to prevent, solved for rows and reintroduced for widths |
| Four copies of the temp-settings-file setup | Two of them did the thing the third one's comment forbids. Hoisted into `tests/RigStart.h` |
| The ABOUT panel was sized only on open | A resize while it was showing stranded it at the old size |
| Plus | the clamped `secondsSinceLastTick` overload; a dead child-repaint loop; dead `fileLocation`/`resetAllForTest`; three null checks on a `make_unique` result |

### The `run()` comment asserted a law this script falsifies twice per run

The altitude pass caught it: *"piping a Windows process into a Linux reader under WSL interop hangs"*
is wrong. Line 64 pipes `cmd.exe` into `tr` on **every** invocation; line 327 pipes `powershell.exe`
through three readers on the fatal-error path; `cmake.exe` drove MSBuild and dozens of `cl.exe`
children through the old pipe without trouble. The script always died at the TEST exe specifically.

The narrower explanation that fits all three measurements is that **this binary** leaves something
alive holding the pipe's write end, so `tee` never sees EOF. The capture is kept — it is cheap and it
works — and the comment now states what was observed instead of generalising it. Finding what the exe
leaves running is recorded in STATE.md as the real cure.

### Measured, and left alone

| Suspicion | Measured | Verdict |
|---|---|---|
| `Settings::get` opens the file per call | **12.4 µs**; ~50 µs per editor open, ~130 µs per gear click | Free. Two to three orders below visible, and nothing here is reachable from `processBlock` |
| `AboutOverlay::layout()` re-walked per paint and per `urlAt` | **3 ns** | Free — `boxHeight` is a constexpr table lookup |
| The entrance repaint | 6 frames at ~5.7 ms against a 33 ms budget | Free; the timer stops at completion |
| Suite wall time | 3.11 s → **3.33 s** (+7.5%) for 3937 → 4083 checks (4082 after `/simplify` replaced one with `static_assert`s) | 82% attributed to the 13 new tests; 66% of the delta is one test |

**`testSettingsChangeTheChassis` at 130 ms was NOT optimised, deliberately.** The two offered savings
cost either coverage or correctness: dropping three of four "restores the original" checks loses three
*distinct* claims (each setting's inverse restoring exactly, not one claim repeated), and windowing the
comparisons makes the window's choice a new assumption that must contain a rounded corner and an
accent fill. For ~1-2% of suite wall, in a suite whose largest single item is `writeReferenceRenders`
at **722 ms (21%)**, untouched by this plan.

## Next Phase Readiness

**Ready:** 08-03 (the display font) has its store entry declared and its menu band free. The
remaining Phase 8 scope is the `CACHAÇA` easter egg and the Ciclotron™ treatment.

**Concerns:**
- **08-03 has a spec problem to settle with the user first.** Space Mono publishes only 400 and 700
  and is not variable, so it cannot supply the 500 and 600 the type scale uses; JetBrains Mono is
  variable-only, which is 04-01's Space Grotesk situation and is solvable by instancing.
  `PLANNING.md:885` calls both "optional".
- `about::authors` is never cross-checked against `ABOUT.md`.
- A tagged blob with no `<STATE>` child still restores an empty grid claiming CAMPINA (from 08-01).

**Blockers:** None.

---
*Built with PAUL Framework v1.4 · https://chrisai.cv/skool*
*Phase: 08-polish, Plan: 02*
*Completed: 2026-09-22*
