---
phase: 09-content-convolution
plan: 01
subsystem: content
tags: [profiles, codegen, cross-check, json, prototype]

requires:
  - phase: 02-sequencer-clock
    provides: "the four groove tables, and the cross-check that has compared them to data.js since 02-01"
provides:
  - "assets/profiles.json — the grooves' one home"
  - "scripts/build-profiles.py — three consumers generated from it, with --verify"
  - "A cross-check that reads JSON instead of regex-parsing JavaScript"
  - "check_descriptions — each profile's prose, against its own data"
  - "A velocity fingerprint: 512 velocities as one pinned number"

affects: [09-02, which adds grooves; any plan that adds a profile]

tech-stack:
  added: []
  patterns:
    - "A generated file is checked, or it rots where nobody looks"
    - "A projection applied to BOTH sides of a comparison can never be detected"

key-files:
  created: [assets/profiles.json, scripts/build-profiles.py]
  modified: [scripts/verify-profiles.py, scripts/verify-midi.py, src/Profiles.cpp,
             CMakeLists.txt, tests/StateRoundTripTest.cpp]
  generated: [data.js, "Forró Box (standalone).html"]

key-decisions:
  - "The C++ is GENERATED, so the banner that said so since Phase 2 is finally true"
  - "data.js becomes partly generated — a status change, recorded rather than done quietly"
  - "The standalone page is spliced and checked, not left to the bundler"
  - "A non-bateria mute is REFUSED, not dropped"

patterns-established:
  - "One JSON, three consumers, all three enrolled"

duration: ~3h
started: 2026-09-23T01:00:00-03:00
completed: 2026-09-23T04:00:00-03:00
description: "profiles.json — the groove tables leave data.js, and the cross-check stops parsing JavaScript"
type: Summary
about: "Forró Box"
---

# Phase 9 Plan 01: `profiles.json` — Summary

**The four regional grooves live in `assets/profiles.json`. The C++ table, `data.js`'s `PROFILES`
block and `PROFILE_ORDER`, and the standalone page's inlined copy are all generated from it and all
three are checked on every build. Not one digit of the music changed, and that is proved rather
than claimed.**

## Performance

| Metric | Value |
|--------|-------|
| Tasks | 3 of 3 + 1 checkpoint, approved |
| Checks | 4410 → **4412** |
| Mutations | 18, each confirmed applied on disk before its result was read |
| `/code-review` | once after Task 2, as the plan required — **6 findings, 2 HIGH, all 6 fixed** |
| `/simplify` | 4 angles — **13 findings, 9 applied, 4 deferred with a named owner** |
| Gates | 5 → **6**; `verify-profiles` 42 → **45** fields, plus 10 description claims and 36 shape checks |
| Lines of JavaScript parsing in `verify-profiles.py` | ~80 → **0** |

## Acceptance Criteria Results

| Criterion | Status | Evidence |
|-----------|--------|----------|
| AC-1: the music does not change | **Pass** | The generator reproduces the committed `Profiles.cpp` table, `data.js` and the standalone page **byte for byte** — `git diff` on all three is empty but for two deliberate comments, the `Profiles.cpp` banner and the generated-region notice `/simplify` added to the JS. Not one velocity digit differs. A fingerprint over all 512 velocities is pinned |
| AC-2: one source, every consumer proved against it | **Pass** | Three consumers, all enrolled. A digit changed in any one of them without the JSON fails the build naming the file, the line and the lane |
| AC-3: the cross-check stops parsing JavaScript | **Pass, with a correction** | The groove parser is gone. `match_braces` STAYS — see below |
| AC-4: each description agrees with its own data | **Pass** | 10 claims across 4 profiles; exact claims compared exactly, ordinal ones as rank |
| AC-5: the prototype still runs, from the same source | **Pass** | Both pages' scripts parse under Node; `data.js`'s own `buildGroove` yields **231 hits** across the four grooves, and `profiles.json` has 231 |

## Verification

| | Result |
|---|---|
| GCC 13 / Clang 18 / MSVC 2022 | **4412 / 4412**, real exit 0, 0 warnings from our sources |
| Cross-checks | all five, plus the new generator gate |
| Install | hashes match, `/mnt/d/VST3`, run with `--install` |
| Checkpoint | approved — all four grooves judged right for the regions they name |

## Decisions Made

| Decision | Rationale | Impact |
|----------|-----------|--------|
| The C++ table is generated | `Profiles.cpp:8` has said "GENERATED FROM data.js — do not hand-edit a digit here" since Phase 2, and it was an instruction to humans: the table was transcribed and compared back by a script that regex-parsed JavaScript | The banner is now a fact |
| `data.js` becomes partly generated | The user's decision moves the grooves out of it, which necessarily edits a file read-only for nine phases. `PROFILES` and `PROFILE_ORDER` are generated; `INSTRUMENTS`, `CHANNEL_DEFAULTS`, `TIMBRES`, `PRESETS` and `buildGroove` stay hand-written and authoritative | Stated here rather than done quietly |
| The standalone page is spliced directly | It inlines `data.js` verbatim, so the same rendering applies and no bundler run is needed. It is the one consumer a stale copy reaches a user through — committed, and `README-prototype.md` tells people to open it | A late correction to the plan, which had claimed checking it meant running the bundler |
| A non-`bateria` mute is REFUSED | `Profile` carries one `bool bateriaMuted`. Supporting more is a change to `Profile`, to `applyProfile` and to what a profile MEANS — not a generator's to infer from a JSON key | The generator fails with what it would take to support it |
| The fingerprint is a digest, not a table | `verify-profiles.py`'s own docstring argues that embedding the patterns by hand duplicates the typo risk. One number carries the same signal | Printed every run, so re-pinning a deliberate change is reading one line |
| The generator IMPORTS the checker | Four things lived in both — the timbre index, the mute whitelist, `validate` and the JSON load — and the writer's copy was the dangerous one: a stale index there is emitted into the plugin and then `--verify` compares the output against that same stale table and passes | One home each, in the gate that owns validity. Same importlib trick `verify-midi.py` already used on the same file |

## Deviations from Plan

| Type | Count | Impact |
|------|-------|--------|
| AC wording wrong | 1 | AC-3 said the brace matcher goes. It could not |
| Plan claim wrong | 1 | The plan said checking the standalone page would mean running the bundler. It does not |
| Files outside `files_modified` | 1 | `scripts/verify-midi.py` |

### AC-3 was written too strongly

It said *"the hand-written brace matcher and the `PROFILES` regexes are gone"*. The regexes are
gone. `match_braces` could not be: four other readers use it, and **all four parse C++** — which is
the thing this script checks, not the thing it checks against. The plan's own prose said as much
two paragraphs later (*"what stays is everything that reads the C++"*), so the AC contradicted its
own task description.

### `verify-midi.py` imports the function that was renamed

It reuses `verify-profiles.py`'s reader rather than parsing the grooves a second time — deliberately,
and its docstring says why. `read_data_js` became `read_profiles_json` and the call broke.

**Only running all six gates together found it.** Each was green on its own, because the five I had
been running individually did not include the one that imports another. `assets/profiles.json` is
now a declared input to `verify-midi` too — the **fourth** instance of the enrolment failure the
CMake comments already record three times.

## What `/code-review` found

Six findings, two of them HIGH, and both HIGH ones were exactly the question the review was pointed
at: *can a wrong groove reach the plugin with every gate green?* Both were reproduced before being
fixed.

### A non-`bateria` mute reached the prototypes and not the plugin

`render_cpp` projected `muted` to `"bateria" in muted` — and `verify-profiles.py` computed **the
same projection** for its expected value. The lossy step sat on both sides of the comparison, so it
could never be detected. `render_js` meanwhile emits the whole object, and `app.js:533` honours a
mute on any of the five instruments.

Reproduced: `muted: ["pandeiro"]` silenced the lane in both prototypes, played it in the plugin, and
exited 0 everywhere. A misspelt `"bateira"` was equally invisible — it muted nothing, anywhere,
green.

**A projection applied to both sides of a comparison is not a check.** That is the general form, and
it is worth carrying: the same shape hid 08-05's double-driven animations, where both drivers called
one `advance`.

### `PROFILE_ORDER` was left hand-written, and 09-02 walks straight into it

The deleted `read_data_js` asserted that `PROFILES` and `PROFILE_ORDER` agreed. Reading JSON dropped
the assertion along with the parser, and nothing generated `PROFILE_ORDER`.

`app.js:111` and `:279` iterate that list, not `PROFILES`. So a fifth profile would have appeared in
both prototypes' DATA and in neither prototype's UI, with every gate exiting 0. A rename is worse:
`PROFILES` takes the new key, `PROFILE_ORDER` keeps the old, and `loadProfile`'s `PROFILES[id]`
comes back `undefined` — the page throws on boot. **09-02 is the plan that adds profiles.**

### And four more

- **Fractional `swing` generated `54.5.0f`** — a syntax error written into a source file, then an
  uncaught `ValueError` out of the gate that should have reported it. `Profiles.h` documents swing
  as a float, so `54.5` is legitimate data. Now formatted rather than concatenated, and it compiles.
- **The new counts were printed, never asserted.** `check_descriptions` matches Portuguese
  substrings; reword a description and the count silently drops while the script still says OK.
  That is the *"reports OK while a class of check is broken"* failure `check_field`'s own docstring
  was written against, reproduced one function over. 09-02 rewrites these descriptions.
- **`read_profiles_cpp` anchored on the first MENTION of `kProfiles`**, not its declaration — third
  instance of a class this file had already fixed twice, and 09-01 had just put a twelve-line banner
  directly above the table.
- **The stale-file report printed no line number** when the difference was past end of file.

## What `/simplify` found

Thirteen findings across four angles. **Efficiency returned none** — it measured the new gate at
0 ms on a real build and 41 ms serial, second-cheapest of the six, because ninja runs all six
concurrently and `verify-geometry`'s 348 ms sets the wall clock regardless.

Nine were applied. The three that mattered:

**The writer held the second copy.** `TIMBRE_INDEX`, `CPP_MUTABLE`, `validate` and the JSON load
were written in `verify-profiles.py` and again in `build-profiles.py`. The checker's timbre index is
*derived*, from `MixBus.h`'s `timbreSpecs`; the generator's was the hand-written
`{"hifi": 0, "lofi": 1, "ciclo": 2}` — and the generator is what writes `Profiles.cpp`. A reorder of
`timbreSpecs` would have had it emit a stale index into the plugin, with `--verify` comparing the
output against the same stale table and passing. `build-profiles.py` now imports the checker through
the importlib path `verify-midi.py` already used on that file. **Mutated: swapping `hifi` and `lofi`
in `timbreSpecs` is caught by both gates, real exit 1 each** — the shared derivation is safe because
`MixBus.h` is independent of both the JSON and the C++ table, which is exactly what the `/code-review`
HIGH was not.

**Three false or hollow comments, all mine.** `Profiles.cpp`'s banner — the one comment this plan
existed to make true — said `build_standalone.py` carries the standalone copy, when
`build-profiles.py` splices it directly. `validate`'s docstring priced a non-`bateria` mute at
"a change to `applyProfile` and what a profile MEANS"; `applyProfile` does not touch mutes at all
(`PluginProcessor.cpp:250-258` does), and the true price is one field and about six lines. And
`verify-profiles.py`'s docstring defended the gate's existence on its *weakest* job, leaving a later
reader free to call it redundant — it now names the three things the generator does not generate.

**`data.js` did not say which of its blocks had stopped being hand-written.** The notice is spliced
*inside* the generated region rather than above it: above the marker it would be ordinary text that
anyone could delete with every gate green. **Mutated: deleting one line of it fails `--verify`,
real exit 1.**

Also applied: `kPatternLength` read from `Profiles.h` instead of the digit `16`; a dead "no pattern"
branch that `validate` had already made unreachable; an outer claim floor that could only ever fire
behind a better per-profile message; and the fingerprint's index-mixing.

### The fingerprint absorbed its own loop structure

It absorbed `profile`, `lane` and `step` alongside each velocity. FNV-1a is a chain and already
distinguishes the same velocity at two positions, so the indices added no signal — but they made the
digest depend on how the traversal was written, under a comment promising it moves when a groove
does. Now it absorbs velocities only, and was re-pinned to `0x313274a06c6ba3e1` in the same commit.

**Re-mutated after the change**, because a reshaped check is an unproven one: a single `7474`→`7373`
edit *in the JSON*, regenerated so all six gates agree and only the fingerprint can object — **real
exit 1, 4411/4412**.

That mutation took two attempts. The first edited `Profiles.cpp` directly, which failed the gate,
which stopped the build, and the suite I then ran was the **previous binary** reporting a green
digest. The project's own law — *a failed build is not a detection* — caught in the act. The
detection is only real because the gates were made to agree first.

## Deferred, with owners

| Finding | Why not now | Owner |
|---|---|---|
| `ids::profileInfos` is not generated | The identity strings and all twelve description lines are still hand-transcribed and compared back by a regex parse of our own C++ — the exact arrangement this plan existed to end, left standing for the one field 09-02 rewrites. Generating it is a fourth render target, a new splice and its own byte-identity proof | **09-02**, as its opening task. Named in `Profiles.cpp`'s banner so it cannot be missed |
| `check_descriptions` parses prose it could derive | Composes with the above: once `desc` is generated, the claims can be data rather than Portuguese substring matches | 09-02, after the above |
| `build_standalone.py` has no `--verify` | It rewrites the whole page unconditionally; `build-profiles.py` splices two blocks into it. The overlap is real but restructuring a root script sits well outside this diff. `--verify` now prints which script to run when the standalone is the stale one | Whichever plan next touches the bundler |
| Deleting the fingerprint | Argued as redundant against `testPatternDecoder` plus the text-level gates, and as a liability because 09-02 must re-pin it. Skipped: it is the only check that reads the **built binary's** table end to end, and it was just proved to detect. Removing coverage on a judgment call is the user's to make, not mine | Raised, not taken |

## Notes for Future Plans

**09-02 edits `assets/profiles.json` and nothing else.** Adding a groove is one JSON edit; the
generator carries it to the plugin and both prototypes, and six gates check the result. That is what
this plan bought.

**Two things 09-02 must expect to touch.** The velocity fingerprint is pinned and will move — it is
printed on every run for exactly that reason, and a deliberate change is re-pinned in the same
commit that makes it. And `check_descriptions` parses the prose it is given; a reworded description
that stops naming its timbre fails deliberately, with a message saying to reword it or teach the
checker the new claim.

**The mute model is narrower than the prototype's.** `Profile` carries one `bool bateriaMuted` where
`app.js` honours a mute on any of the five instruments. 09-01 refuses the difference rather than
papering over it; widening it is a real change to `Profile` and `applyProfile`, and belongs to
whichever plan first wants a profile that silences something else.
