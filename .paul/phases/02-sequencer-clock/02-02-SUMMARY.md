---
phase: 02-sequencer-clock
plan: 02
type: Summary
about: "Forró Box"
description: "A sixteenth-note clock advanced from the audio block's sample position, with swing on odd steps and internal transport, whose step sequence is provably independent of the host's buffer size."
completed: 2026-09-07
---

# 02-02 Summary: Clock core

## What Was Built

`forrobox::Clock` advances a sixteenth-note step counter from the audio block's sample position and
hands each placed step to a `StepListener`. The processor owns one, drives it from `processBlock`,
and carries an internal transport. Nothing consumes the steps yet — Phase 3's voices will.

`PLANNING.md` is explicit that the prototype's 25 ms `setInterval` lookahead scheduler is to be
"replaced entirely". A wall-clock timer cannot place a trigger at a known sample, and a groove whose
timing depends on the host's buffer size is not something a listening test can later diagnose — it
would simply sound slightly wrong.

### Design

- **The step duration stays fractional.** Accumulating a rounded integer per step is exactly the
  drift this avoids; emitted positions come from rounding a running phase, never from summing
  integers.
- **Swing offsets an odd step's placement without advancing the grid**, so it never accumulates —
  the structure of `app.js:637`, not its scheduler. A swung step moves by at most 0.6 of a step, so
  it always lands before the next step's grid position and emissions stay strictly increasing at
  every swing value.
- **`gridPhase` is allowed to go negative.** That is what carries a step owed from a previous block
  across the boundary exactly once.
- **Rounding is `floor(x + 0.5)`, not `juce::roundToInt`.** See below.
- **Steps are delivered through an interface**, not a returned buffer, so there is no capacity to
  overflow: an oversized block simply produces more calls.
- **The clock takes its tempo, swing and window as arguments** rather than reading the APVTS, so the
  timing is swept offline with no processor, host or audio device — and so 02-03 can substitute the
  host playhead without touching the step maths.

### Transport

`playing` is deliberately **neither an APVTS parameter nor persisted state**: `PLANNING.md`'s
parameter-mapping list omits it, a play toggle on an automation lane would fight the host's own
transport, and a plugin that resumes playing when a project is reopened is hostile. This is the
opposite call from `dirty` and `activeProfile`, which are persisted.

`SYNC` is read but not honoured — with sync on the clock still runs on internal tempo. Commented as
02-03's work so the gap reads as scheduled rather than forgotten.

## Acceptance Criteria Results

| AC | Result | Evidence |
|----|--------|----------|
| AC-1 Internal tempo, no drift | ✅ | 8 sixteenths per second at 120 bpm; step 10000 within 1 sample on a fractional grid |
| AC-2 Sequence independent of block size | ✅ | One block of 8192 vs 8192 blocks of 1 vs irregular partitions, identical across 5 bpm/swing/window combinations |
| AC-3 Swing without accumulation or reordering | ✅ | 5 swing values; even steps on the grid, odd delayed exactly, positions strictly increasing |
| AC-4 A swung step crossing a boundary fires once, later | ✅ | 100-sample blocks at swing 100; identical to the single-block reference |
| AC-5 Index wraps over the window, not the storage | ✅ | 16 and 32; mid-run switch neither drops nor duplicates |
| AC-6 Transport start/stop semantics | ✅ | Starts at step 0, stops to -1, restart does not resume mid-pattern, `playing` does not survive a round-trip |
| AC-7 bpm/swing changes mid-stream | ✅ | 40↔300 jumps; unbroken indices, no reordering, count matches the analytic expectation |
| AC-8 No allocation, no locks on the audio path | ✅ | Allocation counter reads 0 across 2000 blocks; `processBlock` takes no lock and never touches `patternState` |
| AC-9 Builds and passes under all three compilers | ✅ | GCC, Clang, MSVC clean of our warnings; 318 + 115 = 433 checks green under each |

## What the Tests Caught That Review Did Not

**Emission was decided on the unrounded placement while the offset was rounded and clamped.** A
placement of 0.6 in a one-sample block clamped to 0; the same step in a large block landed at 1 — a
step sequence that changes with the host's buffer size, the one thing the class exists to prevent.
Found by the block-size invariance case on its first run.

## What the Negative Controls Caught That the Green Suite Did Not

Seven mutations of `Clock.cpp` were run against the passing suite. Two were not detected, and both
were gaps in the tests:

1. **Offsets were never checked against their own block.** Mutating the break condition made the
   clock emit `sampleOffset == numSamples` — one past the end of the buffer the host handed us, which
   in Phase 3 is a write past the end of the audio block. The invariance sweep passed it, because
   every partition can agree on the same out-of-range offset. Sequence equality cannot see this.
2. **The long-run drift case could not see integer truncation.** At 48 kHz and 120 bpm a sixteenth is
   exactly 6000 samples, so the case was asserting against a grid that happened to be integral. It
   now runs at 44100 Hz and 137 bpm, where truncation drifts 4667 samples over 10000 steps.

A third control was reported MISSED while the instrument was fine: the compiler elided its
`new`/`delete` pair, so no allocation ever happened. A counter that is broken or optimised away
reports zero for everything, which looks exactly like success — so the suite now contains a
permanent self-test proving the counter can register a reading.

## `/code-review` Findings

Nine findings, three HIGH. Each was verified independently before being fixed — encoded as a test
that failed against the then-current code — and each fix was then negative-controlled.

| # | Severity | Finding | Resolution |
|---|----------|---------|------------|
| 1 | High | Truncating the `steps` raw value would run a 16-step window while the host displayed 32 | **Not reproducible.** `AudioParameterChoice`'s range carries a `snapToLegalValue` of `roundToInt` that `convertFrom0to1` applies, so the cached raw value is always integral. Verified empirically across normalised 0.0–1.0. `roundToInt` kept as intent-stating; no bug was fixed |
| 2 | High | `juce::roundToInt` rounds ties to **even**, which is not translation-invariant — a block-relative placement rounded differently by block-base parity | Real. 44100 Hz at 40 bpm gives a step of exactly 16537.5 samples; measured a 1-sample disagreement. Now `floor(x + 0.5)`. The original sweep could not see it: its only 40-bpm case used swing 100, whose placements are integral, and 8192 samples never reached step 1 |
| 3 | High | `setPlaying` called `clock.reset()` from the message thread while the audio thread might be inside `advance()`, with no release/acquire pairing | Real. The reset is now requested via an atomic and performed by `processBlock`, on the thread that owns the clock |
| 4 | Medium | A tempo increase collapsed owed swing debt into a burst | Real. Measured 5 steps at offsets `0 0 0 0 368` after 40→300 bpm — four simultaneous voice triggers on one sample in Phase 3. Debt is re-bounded to the current tempo |
| 5 | Medium | The ordering check used `<`, so it could not see two steps at the *same* position — exactly what that burst produced | Real. Now `<=` |
| 6 | Low/Med | A non-finite sample rate hangs the audio thread; `jlimit` cannot sanitise NaN | Real for the **sample rate** (NaN fails `<= 0.0`, the offset rounds to 0, the loop never returns). The claim that NaN *swing* alone hangs was wrong — `gridPhase` still advances on even steps — but NaN swing does corrupt odd placements, so both are now filtered |
| 7 | Low | Cached parameter pointers dereferenced behind only a `jassert`, which compiles away in Release | Guarded in `processBlock` |
| 8 | Low | The Windows test loop aborted on the first failing suite under `pipefail`, and `sort` puts the clock suite first — a clock failure hid every state result | Runs all suites, fails once at the end |
| 9 | Low | `Recorder::currentBlockLength` left at 0 in two hand-rolled loops, so the offset invariant went unchecked on the parameter-change paths | Set in both |

## Deviations From Plan

- **No internal chunking.** The plan required processing in prepared-size chunks to bound the
  per-call event count. The `StepListener` interface removes the buffer that bound was protecting,
  so chunking would have been machinery that bought nothing. The loop terminates because `gridPhase`
  grows by a positive `stepSamples` each iteration, which the input clamps guarantee.
- **The Windows build script gained a fix outside the plan's file list.** It ran a *named* test
  executable, so the clock suite was built under MSVC and never run there while the output still read
  as three-compiler coverage.

## Decisions Made

| Decision | Rationale |
|----------|-----------|
| `playing` is neither a parameter nor persisted | Automation would fight the host transport; auto-play on project open is hostile |
| The clock is a plain class taking Params by value | Sweeps offline with no host; lets 02-03 swap the tempo source without touching step maths |
| Steps delivered through an interface, not a buffer | No capacity to overflow; an oversized block just produces more calls |
| `floor(x + 0.5)` rather than `juce::roundToInt` | Ties-to-even is not translation-invariant, and placements are block-relative |
| The clock reset happens on the audio thread, requested by an atomic | Its fields are plain doubles; resetting from the message thread is a data race, not a stale read |
| Sanitise non-finite inputs at the `Clock` boundary | It is the component whose loop termination depends on them |

## Deferred

- **Host sync** — `AudioPlayHead`, PPQ, locking step 0 to the host bar. 02-03, as planned.
- **Lock-free double-buffer pattern handover** — also 02-03; `processBlock` deliberately does not
  touch `patternState` until it exists.
- **`CACHAÇA` timing jitter** — specified beside swing in `PLANNING.md`, but Phase 3's work.
- **`MSB8064` (23×)** — unchanged in kind; MSBuild lowercases dependency paths.

## Files Created / Modified

| File | Change |
|------|--------|
| `src/Clock.h` | New — `Clock`, `StepEvent`, `StepListener` |
| `src/Clock.cpp` | New — the step maths |
| `src/PluginProcessor.h` / `.cpp` | Owns the clock; transport; `processBlock` integration |
| `tests/TestHarness.h` | New — check helpers extracted so two executables share them |
| `tests/ClockTest.cpp` | New — 115 checks |
| `tests/StateRoundTripTest.cpp` | Transport and step-window cases; 287 → 318 |
| `CMakeLists.txt` | One function for the shared test-target setup; second test target |
| `scripts/build-windows.sh` | Runs every test executable, with a count guard; install banner |

## Next

**02-03: Host sync** — follow `AudioPlayHead`, lock step 0 to the host bar when `SYNC` is on, and add
the lock-free double-buffer handover so the audio thread can read pattern tables. The internal path
is correct and pinned, which is what makes substituting the tempo source safe.
