---
phase: 09-content-convolution
plan: 03
subsystem: content
tags: [profiles, grooves, schema, codegen, cross-check]

requires:
  - phase: 09-content-convolution
    plan: 02
    provides: "the groove bank, and the accessor-not-a-member shape this plan reuses"
provides:
  - "Groove carries bpm, swing and cachaça — a bank can hold different rhythms"
  - "Profile::bpm/swing/cachaca as accessors over defaultGroove(); the members are gone"
  - "The parameter ranges enforced on a groove's feel, read from ParameterIDs.h"
  - "JS_PROFILE_LEVEL — the read-only-app.js duplication named once instead of twice"

affects: [09-04, which fills the banks; 09-06, which loads an arbitrary groove]

tech-stack:
  added: []
  patterns:
    - "A value the plugin will write into a parameter is bounded by that parameter's range"

key-files:
  created: []
  modified: [assets/profiles.json, scripts/build-profiles.py, scripts/verify-profiles.py,
             src/Profiles.h, src/Profiles.cpp, src/PluginProcessor.cpp,
             tests/StateRoundTripTest.cpp, tests/VoiceTest.cpp, tests/UiTest.cpp]
  generated: [src/Profiles.cpp, data.js, "Forró Box (standalone).html"]

key-decisions:
  - "timbre and the bateria mute stay on the profile — regional character, not feel"
  - "Profile's three members DELETED, not kept for compatibility"
  - "The bounds are the PARAMETER ranges, because 09-06 writes these into parameters"

patterns-established:
  - "The read-only-app.js duplication is a pattern with two instances, named once"

duration: ~1h
started: 2026-09-23T13:00:00-03:00
completed: 2026-09-23T14:00:00-03:00
description: "Per-groove feel — a groove carries its own bpm, swing and cachaça, and nothing changed"
type: Summary
about: "Forró Box"
---

# Phase 9 Plan 03: per-groove feel — Summary

**A groove carries its own `bpm`, `swing` and `cachaça`, so a bank can hold genuinely different
rhythms rather than variations at one tempo. `Profile`'s three members were deleted and replaced by
accessors over `defaultGroove()` — the shape `patterns()` took at 09-02 — so the numbers are stored
once. And nothing changed: the fingerprint held and all four profiles report the feel they
reported before.**

## Performance

| Metric | Value |
|--------|-------|
| Tasks | 2 of 2 |
| Checks | 4472 → **4496** |
| Mutations | **8**, each confirmed applied on disk before its result was read |
| `/code-review` | once after Task 2, as the plan required — **5 findings, all 5 fixed** |
| `verify-profiles` | 45 → **57** fields compared |

## Acceptance Criteria Results

| Criterion | Status | Evidence |
|-----------|--------|----------|
| AC-1: nothing changes | **Pass** | Fingerprint `0x313274a06c6ba3e1`, unchanged. `testProfileScalars`' expected table — 132/138/128/124 bpm and their swings and cachaças — still passes untouched, which is what proves the feel survived the move intact |
| AC-2: every groove's feel is checked | **Pass** | 12 new field comparisons. A non-first groove's swing changed in the C++ alone exits 1 naming profile, groove and field |
| AC-3: the numbers are stored once | **Pass** | `Profile::bpm/swing/cachaca` are `constexpr` accessors; the members are **deleted**. The test asserts identity against `defaultGroove()`, not against a literal |
| AC-4: an out-of-range feel is refused | **Pass** | `bpm: 12` exits 1 naming `ids::kMinBpm..kMaxBpm (40..300)` and saying it would be clamped on load |
| AC-5: both prototypes still run | **Pass** | `buildGroove` yields **231 hits**; `PROFILES[id].bpm/.swing/.cachaca` equal `grooves[0]`'s for all four |

## Verification

| | Result |
|---|---|
| GCC 13 / Clang 18 / MSVC 2022 | **4496 / 4496**, real exit 0, 0 warnings from our sources |
| Cross-checks | all six, run together |
| Fingerprint | `0x313274a06c6ba3e1`, unchanged and printed |

## Decisions Made

| Decision | Rationale | Impact |
|----------|-----------|--------|
| A groove is a full feel | A bank named `XOTE LENTO` playing at CAMPINA's 132 bpm is a name that lies, and `PLANNING.md:843`'s eight rhythm labels are unusable without it. The user chose this over patterns-only at 09-03 planning | Phase 9 became seven plans: the schema change is this plan, the content is 09-04 |
| `timbre` and `bateriaMuted` stay on the profile | Regional character, not feel. PETROLINA is LO-FI because the São Francisco forró eletrônico is, and `check_descriptions` ties each profile's prose to both — moving them would make every claim ambiguous about which groove it describes | Stated in `Profile`'s own doc comment, where the next reader will ask |
| The members are DELETED, not kept | A `bpm` beside `grooveBank[0].bpm` is two numbers that can disagree — the duplication 09-02 removed for `patterns` | 19 call sites became `profile.bpm()`. Each was read, not sed'd: 09-02's review found exactly one test left behind by a mechanical accessor edit |
| The bounds are the PARAMETER ranges | 09-06 writes a groove's feel into `ids::bpm`, `ids::swing` and `ids::cachaca`, which clamp silently. A groove outside them would play at a tempo its own source does not state | `MIN_BPM`, `MAX_BPM` and `MAX_PERCENT` are read out of `ParameterIDs.h`, never typed — the rule `read_lane_order` and `read_timbre_index` already follow |

## The read-only constraint, met a second time

`app.js` is design source and cannot be edited. It reads four things off a PROFILE that now live on
a groove:

```
app.js:135       S(p.patterns[key])                  inside buildGroove
app.js:526-528   setBPM(p.bpm), p.swing, p.cachaca   inside loadProfile
```

So the generated `PROFILES` block keeps all four at profile level, rendered from `grooves[0]`, and
emits the full bank beside them. 09-02 met this with `patterns` and explained it inline; meeting it
again with the feel makes it **a pattern rather than an exception**, so the explanation was hoisted
into one named constant, `JS_PROFILE_LEVEL`, instead of being written twice.

## What `/code-review` found

Five findings, and it cleared the question it was pointed at: **drift between a profile and its own
default groove is now structurally impossible** — the members are gone and the accessors return
`defaultGroove()`'s fields, so the two cannot disagree. It confirmed the C++ feel regex can only
bind to a groove's `name` literal, and reproduced every check going red on a real two-groove bank.

What it found instead were three ways the feel goes wrong **loudly but badly**, and two contracts
that were documented and unenforced. **Two of the five were introduced by this plan.**

### `%g` was six significant digits, and the remedy could not clear it

`cpp_float` formatted with `%g`, so a swing of `61.53125` reached the plugin as `61.5312f` while
`data.js` and the standalone page got `61.53125` — **the plugin and the prototypes playing different
numbers.** Worse than the divergence was the dead end: `verify-profiles` went red while
`build-profiles.py --verify` said *"up to date"* for all four targets, so the remedy the gate prints
— *regenerate them* — could not clear it and the build was stuck with no documented way out.

`repr` is the shortest string that round-trips, and it replaces `%g`. `validate` now also refuses a
swing or cachaça that is **not exactly representable as a C++ float**, because `data.js` gets a
double and `Profiles.cpp` gets a `float`: a value the two cannot both hold exactly is the same
divergence by another route. 09-03 multiplied the exposure from two values per profile to two per
groove, which is why it surfaced here.

### `JS_PROFILE_LEVEL` was a constant that looked load-bearing and enforced nothing

I introduced it in this plan to name the read-only-`app.js` duplication once instead of twice — and
nothing read the tuple. Dropping `cachaca` from the profile-level line passed **all six gates**:
`--verify` compares the generator against its own output, and `verify-profiles` stopped reading
`data.js`'s `PROFILES` at 09-01. Meanwhile `app.js:528` would read `p.cachaca` as `undefined` and
the prototype would boot with an undefined `CACHAÇA` knob.

`render_js` now asserts every name in the tuple appears at profile level, checked against the
rendered text rather than against the code that wrote it — so a deleted line fails in the generator
rather than six gates later.

### And three more

- **`bpm` was range-checked but never type-checked.** `"bpm": 96.5` passed `40 <= x <= 300`, wrote
  `96.5` into an `int` field, stopped `Profiles.cpp` compiling, and made *this script* report
  *"could not read bpm/swing/cachaca"* — pointing the reader at the C++ parser instead of at the
  fractional digit in the JSON. The same class `cpp_float`'s own docstring records from the `54.5.0f`
  incident.
- **The new mismatch message could not show the mismatch.** It formatted both sides with `:g` — six
  significant digits each — so `61.53125` against `61.5312` printed as two identical numbers.
  `check_field` two screens down has always used `!r`; this loop was the one place that did not, in
  a script whose whole thesis is naming the digit that diverged.
- **A leftover profile-level `bpm` was silently inert.** Re-adding one left both gates green and
  every generated file unchanged. In the file that is THE source, a key that looks authoritative and
  is read by nothing is exactly what someone edits when they mean to change the tempo.

## A blind exit code in my own verification, found by a stall

The final MSVC run stalled — 30 minutes with the test executable alive at 1712 seconds of CPU, where
it normally finishes in a few. Investigating it found the stall was environmental (the same binary,
re-run directly, passed 4496/4496 in well under the timeout, and a clean re-run of the whole script
finished quickly) — but it also found something real about how this session was verifying MSVC.

Every MSVC run was invoked as:

```
./scripts/build-windows.sh 2>&1 | tail -2; echo MSVC_DONE
```

The exit code that comes back from that is **`echo`'s**. A genuine MSVC failure would have been
reported as exit 0. When I killed the stalled run, the script printed
`FATAL: 1 of 1 test executables failed under MSVC` and the harness still reported `[exited with
code 0]` — the two disagreeing is what exposed it.

The conclusions in this plan and the previous two are unaffected, because the signal actually read
each time was `4496 / 4496 checks passed — OK` grepped out of the build log, not the exit status.
But the exit-code channel was blind, which is the failure this project states as *"a build failure
is not a detection, and the real exit code is read rather than a pipeline's"* — applied to the
cross-checks since Phase 4 and not, it turns out, to my own invocation of the Windows build.

Re-run without the pipeline: **real exit 0, 4496 / 4496**.

## Deviations from Plan

| Type | Count | Impact |
|------|-------|--------|
| None | 0 | Both tasks landed as written |
| Method corrected | 1 | The MSVC exit code was masked by a pipeline — see above. Not a deviation from the plan, but a correction to how its verification row was gathered |

## Notes for Future Plans

**09-04 is much smaller than its ROADMAP line suggests, and the reason is worth reading before
planning it.** `tests/VoiceTest.cpp:4893` already has **`renderAuditionFiles`** — it renders every
profile to a `.wav` through the shipping chain (`useShippedChain`, the real `CACHAÇA`, the limiter),
four bars at the profile's tempo plus a tail, printing peak and flagging clipping, and it is
invoked from `TestMain.cpp:88` behind a command-line flag. Phase 3 built it so a person could judge
whether the grooves match the prototype.

So 09-04's renderer task is **generalise it from per-profile to per-groove and add the `.mid`
side**, not build one. Two things it must confront:

- **`applyGroove` finally gets its caller.** The renderer needs to apply an arbitrary groove, which
  is the function 09-02 and 09-03 both declined to write for want of one.
- **`renderAuditionFiles`' own docstring says "Not a test — nothing here asserts."** That is exactly
  what 04-01's law forbids of a checkpoint artefact, and `ui-renders` is the counter-example in this
  same repository: those renders are asserted for their far corner and their knob ink. Esmeraldo's
  audition files deserve the same, and 09-04 should say what a rendered groove must be true of
  rather than only writing it.

**A groove's feel is bounded by the parameter ranges, not by taste.** 09-04 can write any bpm from
40 to 300 and any swing or cachaça from 0 to 100; anything else is refused at build time with the
range named.
