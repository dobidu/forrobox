---
phase: 12-settings-restructure
plan: 01
subsystem: ui
tags: [settings, PopupMenu, refactor, LookAndFeel, tests]

requires:
  - phase: 08-polish
    provides: "08-02's settings menu and the global Settings store"
provides:
  - "src/SettingsMenu.{h,cpp}: ids/bands/static_asserts, build (Settings), apply (id, Settings) -> Result, named item ids"
  - "settings::applyTo (ForroBoxLookAndFeel&, const Settings&): the store pushed into the UI, no repaint"
  - "Chassis::repaintAll + handleSettingsMenuResult (the one dispatch seam)"
affects: [12-02, Phase 13 (the broadcast calls applyTo + repaintAll on the other instance)]

key-files:
  created: [src/SettingsMenu.h, src/SettingsMenu.cpp]
  modified: [src/Chassis.h, src/Chassis.cpp, src/PluginEditor.cpp, CMakeLists.txt, src/Typography.h, src/Typography.cpp, tests/UiTest.cpp]

key-decisions:
  - "Result enum (dismissed/changed/about/unknown) replaces bool; unknown never writes"
  - "applyStoredSettings deleted, not wrapped"

duration: ~1 session
completed: 2026-10-01
description: "The settings menu is its own unit with a typed result, applying the store is split from repainting, and the tests name menu items instead of numbering them"
type: Summary
about: "Forró Box"
---

# Phase 12 Plan 01: SettingsMenu and applyTo

**The settings menu moved out of `Chassis` into `SettingsMenu`, and applying a stored setting is now `settings::applyTo` plus one `repaintAll()`.** The tests address menu items by name. Renumbering a band leaves every check green.

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: SettingsMenu owns the menu | **Pass** | The ids, bands, `kAccentSteps` and every `static_assert` moved verbatim with their comments. `build` produces the same items and ticks, and the existing tick checks pass. `apply` → `Result`. `Chassis` holds no menu-id knowledge |
| AC-2: applying is split | **Pass** | `applyTo` does the four setters (mode, radius, accent, mono family); `repaintAll` does the one root repaint; the editor applies before the first paint. `applyStoredSettings` is gone, and its four comment mentions are updated |
| AC-3: tests stop transcribing | **Pass** | 29 literal ids became accessors (23 call sites plus 6 tick lookups). Only `0` and `55'555` remain, deliberately. Mutations: renumbering the accent band (300 → 350) stays 5023/5023 green; writing the index instead of the percent FAILS 4+ checks |
| AC-4: no behaviour change | **Pass** | 5023/5023 on GCC 13, Clang 18 and MSVC 2022 (+5 checks: direct `SettingsMenu::apply`, one per `Result`); Debug suite 5025, 0 unexpected; gate 3/3; Windows pluginval PASS |

## Files

| File | Change |
|------|--------|
| `src/SettingsMenu.h/.cpp` | Created: the menu unit and `settings::applyTo` |
| `src/Chassis.h/.cpp` | The menu block removed; `repaintAll`, `handleSettingsMenuResult`; `showSettingsMenu` builds through `SettingsMenu` |
| `src/PluginEditor.cpp` | `applyTo` + `repaintAll` before the first paint |
| `CMakeLists.txt` | `src/SettingsMenu.cpp` |
| `src/Typography.h/.cpp` | Comment references updated |
| `tests/UiTest.cpp` | Named ids; direct `SettingsMenu::apply` checks; the `applyTo` path |

## Deviations

| Type | Count | Notes |
|------|-------|-------|
| Naming | 1 | The plan said `handleSettingsMenuResult` *or* a seam; it is that name |
| Accessors added | 2 | `accentStepPercent (i)` and `numAccentSteps()`, so a test can name the percent an accent item writes |
| Clang warning fixed | 1 | A lambda capture made unused by the move |

## Skill audit
Not triggered: UI structure only, so `/code-review` is not required.

## Next
**12-02:** the step-count preference as STEPS' declared default.

---
*Phase: 12-settings-restructure, Plan: 01 · Completed: 2026-10-01*
