---
phase: 12-settings-restructure
plan: 02
subsystem: state
tags: [parameters, STEPS, defaults, VST3, settings, decision]

requires:
  - phase: 08-polish
    provides: "08-02's step-count preference seeding a fresh instance through a bracketed write"
  - phase: 12-settings-restructure
    plan: 01
    provides: "SettingsMenu and settings::applyTo"
provides:
  - "the decision, with its reasons, that STEPS' declared default stays 16 and the preference stays a write"
  - "a check that the declared default is 16 even with the preference at 32"
affects: [Phase 13 (the broadcast never touches parameter info)]

key-files:
  modified: [src/PluginProcessor.cpp, src/PluginProcessor.h, src/SettingsMenu.cpp, tests/StateRoundTripTest.cpp, tests/UiTest.cpp]

key-decisions:
  - "REVERSED after /code-review, by the user: the preference stays a bracketed constructor write; STEPS' declared default stays PLANNING.md's 16"

duration: ~1 session
completed: 2026-10-01
description: "The plan made the step-count preference STEPS' declared default; /code-review showed what that costs, and the user reversed it: the default stays a fixed 16 and the preference stays a write, now recorded and guarded"
type: Summary
about: "Forró Box"
---

# Phase 12 Plan 02: The step-count default (reversed)

**The plan was applied as written, and green, then reversed on review evidence.** A declared default
read from the user's settings store made STEPS' parameter info depend on the machine and the
instance. The user chose to keep the write. The code now says why. A check makes sure the declared
default stays 16 whatever the preference says.

## What /code-review showed (required: processor state)

The declared default (`createParameterLayout` reading `Settings::shared().defaultStepChoiceIndex()`)
had three costs the planning had not weighed:

1. **A restore that omits STEPS** lands on each machine's preference, not on 16. The same project
   opens differently on two machines.
2. **Instances disagree on parameter info**: each one declares its default when it is built. A
   preference change in an instance's own menu never moves that instance's default. VST3 assumes
   parameter info is static.
3. **Validation stops being hermetic**: pluginval, and so the gate, would read the user's real
   settings file.

The only gain was that a host's "reset to default" would mean the preferred step count. **The user
chose to revert to the write.**

## Acceptance Criteria Results (as re-scoped by the decision)

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: the preference is the declared default | **Superseded** | Reversed by the user. The declared default is 16 and is checked with the preference at 32 |
| AC-2: nothing writes it | **Superseded** | The bracketed `writeParameter` stays. Its comment now records the decision and the three costs. The 08-02 checks are unchanged and green. **Mutation:** declaring the default from the preference FAILS the new check (5023/5024) |
| AC-3: green everywhere | **Pass** | Gate 3/3. 5024/5024 on GCC 13, Clang 18 and MSVC 2022. The Debug suite runs 5026 checks with 0 unexpected assertions (2 expected). Windows pluginval PASS |

The other /code-review findings:
- **Stale comments (5, 6):** accurate again once the write is back.
- **`createParameterLayout` doc (4):** said 45 parameters; now 12 + 7×5 = 47.
- **Coverage (7, 8):** the new declared-default check covers it.
- **The settings read on a loader thread (9):** it predates 12-02. Deferred to Phase 13, which replaces the store.

## /simplify at Phase 12 close (required): four agents

**Applied:**
- **`SettingsMenu::build` reads each setting once** instead of once per item. That cuts 13 settings-file parses per menu open to 5. A mutation pointing the radius ticks at another cached value FAILS 2 checks.
- **`kThemeNames`**: the theme band is sized from a table like every other band. Its `static_assert` checks the table against `settings::info (Setting::theme)`. The literal `2` is gone from three places.
- **The About label** is one `fromUTF8` literal.
- **UiTest:** the comment that pointed the collision guard at `Chassis.cpp` now names `SettingsMenu.cpp`. The redundant "900 is the About id" is gone.

**Deferred (Phase 13, where the broadcast decides these seams):**
- **One entry point** that pairs `applyTo` with `repaintAll`. Three callers repeat the pair today, and the broadcast would be the fourth.
- **Where `applyTo` lives.** It is a store-to-LookAndFeel bridge, not part of the menu. It also writes the process-global mono family.
- **Single-open snapshots for `applyTo`**, so it reads the file once instead of 4 times. Phase 13 already owns replacing `PropertiesFile`-per-open.

**Skipped:**
- **A band table** driving build, apply and the accessors. The bands differ in labels and value mapping. The `static_assert`s and the named-id tests already catch the drift it would prevent.
- **Inline `constexpr` accessors** and the defensive `jlimit` in `accentStepPercent`.
- **Hoisting `declaredStepsIndexOf`** into a file-level helper. It is one local lambda.

## Deviations

| Type | Count | Notes |
|------|-------|-------|
| Direction reversed | 1 | The user's decision after /code-review. The plan's AC-1 and AC-2 are superseded and the decision is recorded |
| Scope addition | 1 | /simplify's SettingsMenu fixes (12-01 code), at phase close |

## Next
**Phase 13 — Multi-instance.**

---
*Phase: 12-settings-restructure, Plan: 02 · Completed: 2026-10-01*
