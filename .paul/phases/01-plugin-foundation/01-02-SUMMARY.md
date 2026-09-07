---
phase: 01-plugin-foundation
plan: 02
subsystem: infra
tags: [juce, juce8, apvts, valuetree, parameters, state-persistence, base64, testing]

requires:
  - phase: 01-plugin-foundation (01-01)
    provides: CMake project, plugin target, processor skeleton with the audio-thread contract
provides:
  - 45 automatable APVTS parameters in a GLOBAL group plus 5 per-instrument groups
  - Single source of truth for channel-parallel data (ids::channelInfos)
  - forrobox::State — 8 grid lanes, profile, dirty flag, bounded preset/pattern scalars
  - Lossless getStateInformation / setStateInformation round-trip
  - LockedState RAII accessor: no unguarded path to the non-automatable state
  - ForroBoxTests target — 161 checks, reusable for Phase 2's clock and Phase 3's voices
affects: [01-03-windows-build, 02-sequencer-clock, 03-voices-mix-bus, 04-ui-shell, 05-sequencer-grid, 06-side-panel]

tech-stack:
  added: []
  patterns:
    - "Every persisted ID declared once in ParameterIDs.h; nothing hardcodes an ID string"
    - "Parallel per-channel data lives in one array of structs, not several arrays plus asserts"
    - "Bounds live with the data via clamping setters; serialisation clamps are defence in depth"
    - "Non-automatable state reachable only through an RAII lock handle"
    - "Untrusted project data is size-checked before it can dictate an allocation"

key-files:
  created:
    - src/ParameterIDs.h
    - src/ForroBoxState.h
    - src/ForroBoxState.cpp
    - tests/StateRoundTripTest.cpp
  modified:
    - src/PluginProcessor.h
    - src/PluginProcessor.cpp
    - CMakeLists.txt

key-decisions:
  - "sync is a parameter, though PLANNING.md's mapping list omits it"
  - "Grid lanes stored at a fixed 32 slots; the steps parameter selects the active window"
  - "Manual base64 kept over var(MemoryBlock) — JUCE's native path trusts a length prefix"
  - "Test target is a plain add_executable, not juce_add_console_app"

patterns-established:
  - "Every acceptance criterion must be provable by a command that can be re-run"
  - "Negative-control the test suite: break something deliberately, confirm it fails"

duration: ~95min
started: 2026-09-07T00:50:00Z
completed: 2026-09-07T02:25:00Z
description: "45 grouped APVTS parameters plus a ValueTree grid/profile state node, round-tripping losslessly and proven by a 161-check suite with no unguarded path to the state."
type: Summary
about: "Forró Box"
---

# Phase 1 Plan 02: Parameters and State Persistence Summary

**45 grouped APVTS parameters plus a ValueTree grid/profile state node, round-tripping losslessly
and proven by a 161-check suite with no unguarded path to the state.**

## Performance

| Metric | Value |
|--------|-------|
| Duration | ~95 min |
| Tasks | 3 completed |
| Files | 4 created, 3 modified — 1,316 lines total |
| Clean rebuild | 47 s, zero warnings (GCC and Clang) |
| Tests | 161 checks, 7 case groups, all passing |

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: Parameter surface exactly as specified | Pass | 45 parameters, no duplicate IDs, every default matched against PLANNING.md's tables, versionHint 1 throughout |
| AC-2: Parameters grouped for the host | Pass | 6 groups sized 10/7/7/7/7/7; ASCII group IDs with accented display names |
| AC-3: Values display as the prototype does | Pass | `C`/`L18`/`R22`, `+5`/`0`/`-5`, `12%`, `132`; invertibility swept across all four full ranges |
| AC-4: Non-parameter state in a ValueTree node | Pass | 8 lanes × 32 as base64, plus profile, dirty, preset index, 5 pattern slots. No grid step is a parameter |
| AC-5: State round-trips losslessly | Pass | All 45 parameters off their defaults, all 8×32 lane values, every scalar. Bools now flip rather than being set to a value one already held |
| AC-6: Malformed state survived, not crashed on | Pass | Empty, random bytes, missing node, truncated base64, out-of-range velocities, non-string lane, out-of-range scalars |
| AC-7: Test target builds and passes; plugin still clean | Pass | 161/161; both plugin targets `ldd`-clean under two compilers |

## Accomplishments

- The full parameter surface every later phase attaches to, with IDs and group IDs fixed so a
  host's saved state stays valid
- A state layer hardened against arbitrary project files — every malformed-input case degrades to
  a valid default instead of crashing
- A reusable test harness, and the habit of negative-controlling it: two deliberate breakages were
  introduced and confirmed to fail before the suite was trusted
- No unguarded path to the non-automatable state, closing a race a partial fix had left open

## Task Commits

Not applicable — the project is intentionally not a git repository. **This still blocks the
phase-transition workflow's mandated commit; unresolved before Phase 1 closes.**

## Files Created/Modified

| File | Change | Purpose |
|------|--------|---------|
| `src/ParameterIDs.h` | Created | Every persisted ID once; `channelInfos` as the single source of channel-parallel truth |
| `src/ForroBoxState.h` | Created | Grid lanes, profile, dirty flag; sizes derived from the ID lists; bounded scalars behind clamping setters |
| `src/ForroBoxState.cpp` | Created | Base64 lane serialisation with defensive decode |
| `tests/StateRoundTripTest.cpp` | Created | 7 case groups, 161 checks |
| `src/PluginProcessor.h` | Modified | APVTS member, `LockedState` RAII accessor, `stateLock` |
| `src/PluginProcessor.cpp` | Modified | `createParameterLayout()`, real save/load |
| `CMakeLists.txt` | Modified | New source, `ForroBoxTests` target behind `FORROBOX_TESTS` |

## Decisions Made

| Decision | Rationale | Impact |
|----------|-----------|--------|
| `sync` is an automatable parameter | PLANNING.md's parameter-mapping list omits it while its state table includes it; it is a user-facing toggle that must persist | 10 globals, not 9 |
| Lanes fixed at 32 slots | A variable-length lane makes 32→16→32 lossy in persistence, which is not the same as the truncation the UI performs on a step-count change | `steps` selects the active window |
| Manual base64 kept | `MemoryBlock::fromBase64Encoding` parses an attacker-controlled length prefix and calls `setSize()` before validating any payload backs it — `"999999999.AAAA"` in a project file means a ~1 GB allocation. `juce::Base64` has no such header | The hand-written path is the safer one, and the format stays standard base64 |
| One `ChannelInfo` array replaces three parallel arrays | Four `static_assert`s were guarding one "these arrays must agree" invariant | Divergence is now impossible rather than detected |
| `LockedState` RAII handle replaces the mutable reference | A lock covering only the state methods advertised protection the primary access path did not have | Every touch is serialised |

## Deviations from Plan

### Summary

| Type | Count | Impact |
|------|-------|--------|
| Plan spec errors corrected | 2 | Verify path and source-registration order |
| Auto-fixed during qualify | 5 | 3 were bugs in my own test suite |
| Review fixes applied | 8 of 9 from `/code-review`, 6 of 7 from `/simplify` | 3 medium-severity, 1 user-visible |
| Deferred | 1 | `juce::UnitTest` migration |

**Total impact:** No scope creep. The largest change — restructuring channel data and state access —
came from review findings that my own fixes had been symptom patches.

### Auto-fixed during qualify

1. **Greedy hex escape.** `"CACHA\xc3\x87A"` parsed `\x87A` as one escape (`A` is a hex digit),
   out of range. Split the literal.
2. **`JUCE_STANDALONE_APPLICATION` redefinition.** `juce_add_console_app` forces `=1` while the
   plugin's shared code exports `=0`, warning on every TU. Plain `add_executable` instead.
3. **`-Wfloat-equal`** in the test comparison helper. `if constexpr` tolerance branch.
4. **Invertibility sweeps failed for every value** — my bug: `getValueForText` returns a
   *normalised* value and I compared it against plain values.
5. **`limiter_on` was never moved off its default** — the helper returned `1.0f` for every bool and
   that parameter defaults to true, so AC-5's "every parameter" was quietly false. Bools now flip.

### `/code-review` findings (9)

Three medium. Full table in STATE.md. The user-visible one: pan's text→value dropped the sign, so a
host's typed `-20` returned **R20** — the opposite channel. Two others were array-size decouplings
that would have turned a natural edit (a 9th kit piece, a 6th channel) into an out-of-bounds write
or a crash on construction. Finding 5 (unsynchronised state) was fixed only partially at first and
completed during UNIFY.

### `/simplify` findings (7 across 4 agents)

- **Altitude:** three of the code-review fixes were symptom patches. Four `static_assert`s guarding
  one invariant became one array of structs; strip-after-`replaceState` became a pre-stripped copy so
  the live tree is never invalid; edge clamping became clamping setters so an out-of-range value
  cannot exist in memory. The `stateLock` was judged *actively misleading* rather than incomplete —
  fixed with an RAII accessor.
- **Reuse:** `plainIntAttributes()` was byte-identical to `AudioParameterInt`'s built-in default —
  deleted, along with two redundant parsers. The base64 implementation was verified as deliberate
  and safer than JUCE's native path.
- **Simplification:** the `reload` scaffolding was duplicated three times and had already drifted
  (some copies guarded `xml != nullptr`, others did not) — hoisted to file scope. Two near-identical
  typed-entry lambdas parameterised.
- **Efficiency:** audio path clean. A double ValueTree lookup and a missing hot-path warning fixed.

### Deferred

- **`juce::UnitTest` migration.** The hand-rolled `check`/`checkEqual`/`section` harness duplicates
  `juce::UnitTest`/`UnitTestRunner`, including `expectWithinAbsoluteError` matching the `if constexpr`
  branch exactly, at no extra link cost. Skipped deliberately: it is a mechanical rewrite of 620
  lines with a real risk of silently dropping an assertion, for no behavioural gain. Phase 2 adds
  clock tests — that is the natural moment to adopt it.
- **`testDeclaredIdsMatchLayout` walks its loops twice.** Flagged as a judgment call by the agent
  itself; the split reads more clearly than a single fused pass. Skipped.

## Issues Encountered

| Issue | Resolution |
|-------|------------|
| `JucePluginDefines.h` is generated at build time | AC-2 checked after the build, via the target's compile flags |
| A clang check reported "CLEAN" while aborting on a missing generated `JuceHeader.h` | Caught via the non-zero exit; redone with the header on the include path. Every clang check since asserts the header exists first |
| `parameterTree` shadowed `AudioProcessor`'s own private member | Renamed to `strippedTree` |
| Two AC-4 "violations" were false positives | My own comment text, and `lock` matching inside `processB`**lock** |

## Next Phase Readiness

**Ready:**
- Phase 2's clock has `bpm`, `swing`, `steps`, `sync` parameters and a persisted grid to read
- Phase 3's voices have all 35 per-channel parameters
- Phase 4 can attach UI controls to fixed, centralised IDs
- A test target exists for both to build on

**Concerns:**
- No git repository, so the phase-transition commit cannot run
- The plugin display-name defect is still open and should be settled before 01-03
- Sample library received 2026-09-07 conflicts with the synthesised-voices decision; needs settling
  before Phase 3 is planned. It does not affect this plan's parameter surface either way

**Blockers:** None for 01-03.

## Skill Audit

| Expected | Invoked | Notes |
|----------|---------|-------|
| `/code-review` | ✓ | After Task 2; 9 findings, 8 fixed, 1 completed during UNIFY |
| `/simplify` | ✓ | During UNIFY; 4 agents, 7 findings, 6 applied, 2 skipped with reasons |
| `/graphify` | ○ | Waived — `PLANNING.md` authored this session and already in context |
| `/impeccable` | — | N/A per plan; no UI in this plan |

---
*Built with PAUL Framework v1.4 · https://chrisai.cv/skool · https://youtube.com/@chris-ai-systems*
*Phase: 01-plugin-foundation, Plan: 02*
*Completed: 2026-09-07*
