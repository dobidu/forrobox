---
phase: 08-polish
plan: 01
subsystem: state
tags: [juce, vst3, apvts, valuetree, profiles, testing]

requires:
  - phase: 06-side-panel
    provides: "ForroBoxAudioProcessor::loadProfile, with its parameter-before-pattern order"
  - phase: 02-sequencer-clock
    provides: "setStateInformation's wholesale restore and its three early returns"
provides:
  - "A fresh instance that loads ids::defaultProfile for real — grid and BATERIA mute"
  - "tests/RigStart.h — the one definition of what a test means by a blank instrument"
  - "Checks for both halves: the fresh instance HAS the groove, a restore does NOT lose the user's"
affects: [08-polish settings menu, 08-polish ABOUT panel, any plan touching setStateInformation]

tech-stack:
  added: []
  patterns:
    - "A test states its starting pattern at the site; the product default is not inherited"
    - "A gate's CMake dependencies must equal what its script actually reads"

key-files:
  created: [tests/RigStart.h]
  modified:
    - src/PluginProcessor.cpp
    - tests/StateRoundTripTest.cpp
    - tests/VoiceTest.cpp
    - tests/UiTest.cpp
    - tests/MidiExportTest.cpp
    - CMakeLists.txt
    - scripts/verify-charset.py

key-decisions:
  - "The constructor is the right home for the default load; State's default must NOT carry the groove"
  - "loadProfile is reused, not reimplemented — its parameter-before-pattern order is a recorded finding"
  - "A tagged blob with no STATE child is deferred, not guarded: it is the pre-existing bug, not a new one"
  - "loadProfile's JUCE_ASSERT_MESSAGE_THREAD is no longer structurally guaranteed — recorded, not deleted"

patterns-established:
  - "blankInstrument / clearGrid: two verbs with real callers, no do-nothing enum arm"
  - "A fixture asserts its own usefulness inside itself, not at each use site"

duration: ~3h
started: 2026-09-21T13:43:16-03:00
completed: 2026-09-21T23:05:00-03:00
description: "A fresh instance loads CAMPINA GRANDE for real — grid and BATERIA mute — without clobbering a restored project"
type: Summary
about: "Forró Box"
---

# Phase 8 Plan 01: A fresh instance plays the profile it claims — Summary

**Inserting Forró Box and pressing play now produces a groove. PROJECT.md's only Success Metric still
reading "Not started" — *time from plugin open to a usable groove, under 30 s, zero config* — is met,
and an emptied grid still survives a save and reload.**

## Performance

| Metric | Value |
|--------|-------|
| Duration | ~3 h wall, including three full MSVC runs |
| Tasks | 4 of 4 (3 auto + 1 blocking checkpoint) |
| Qualify | 4 PASS, 0 GAP, 0 DRIFT |
| Escalations | 1 DONE_WITH_CONCERNS (Task 1, the message-thread assert — validated, recorded) |
| Checks | 3906 → **3937**, none deleted |
| Suite wall time | 3.12 s → 3.57 s → **3.11 s** (regression found and recovered during UNIFY) |
| Files modified | 7 + 1 created (plus ABOUT.md/README.md, out of band) |

## Acceptance Criteria Results

| Criterion | Status | Evidence |
|-----------|--------|----------|
| AC-1: A fresh instance plays the profile it claims | **Pass** | `testAFreshInstanceCarriesTheDefaultGroove` compares all 8×32 slots against `findProfile`'s own decoded grid and asserts the per-channel mutes; M1 and M5 both fire |
| AC-2: A restored project keeps its pattern, including an emptied one | **Pass** | `testARestoreIsNotClobberedByTheDefaultGroove` round-trips an emptied grid AND an edited one; M2 and M3 both fire |
| AC-3: Garbage or foreign state leaves the default groove intact | **Pass** | `testGarbageStateKeepsTheDefaultGroove` covers absent, unparseable and foreign blobs; M1 fires on all three |
| AC-4: The tests say when they want an empty grid | **Pass** | `tests/RigStart.h`; `AudioRig`, `ChassisRig`, 3 MidiExport sites and 3 clock sites all state their start |

## Verification

| | Result |
|---|---|
| GCC 13 | 3937 / 3937, real exit 0 |
| Clang 18 | 3937 / 3937, real exit 0, 0 warnings |
| MSVC 2022 | 3937 / 3937; hashes match, moduleinfo clean, installed to `/mnt/d/VST3` |
| Cross-checks | all five run explicitly, all pass |
| Human checkpoint | **approved**, including step 5 (the emptied-grid round trip) in Ableton Live 12 |

### Mutation controls — 7 run, each confirmed applied on disk before its result was read

| | Mutation | Failing checks |
|---|---|---|
| M1 | constructor skips the load | 10 |
| M2 | default applied AFTER the restore | 21 |
| M3 | restore ignores the saved grid | 16 |
| M4 | the load marks the state dirty | 28 |
| M5 | constructor loads a different profile than the state claims | 10 |
| M6 | *(the `/code-review` fix removed)* | **0 — the fix was a no-op** |
| M7 | a parameter absent from the tree keeps its live value | 2 |

The full set was re-run twice: once after `/simplify` restructured the checks, and again after the
efficiency fix changed the clock tests' input. A detection recorded against code that has since
changed proves nothing about the code that ships.

## What Was Built

| File | Change | Purpose |
|------|--------|---------|
| `src/PluginProcessor.cpp` | Modified (+42) | The constructor calls the existing `loadProfile (*findProfile (ids::defaultProfile))`. No new load path. |
| `tests/RigStart.h` | **Created** | `blankInstrument` and `clearGrid` — the one definition of what a test starts from |
| `tests/StateRoundTripTest.cpp` | Modified (+387) | Four new tests; the inventory section now reads *declared* defaults; three clock sites state their grid |
| `tests/VoiceTest.cpp` | Modified | `AudioRig` calls `blankInstrument`; `testFactoryDefaults` gained a control that can fail |
| `tests/UiTest.cpp` | Modified | `ChassisRig` blanks; `writeReferenceRenders` stopped re-applying the profile |
| `tests/MidiExportTest.cpp` | Modified | Three note-counting sites blank explicitly |
| `CMakeLists.txt`, `scripts/verify-charset.py` | Modified | Two gate-enrolment fixes — see Deviations |

## Decisions Made

| Decision | Rationale | Impact |
|----------|-----------|--------|
| The **constructor** is the right home, and `State`'s default must NOT carry the groove | `State`'s default is also the *degradation target* for every restore (`ForroBoxState.cpp:98`). Give it CAMPINA's notes and a partial blob silently mixes the profile into the user's work — AC-2's harm, relocated where no test would see it. `Profiles.h` includes `ForroBoxState.h`, so the inverse also inverts the header direction. And a fresh instance spans two stores: the grid is `State`, the mute is the APVTS, and only the processor sits above both | Settles a question any future plan touching startup would reopen |
| A tagged blob with **no `<STATE>` child** is deferred, not guarded | Verified real: `readFrom` returns all-zero lanes still claiming campina. But `writeTo` always writes that node, so no Forró Box project reaches it, and it is the *pre-existing* bug — a fresh instance claimed CAMPINA over an empty grid everywhere until today. Deciding what such a blob *means* is a product call the plan's boundary keeps out | Logged in PROJECT.md; revisit with the settings menu |
| `loadProfile`'s message-thread assert is **recorded, not weakened** | JUCE's VST3 factory calls `createPluginFilterOfType` with no `MessageManagerLock` (`juce_audio_plugin_client_VST3.cpp:2674`, `:4135`), so the host picks the thread. The work is safe — nothing else can reach the object yet — and `jassert` compiles out of Release | Debug/pluginval only. An assert deleted to silence a message is the guarantee deleted with it |
| **No phase transition.** ROADMAP is the authority, not the file counts | 1 PLAN and 1 SUMMARY would trip `unify-phase.md`'s heuristic into "phase complete" for the fifth time. ROADMAP lists four scope items and three are unplanned | Phase 8 continues |

## Deviations from Plan

| Type | Count | Impact |
|------|-------|--------|
| Scope additions | 3 | Two gate-enrolment fixes and one strengthened check — all found *because of* this plan |
| Files outside `files_modified` | 4 | `tests/MidiExportTest.cpp`, `CMakeLists.txt`, `scripts/verify-charset.py`, `tests/RigStart.h` |
| Fix written then reverted | 1 | A `/code-review` finding whose premise was wrong |

### 1. The charset gate scanned files it did not depend on

`verify-charset.py` rglobs all of `tests/`, but CMake enrolled only `tests/UiTest.cpp`. An em dash I
put in a `VoiceTest.cpp` message passed **every incremental Linux build** and surfaced only on a full
MSVC rebuild — costing one MSVC run. CMake now enrols `tests/*.cpp` and `tests/*.h`, and the script
scans headers too. **Controlled:** touching `VoiceTest.cpp` re-runs the gate.

### 2. `verify-profiles` had an undeclared input — third instance of the same class

Found by `/simplify`'s altitude pass. `verify-profiles.py:260` reads `src/MixBus.h` to cross-check
the TIMBRE table against `data.js`; CMake declared only `data.js`, `Profiles.cpp` and
`ParameterIDs.h`. **Editing `timbreSpecs` did not re-run the gate whose entire justification is that
a wrong digit there produces no crash, no failed build and no failing test.** Declared and
controlled: touching `MixBus.h` now re-runs it. `verify-theme` and `verify-midi` were audited and
are complete.

### 3. `testFactoryDefaults` was passing on ghost notes

Its "makes a substantial sound (peak > 0.05)" check had run against an **empty grid** since 03-03,
and its comment already claimed the constructor loaded a profile. The shipped ghost probabilities
(6–14% per channel) alone reach **0.0974**. It now renders the same processor with only the lanes
cleared and asserts the groove is clear of that floor. M1 fires on it.

### 4. A `/code-review` fix was written, then reverted — its premise was wrong

The finding: a parameter absent from a restored tree would keep the constructor's CAMPINA value,
because `updateParameterConnectionsToChildTrees` creates the child from the *live* value. That
mechanism is real, but appending the child fires `valueTreeChildAdded` → `setNewState`, which reads
the `value` property with `getDenormalisedDefaultValue()` as its fallback — so JUCE drives the
parameter to its default first. **M6 caught that the fix changed nothing.** The test survives as a
tripwire on that fallback and is controlled by M7.

## Issues Encountered

| Issue | Resolution |
|-------|------------|
| `ForroBoxTests.exe` printed 3936/3936 then never exited under MSVC — the Phase 7 stall | Killed it; the script correctly reported the test as failed and skipped the install, so it was re-run clean. Did not recur |
| Two MSVC runs invalidated by editing sources mid-build | Killed and restarted rather than trusting a mixed result |
| `/simplify` measured a **+13% suite regression** (3.12 → 3.57 s) | Not the constructor — `loadProfile` measures 2.7 µs and 451 constructions are 1.2 ms of 3.5 s. The cost was second-order: clock tests rendering CAMPINA through the voice engine, 7× per block. Three `clearGrid` calls: **3.11 s**, below the pre-08-01 baseline |

## Skill Audit

| Skill | Required | Invoked |
|-------|----------|---------|
| `/graphify` | before phase planning | ✓ |
| `/code-review` | after Task 1 | ✓ — 3 findings, 1 fixed, 1 reverted as a false premise, 1 deferred |
| `/simplify` | during UNIFY | ✓ — 4 agents; the altitude pass found a live gate bug |

## Next Phase Readiness

**Ready:** The remaining Phase 8 scope is decoration and unblocked — the `CACHAÇA` easter egg, the
Ciclotron™ treatment, the settings/gear menu, and the ABOUT panel added at this UNIFY.

**Concerns:**
- The ABOUT panel is Phase 8's **first invented control** — `PLANNING.md` specifies none. The user
  asked for it explicitly, which is the decision the design mandate requires, and it belongs behind
  the gear menu rather than on the chassis. Plan the two together.
- A tagged-but-gridless blob still restores an empty grid claiming CAMPINA (deferred above).
- The gate-dependency class is closed instance by instance. `verify-geometry` already solves it
  properly by scraping its own script; lifting that into a shared helper would close it for all five.

**Blockers:** None.

---
*Built with PAUL Framework v1.4 · https://chrisai.cv/skool*
*Phase: 08-polish, Plan: 01*
*Completed: 2026-09-21*
