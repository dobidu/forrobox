---
phase: 09-content-convolution
plan: 02
subsystem: content
tags: [profiles, grooves, codegen, cross-check, schema]

requires:
  - phase: 09-content-convolution
    plan: 01
    provides: "assets/profiles.json, the generator, and the six-gate discipline this plan extends"
provides:
  - "A groove BANK per profile — Groove, kMaxGroovesPerProfile, grooves(), defaultGroove()"
  - "ids::profileInfos generated, closing the last hand transcription in the groove path"
  - "descClaims — each description declares what it asserts, checked in both directions"
  - "A cross-check that compares every groove of every bank, with its counts asserted"

affects: [09-03, which fills the banks; 09-05, which reaches them]

tech-stack:
  added: []
  patterns:
    - "A bank of one must take the same path as a bank of eight"
    - "Prove the UNMUTATED case passes before trusting that the mutated one fails"

key-files:
  created: []
  modified: [assets/profiles.json, scripts/build-profiles.py, scripts/verify-profiles.py,
             src/Profiles.h, src/Profiles.cpp, src/ParameterIDs.h, CMakeLists.txt,
             tests/StateRoundTripTest.cpp, tests/UiTest.cpp]
  generated: [src/ParameterIDs.h, src/Profiles.cpp, data.js, "Forró Box (standalone).html"]

key-decisions:
  - "Per-profile banks, over the spec's flat eight — the user's call, recorded as a deviation"
  - "data.js keeps a top-level `patterns`, because app.js is read-only and reads it"
  - "The C++ keeps only the bank; `patterns()` is an accessor, not a second copy"
  - "The fingerprint walks the whole bank, and did not move"

patterns-established:
  - "Four consumers from one JSON, all four enrolled"

duration: ~2h
started: 2026-09-23T10:30:00-03:00
completed: 2026-09-23T12:30:00-03:00
description: "The groove bank — each profile carries a list, and not one note changed"
type: Summary
about: "Forró Box"
---

# Phase 9 Plan 02: the groove bank — Summary

**Each regional profile now carries a bank of grooves instead of one. The schema, the C++ storage,
the generator and the gates all grew to hold it, and not one note changed — proved by a fingerprint
that was repointed at the whole bank and did not move. `ids::profileInfos` stopped being
hand-transcribed in the same plan, which was the gap 09-01 named in `Profiles.cpp`'s own banner.**

## Performance

| Metric | Value |
|--------|-------|
| Tasks | 2 of 2 |
| Checks | 4412 → **4472** |
| Mutations | **15**, each confirmed applied on disk before its result was read |
| Generated files | 3 → **4** (`ids::profileInfos` joined) |
| `verify-profiles` | 45 fields, 10 declared claims, 36 shape checks |
| `/code-review` | once after Task 2, as the plan required — **8 findings, all 8 fixed** |

## Acceptance Criteria Results

| Criterion | Status | Evidence |
|-----------|--------|----------|
| AC-1: not one note changes | **Pass** | The velocity fingerprint is `0x313274a06c6ba3e1`, unchanged — and it was **repointed at the whole bank** rather than the default groove, so the number holding still is the proof that every bank contains exactly its profile's own groove |
| AC-2: the whole bank is checked | **Pass** | Every groove compared by position, id, name and eight lanes. A digit in a non-first groove, a reordered bank, a wrong `grooveCount` and a groove no consumer carries all exit 1 naming the profile and the groove |
| AC-3: profileInfos is generated | **Pass** | `git diff` on `src/ParameterIDs.h` empty after generation; a description reworded in the JSON alone exits 1 naming the file and the line |
| AC-4: both prototypes still run | **Pass** | `buildGroove` under Node yields **231 hits**, unchanged; `patterns === grooves[0].patterns` for all four; the standalone page carries the bank |
| AC-5: a bank of one is not special-cased | **Pass** | No `grooveCount == 1` path exists. `grooves()` is the only iterator and nothing outside `Profiles.h` touches `grooveBank`. **Proved by adding a second groove and finding the parser broken** — see below |

## Verification

| | Result |
|---|---|
| GCC 13 / Clang 18 / MSVC 2022 | **4472 / 4472**, real exit 0, 0 warnings from our sources |
| Cross-checks | all six, run together |
| Fingerprint | `0x313274a06c6ba3e1`, unchanged and printed |

## Decisions Made

| Decision | Rationale | Impact |
|----------|-----------|--------|
| **Per-profile banks**, over the spec's flat eight | `PLANNING.md:843` names eight FLAT preset labels and `app.js:561`'s `cyclePreset` never looks at `activeProfile`. Those eight are *rhythms*; the four profiles are *regions* — orthogonal axes. The ROADMAP's assumption that they were the same thing was recorded as an assumption precisely so it could be corrected here, and reading the code corrected it | The user chose per-profile banks. The cycler's contents will change with the profile, which the design source does not describe — a **sanctioned deviation** with 08-01's ABOUT-panel standing |
| `data.js` keeps a top-level `patterns` | `app.js:135` is `S(p.patterns[key])` inside `buildGroove`, and `app.js` is READ-ONLY. Moving the patterns under `grooves` breaks both prototypes on boot | The one place the consumers differ in SHAPE rather than syntax, and the generator says so |
| The C++ keeps only the bank | A `patterns` member beside `grooveBank[0]` is two copies of eight strings that can disagree — the drift this phase exists to remove | `patterns()` is an accessor. The test asserts it returns the same POINTER, not an equal value |
| The fingerprint walks the bank | Digesting the default groove would have left 09-03's content unpinned until someone remembered to extend it | It covers whatever 09-03 adds, automatically |
| `kMaxGroovesPerProfile = 8` | `PLANNING.md:843` gives the cycler eight labels and `:844` gives each channel eight slots. A bank larger than any control can select is data nothing can reach | `validate` refuses a ninth, reading the cap out of `Profiles.h` rather than typing `8` |

## The bug that a passing mutation would have hidden

**`{{` is two braces, and the bank parser read one.** `Profile::grooveBank` is a
`std::array<Groove, 8>`, so its initialiser opens with `{{` — the array's brace and the inner
C-array's. Brace-matching the outer one yields a body that *is* the inner brace, whose single
top-level entry is the entire groove list. A two-groove bank therefore parsed as **one groove with
twenty string literals**.

It would have gone unnoticed. Every bank in this plan holds exactly one groove, so the whole suite
was green and all six gates passed. It surfaced because the mutation script asserted that an
**unmutated two-groove bank passes** before mutating a digit inside it — and that line never printed
while the mutation's `exit 1` did. Without that assertion the mutation would have been recorded as a
detection, and the real detection would have been a parse error.

**That is this plan's lesson, and it generalises: a mutation proves nothing unless the unmutated
case is known to pass.** The project already knew the converse — *a failed build is not a
detection*, which caught a stale binary at 09-01. This is the same law from the other side.

## Two comments this plan made false, corrected in it

Both were true when written and stopped being true in this plan's own first task — the failure
mode Phase 6 recorded as *"a green suite proves nothing about a check that cannot fail — and
neither does a comment"*.

- `src/Profiles.cpp`'s banner named the `profileInfos` gap as something 09-02 walks into. Task 1
  closed it. The banner now records what replaced it, including that `grooveCount` bounds the bank.
- `verify-profiles.py`'s docstring listed "`ids::profileInfos` is still hand-written" as the first
  of three things it owns. It is generated now. What the script actually owns there is sharper and
  is worth stating: **generating prose from the JSON makes the text consistent with the data, not
  true of it.** `check_descriptions` is the only thing that checks the difference.

## What `/code-review` found

Eight findings, none of which the six gates could see, and **all eight were in this plan's own new
code**. It was pointed at one question — *can a wrong, unreachable or duplicated groove reach the
plugin with every gate green?* — and it cleared the three mechanisms I had worried about (the
value-initialised tail is fenced three independent ways, `patterns()` cannot drift from the bank,
and `data.js`'s duplicated `patterns` is byte-compared) before finding the gap somewhere else
entirely: **the content and label dimension nobody was checking.**

### A regression I introduced by collapsing two branches

`check_descriptions` used to read `elif not above_average and mine >= average`. I rewrote the pair as
`(arg == "above") != (mine > average)` — which is correct for `above` and **quietly wrong for
`below`**: a value sitting exactly ON the four-profile average passed, while the `above` side still
rejected it. The two directions had become asymmetric.

Reproduced exactly: `petrolina.cachaca = 20` makes the mean exactly 20.0, and `cachaça baixa` then
passed with exit 0. Both sides now reject equality, because a profile claiming `cachaça baixa` at
precisely the mean is not making a true statement in either direction.

### A check I added that could never fire

I asserted `claims_checked == sum(len(descClaims))` — but `check_descriptions` increments
unconditionally inside `for phrase in claims`, so its return value *is* that sum by construction.
The scenario its own message described is caught one function up. **This is the exact class the
file's own comments are written against**, two of which I had written in this plan. Deleted, with
the reasoning recorded and a note on why `check_pattern_shape`'s count assertion is genuinely
different: that one walks a nested structure and can skip a level, which a mutation proved.

### Two grooves could be identical, and 09-03 fills these banks by copy-and-edit

Uniqueness was enforced on `id` alone. Two entries in one bank could carry the same **name** and
byte-identical **patterns** and pass every gate — a cycler offering the same groove twice under two
labels. Both are refused now, and so is an id that is not the lowercase-kebab its own doc comment
promises, which 09-05 will write into saved plugin state.

### The quiet one

Groove `id` and `name` are interpolated into C++ *and* JavaScript string literals with no escaping.
A quote is loud — uncompilable C++. **A backslash is silent**: `render_cpp` emits `"A\nB"`, the
compiler consumes the escape, and the reader's regex returns the raw source text, so the two compare
equal and the shipped string differs from its source with every gate green. Refused rather than
escaped, because no groove name needs either character and 09-03 adds ~28 names through this path.

### And two more

- **The velocity bound stayed on `grooves[0]`.** `testNoProfileReachesFullVelocity` pins the pad
  renderer's group-opacity threshold against "the loudest a profile can play" — which from 09-05 is
  a bank-wide property. I widened the fingerprint to the bank in this plan and left this one behind
  to a mechanical `patterns` → `patterns()` edit.
- **The enrolment failure, for the fifth and sixth time.** `src/Profiles.h` was not an input to
  `verify-profiles` although this plan reads a second constant out of it, and
  `verify-generated-profiles` declared neither `scripts/verify-profiles.py` nor `src/MixBus.h`
  despite `build-profiles.py` importing `validate`, `load_profiles` and `TIMBRE_INDEX` from that
  module — so a new bank rule could not re-run the generator gate that depends on it.

## Deviations from Plan

| Type | Count | Impact |
|------|-------|--------|
| Plan claim wrong | 1 | The plan said `applyProfile` needs **no change at all**. It reads `profile.patterns` twice, so it changed by two characters — `patterns()`. Semantically untouched, but "no change" was an overclaim and the accessor is why it is only two characters |
| Extra work | 1 | `shape_checked` was printed and never asserted. The plan named that class of failure and I then left an instance of it in my own new code; it is asserted now, and a mutation that skips a groove's shape check fails |

## Notes for Future Plans

**09-03 adds grooves and nothing else.** A groove is one JSON object under a profile's `grooves`;
the generator carries it to four consumers and six gates check it. No C++ change is needed.

**The refusals it will meet, and every one of them is aimed at copy-and-edit** — which is how a
groove bank actually gets filled. `validate` rejects a ninth groove, a duplicate `id`, a duplicate
`name`, **two grooves with identical patterns in every lane**, an `id` that is not lowercase-kebab,
a quote or backslash in either label, and a missing lane. The identical-patterns rule is the one
worth knowing about in advance: copying a groove and forgetting to edit it is not a typo the other
checks can see, and it produces a cycler offering the same thing twice.

**Two things 09-03 must expect to touch.** The velocity fingerprint WILL move — it walks the whole
bank now, so any added groove changes it, and it is printed every run for exactly that reason.
And each new groove needs a `name`: the four that exist are named `PÉ-DE-SERRA 01`,
`TRADICIONAL 01`, `FORRÓ ELÉTRICO` and `UNIVERSITÁRIO 01`, the first and third taken from
`PLANNING.md:843`'s own label list. **Naming is Esmeraldo's call along with the content.**

**09-05 resolves a groove by id within its profile.** The ids are unique per profile and asserted to
be, because a saved state will hold one. They are not unique across profiles and were never intended
to be.

**`applyGroove` does not exist, deliberately.** 09-05 will want `applyProfile`'s body parameterised
by a groove, and writing it now would have been a function with no caller — which this project has
refused twice before. `applyProfile` loads `defaultGroove()`; the generalisation belongs to the plan
that calls it.
