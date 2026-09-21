---
phase: 06-side-panel
plan: 06
subsystem: testing
tags: [juce, test-rig, msvc, utf-8, enrolment-gate]

requires:
  - phase: 06-05
    provides: HitZone and SelectableTile — the shapes the rig reaches through; clickInside/hoverInside
provides:
  - HeaderBar::getStyleControl — one accessor for five scans and two disagreeing predicates
  - collectChildren returning root-space bounds, with a self-test that can fail
  - ChassisRig — the MSVC destruction-order invariant made structural
  - a charset gate over tests/ message literals, and a tokenizer self-test
affects: [07-midi-out, 08-polish]

tech-stack:
  added: []
  patterns:
    - "An invariant that fails on ONE compiler goes in the type, not a comment"
    - "A non-ASCII test message must be a DIRECT argument; the gate says so"
    - "A scrape that feeds a build gate needs CONFIGURE_DEPENDS and a floor"

key-files:
  created: []
  modified: [src/HeaderBar.h, tests/UiTest.cpp, tests/TestHarness.h, tests/VoiceTest.cpp, scripts/verify-charset.py, CMakeLists.txt]

key-decisions:
  - "The processor lives in a BASE, so no member reorder can outlive it"
  - "withState, pollAll, renderChild and render() all deleted — verbs with no caller"
  - "The overloads are not the fix; the gate is. 20 mojibake lines survived them"
  - "The footer's OUTPUT scan stays unconverted — it needs a FooterBar accessor no plan named"

patterns-established:
  - "When a type is added to prevent a bug, remove the implicit conversion that lets old sites compile"
  - "Prove a gate by mutating it, not by reading its exit code once"

duration: ~190min
started: 2026-09-20T07:50:00-03:00
completed: 2026-09-20T11:00:00-03:00
description: "The test seams and the ChassisRig — and the reviews found 20 mojibake lines in a passing run, a verb with no caller, and the same bounds bug 600 lines from its own fix"
type: Summary
about: "Forró Box"
---

# Phase 6 Plan 6: The test seams and the ChassisRig — Summary

**`tests/UiTest.cpp` stops re-establishing four things by hand — and the review passes found that
this plan's own work had a verb with no caller, a claim proved on one example and generalised to
228, and a live instance of the very bug it added a type to prevent.**

## Performance

| Metric | Value |
|--------|-------|
| Duration | ~190 min |
| Tasks | 3 of 3 |
| Qualify | 2 PASS / 1 GAP (AC-4) |
| Checks | 3818 → **3822** |
| Compilers | GCC 13, Clang 18, MSVC 2022 — 0 warnings |
| Cross-checks | 4 green; charset gate now covers `tests/` too |
| Mojibake in a passing run | 20 → **0** |

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: STYLE asked for, not hunted | **Pass** | 5 → 1; both disagreeing predicates gone; mutation fails cleanly |
| AC-2: `collectChildren` carries a space | **Pass** | Only after a self-test — see Deviations |
| AC-3: one rig, order load-bearing | **Pass** | 30 sites; **proved on MSVC**, then made structural |
| AC-4: state mutation is one verb | **NOT MET** | `withState` was built, had one caller, and was **deleted** |
| AC-5: a failing check prints its characters | **Pass** | Only after the gate — the overloads alone left 20 lines wrong |

## Accomplishments

- **The MSVC invariant is structural.** It was documented at one of ~38 sites; it became the rig's
  member order, was **proved by mutation** (Linux exit 0, MSVC `FATAL: 1 of 1 test executables
  failed`), and then moved into a BASE class so no future member reorder can break it.
- **`getStyleControl()`** — five scans with two predicates that did not agree became one accessor.
- **`Found<T>`** carries root-space bounds, its implicit `operator T*` was removed so un-migrated
  sites become compile errors rather than silent survivors, and it has a self-test that fails.
- **The charset gate covers `tests/` message literals**, with a tokenizer self-test. Both proved
  to fail by mutation.

## Files Modified

| File | Change |
|------|--------|
| `src/HeaderBar.h` | `getStyleControl()` |
| `tests/UiTest.cpp` | `Found<T>`, `ChassisRig` (+30 sites), `componentAt`, the collector self-test |
| `tests/TestHarness.h` | `const char*` overloads doing `fromUTF8` |
| `tests/VoiceTest.cpp` | 8 message literals the overloads could not reach |
| `scripts/verify-charset.py` | Digit-separator lookbehind; `tests/` message rule; tokenizer self-test |
| `CMakeLists.txt` | Gate dependencies scraped from the script, with `CONFIGURE_DEPENDS` and a floor |

## Decisions Made

| Decision | Rationale | Impact |
|----------|-----------|--------|
| The processor lives in a BASE | A base is destroyed after every member, whatever order they end up in. Member order under a comment compiles, passes on Linux, and fails only on MSVC — the longest feedback loop here | The invariant is a property of the type, and shareable |
| `withState`, `pollAll`, `renderChild`, `render()` all deleted | Callers: 1, 0, 0, 0. The struct refuses `renderChild` on 02-04's "a guarantee with no caller is not a guarantee" — shipping three more would state and break that rule on one screen | AC-4 not met, deliberately |
| `operator T*` removed from `Found<T>` | It was what let four un-migrated sites keep compiling, including one with the space mismatch the type exists to prevent | Four explicit `.control` buys the compiler finding the rest |
| The gate, not more overloads | `section ("… — " + mode)` constructs the String **before** `section` is called, so an overload can never see it | 20 → 0 mojibake, and a rule that fails |
| The footer's OUTPUT scan stays | It needs a `FooterBar` accessor no plan has named | Recorded, not half-converted |

## Deviations from Plan

### Summary

| Type | Count | Impact |
|------|-------|--------|
| Auto-fixed | 15 | 9 from `/code-review`, 6+ from `/simplify` |
| Built then deleted | 4 | Verbs with no caller |
| AC not met | 1 | AC-4 |
| Premise overstated | 1 | Task 3's `contrastMass` claim |

### The claim I proved on one example and generalised

AC-5 was reported PASS after I mutated a check, read the printed message, and saw an em dash render.
It did — that one. `/simplify` then **measured the whole output: 20 section headers still printed
mojibake in a passing run.** `juce::String::operator+= (const char*)` uses `CharPointer_UTF8`, but
`juce::String (const char*)` uses `CharPointer_ASCII` — so `section ("… — " + mode)` builds the
String through Latin-1 before `section` is ever called, where no overload can reach it. 19 sites
across two test files. The overloads stay (they cover ~209), but the rule now lives in
`verify-charset.py`, which immediately caught **3 more sites my own bulk fix had missed**.

### AC-2 was briefly unfalsifiable

Every converted site collects controls that are DIRECT children, where parent-relative and
root-space are the same rectangle — the mutation left the suite green. A self-test on a
`ProfileButton` (nested inside `SidePanel` at x=920) is the subject that can tell them apart.

### The same bug, 600 lines from its own fix

`/code-review` found `UiTest.cpp:7031` comparing a footer-space rectangle against parent-relative
bounds — the exact shape `Found::bounds` was added to prevent. I fixed it with a six-line comment.
`/simplify` then found **the identical bug in the same footer** at the OUTPUT toggle, which my fix
had walked past. Removing `operator T*` is what makes that class stop being possible.

### Built, then deleted

`withState` (1 caller), `pollAll` (0), `renderChild` (0), `render()` (0). The last two were caught
by `/simplify` pointing out that the same struct *refuses* a verb on the no-caller rule twenty lines
above. `withState`'s single conversion was also a regression: a one-view refresh became four, five
times per loop, to read one `getSelectedIndex()`.

### Task 3's premise was overstated

PROJECT.md says `contrastMass` appears 50 times "with the reference pixel picked by hand each time."
Measured: **4–6**; the rest pass a named colour or token. The measurement verb was not built. Third
overstated premise this phase.

### Gate wiring that claimed more than it did

The `verify-geometry` scrape had no `CONFIGURE_DEPENDS`, so adding a header to the enrolled list
would not re-configure and the new header would never become a dependency — the same enrolment
failure 06-05 fixed, one level up, twenty lines from a block that states the rule. Fixed, plus a
floor so a scrape that matches nothing fails loudly.

## Issues Encountered

| Issue | Resolution |
|-------|------------|
| Mutating `getStyleControl` to null **segfaulted** the suite | One converted site checked then dereferenced without returning. Guarded — a crashing test loses every check after it |
| Bulk-wrapping message literals broke an adjacent-literal run | Detected by the build; the run's continuation was unwrapped |
| 8 unused `processor` aliases after conversion (Clang only) | Removed; 12 more single-use aliases inlined |

## Deferred (logged to PROJECT.md)

- The footer's OUTPUT scan needs a `FooterBar` accessor.
- 4 hand-built sites must mutate between construction and `attachParameters`; the rig has no
  pre-attach hook. Same shape three times.
- 18 hand-built sites remain and are genuinely different composites (editor rigs, processor-only).

## Next Phase Readiness

**Ready:** Phase 6 is complete. Phase 7 (MIDI out) inherits `ChassisRig`, `clickInside`/`componentAt`
and a charset gate that now covers both trees.

**Concerns:** AC-4 is unmet by decision; the rig has no state verb. Whoever converts those sites
should add one then.

**Blockers:** None.

---
*Built with PAUL Framework v1.4*
*Phase: 06-side-panel, Plan: 06*
*Completed: 2026-09-20*
