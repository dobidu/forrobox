---
phase: 09-content-convolution
plan: 06
subsystem: presets
tags: [presets, grooves, state, ui]

requires:
  - phase: 09-content-convolution
    plan: 04
    provides: "the sixteen grooves this cycler reaches"
  - phase: 09-content-convolution
    plan: 05
    provides: "the pattern slots a groove load must reset"
provides:
  - "State::activeGroove — which groove is playing, by id, persisted"
  - "loadGroove / cycleGroove — the bank walked, clamped, loaded whole"
  - "grooveInProfile — id resolution that degrades instead of failing"
  - "The header cycler wired, following state it did not cause"

affects: [09-07, 09-08, 09-09]

tech-stack:
  added: []
  patterns:
    - "Record the identity instead of marking dirty, when the thing HAS an identity"

key-files:
  created: []
  modified: [src/ForroBoxState.h, src/ForroBoxState.cpp, src/ParameterIDs.h,
             src/Profiles.h, src/Profiles.cpp, src/PluginProcessor.h,
             src/PluginProcessor.cpp, src/HeaderBar.h, src/HeaderBar.cpp,
             src/Chassis.h, src/Chassis.cpp,
             tests/StateRoundTripTest.cpp, tests/UiTest.cpp]

key-decisions:
  - "Store the groove's ID, which is why dirty stays false"
  - "An id, not a bank index — activeProfile's precedent"
  - "presetIdx is NOT repurposed, and its doc says so"
  - "A groove load resets the slots — reversing this plan's own instruction"

patterns-established:
  - "A preset load replaces the pattern state, all eight slots included"

duration: ~2h
started: 2026-09-23T18:30:00-03:00
completed: 2026-09-23T20:00:00-03:00
description: "The preset cycler — the top control loads a groove from the active profile's bank"
type: Summary
about: "Forró Box"
---

# Phase 9 Plan 06: the preset cycler — Summary

**`‹ PÉ-DE-SERRA 01 ›` stopped being a label. The header's cycler walks the active profile's bank,
each selection loads that groove's patterns and its feel, the state records which groove, and
save/reload brings it back. The twelve grooves approved at 09-04 are reachable at last.**

## Performance

| Metric | Value |
|--------|-------|
| Tasks | 2 of 2 |
| Checks | 4824 → **4848** |
| Mutations | 5, each confirmed applied on disk before its result was read |
| `/code-review` | once after Task 2 — **7 findings, all 7 fixed** |

## Acceptance Criteria Results

| Criterion | Status | Evidence |
|-----------|--------|----------|
| AC-1: a groove loads completely | **Pass** | Patterns, plus bpm/swing/cachaça as parameter writes a host can see. Timbre and the bateria mute unchanged — those are the profile's |
| AC-2: the state says which groove, dirty stays false | **Pass** | `activeGroove` holds the id; `dirty` false, because a factory groove is not an edit |
| AC-3: save and reload | **Pass** | `activeGroove` round-trips; the screen and the patterns come back with it |
| AC-4: an unknown id is not lost | **Pass** | Preserved verbatim, resolved to the default so something plays — **and the screen now shows the raw id rather than claiming the default's name** |
| AC-5: the cycler follows the profile | **Pass** | Clamps at both ends, never crosses into another bank, and a profile load moves the screen without a click |

## Verification

| | Result |
|---|---|
| GCC 13 / Clang 18 | **4848 / 4848**, real exit 0 |
| MSVC 2022 | **4848 / 4848** — the suite passed; the PROCESS then hung after printing. See below |
| Cross-checks | all six, run together |
| Warnings from our sources | zero |

## The decision this plan existed to make

Two consecutive reviews flagged the same thing: `applyGroove` recorded `profile.id()` and cleared
`dirty` while `State` had no groove field, so applying `campina/xote-lento` left a state claiming
"CAMPINA GRANDE, pristine".

Two ways out, and they are not equivalent. **Marking `dirty`** — 09-05's answer for slots — makes
the state honest but throws the identity away: reload restores `grooveBank[0]` and the cycler cannot
show what was chosen. **Storing the identity** is what a preset system means, and `PLANNING.md:843`
says "save/load full plugin state".

This plan stores it, so selecting a factory groove is correctly *not* an edit. 09-05 took the other
branch for a real reason — a slot's **contents** are user-edited and have no factory identity to
record — and both sites now say so where they are written.

An **ID, not an index**, following `activeProfile`'s verbatim-preservation rule (07-02). A bank
index would point at a different groove the moment a bank is reordered, and revising a groove is one
JSON edit.

## The plan's own instruction was wrong

09-06's plan said `loadGroove` *"does not reset the pattern slots either — that is `applyProfile`'s,
and selecting a groove inside a profile is not a full reload."*

`/code-review` read the consequence. `applyGroove` writes `state.lanes`, which is each channel's
**currently selected** slot — so with zabumba parked on `PAT 03`, a groove load put the new zabumba
in slot 3 while slot 1 kept the **old** groove's. Press `PAT ‹` and zabumba plays the previous groove
while the preset screen names the new one and `activeGroove` still claims it. With four channels on
four different slots, a groove's eight patterns scattered across four slots permanently.

`PLANNING.md:843` settles it: a preset load replaces the plugin state, which since 09-05 means all
eight slots per channel. The reset moved into `applyGroove` so **both** paths get it, and
`applyProfile`'s copy — which had been doing this correctly since 09-05 — was deleted as redundant.

That fix also closed the second finding: `dirty = false` was reachable with user-edited patterns
still parked, so the state advertised "pristine profile" over content the user had written, and said
so through save and reload.

## A five-phase-old assertion, inverted

`tests/UiTest.cpp` asserted *"the preset arrows do not even cycle the label — a label that changes
while nothing else does is the dishonest kind of stub."* That was correct while the cycler stored
nothing. It now stores a groove id and loads patterns and feel, so the honest behaviour inverted and
the assertion with it — with the reasoning kept, rather than the line quietly replaced.

**And one of my new assertions was wrong where the code was right.** I expected `‹` then `›` to
return to the default groove; it lands on the **second**, because `‹` clamps at the start of the
bank rather than wrapping. `app.js:561` wraps a global array — per-profile banks have none to wrap.
The user's 09-02 deviation, showing up as behaviour, and the test now says so.

## And five more findings

- **The screen kept a stale name.** `refreshFromProcessor` skipped the write when
  `activeGrooveName()` returned empty, so a recall naming an unknown profile left a groove name from
  the **wrong bank** on screen — precisely what that function's doc claims it prevents.
- **An unresolvable id was drawn as the default groove's name**, asserting something the state does
  not claim while other lanes played. It now shows the raw id, which is truthful and says where it
  came from.
- **`profileNamed` duplicated `forrobox::findProfile`**, which this same file already calls.
- **A doc comment was spliced between `selectPatternSlot`'s comment and `selectPatternSlot`** — the
  fourth instance of that slip in four plans.
- **`loadGroove` did not check the groove belongs to the profile.** A mismatched pair writes an
  identity nothing can resolve; a `jassert` keeps the public entry point honest.

## Deviations from Plan

| Type | Count | Impact |
|------|-------|--------|
| Plan instruction reversed | 1 | "Does not reset the pattern slots" — it must, and `/code-review` showed why |
| Test premise inverted | 1 | A Phase 4 assertion that documented the stub |

## Notes for Future Plans

**`presetIdx` is still not the groove selector**, and its doc comment now says so. It is persisted,
clamped 0-7, round-trip tested and has no production reader — which is exactly why the next reader
would assume it is one. Repurposing it would swap a robust id for a fragile index.

**The MSVC stall recurred a third time, and it is NOT what I twice said it was.**

Three hypotheses, three falsifications. 09-05 recorded it as correlating with a concurrent review
agent — this occurrence had the machine to itself. The working-directory theory was tested and
falsified at 09-05: 32 s either way. And while this one ran I offered on-access scanning of a
freshly linked binary, which the evidence then killed as well.

**What actually happens, with evidence this time:** the log contains
`4848 / 4848 checks passed — OK`. The suite RUNS AND PASSES. The process then stays in the process
table burning ~92% of a core — 13,219 seconds of CPU over four hours before it was killed, and
~2,000 s in the earlier occurrence at the same rate. The failure is **after `main` returns**, not
during the tests, and the `FATAL: 1 of 1 test executables failed` line is produced by the kill, not
by a check.

That is exactly the symptom `build-windows.sh:129-150` records across 07-03, 08-01 and 08-02 —
*"printed its full result and then sat in the process table forever"* — which was then diagnosed as
a pipe-buffering problem and "fixed" with `> file`. The redirect fixed a variant; the underlying
non-exit did not go away. A leaked thread or a static destructor spinning at shutdown fits every
observation and none of the three theories offered since do.

**Reading the result therefore means grepping the log, not the exit code** — which is what the
script's own comment has said since 08-02, and what was done here. The MSVC row above is that
number, from that log.

Unresolved, and now the FOURTH recorded misdiagnosis of this symptom. Worth its own plan rather
than another guess appended to a summary.
