---
phase: 03-voices-mix-bus
plan: 02
completed: 2026-09-08T14:40:00Z
duration: ~5h
description: "CACHAÇA does something: one per-step jitter on a 32 ms delayed origin, per-hit velocity variation, and ghost notes — with every humanisation value keyed rather than drawn"
type: Summary
about: "Forró Box"
---

# Phase 3 Plan 02: CACHAÇA — Summary

**The signature control works.** One knob, three effects, transcribed from `app.js:633-676` rather
than from `PLANNING.md`'s prose alone. 956 checks green under GCC, Clang and MSVC; 39 negative
controls, all detecting.

## What Was Built

| File | Purpose |
|------|---------|
| `src/Humanisation.h` | **New.** The eight constants, the lookahead derivation, and the keyed random |
| `src/VoiceEngine.h` / `.cpp` | Keyed humanisation, the delayed origin, the collapsed ghost path |
| `src/PluginProcessor.h` / `.cpp` | `cachaca`/`ghost` resolution, reported latency, one `render` call site |
| `src/ParameterIDs.h` | `normalisedPercent` / `normalisedPan` — bounded and NaN-safe |
| `src/Voices.h` / `.cpp` | `trigger` takes a key, not a generator |
| `tests/TestHarness.h` | Displacement, onset-counting and highpass helpers — each now self-tested |
| `tests/VoiceTest.cpp` | +1900 lines: distributions, keying properties, ghost behaviour |

## Acceptance Criteria

| AC | Description | Status |
|----|-------------|--------|
| AC-1 | Timing jitter is one bipolar draw per step | **Pass** |
| AC-2 | Early hits representable, host told | **Pass** |
| AC-3 | Velocity variation per hit, only ever softer | **Pass** |
| AC-4 | Ghosts fire where silent, at the specified rate | **Pass** |
| AC-5 | Ghosts obey the spec's boundaries | **Pass** |
| AC-6 | Audio-thread contract and determinism | **Pass** — with a recorded deviation |

## Verification

| Check | Result |
|-------|--------|
| GCC / Clang / MSVC | 956 / 956 each |
| Windows VST3 bundle | builds |
| Reported latency | 1536 @48k · 1411 @44.1k · 3072 @96k · 259 @8069 |
| Negative controls | **39 run, 39 detect** |
| Allocations, 2000 blocks, CACHAÇA 100 + ghosts 100 | 0 |
| Suite wall time | 1.24 s → **1.10 s** |
| Groove cross-check | OK, `data.js` unmodified |

## The spec numbers, and what the prose omits

`PLANNING.md` gives the three effects; `app.js:633-676` settles four things it does not say, each of
which is silently wrong if guessed:

- the velocity multiplier draws **per hit** while the timing jitter draws **once per step**
- a ghost's velocity is already normalised (0.20–0.32) and does **not** get the multiplier
- a ghost's ±10 ms sits **on top of** the step's ±22 ms, so total reach is ±32 ms
- `CACHAÇA 0` still fires ghosts at `(ghost/100) × 0.22` — it raises the rate, it does not gate it

**The lookahead is 32 ms, not 22**, and that is the first consequence: sized at 22 it would have
clamped ghosts specifically, at a rate rising with `CACHAÇA`, and silently.

## Deviations

| # | Planned | Actual | Why |
|---|---------|--------|-----|
| 1 | AC-6: all randomness from one seeded generator | **Keyed hash + one noise stream** | See below. Reproducibility — what the criterion protected — is stronger: values no longer depend on execution order at all |
| 2 | `jmax(0, offset)` removed | Removed **and** replaced with `jassert` | The plan said to remove them; both survived the first pass |
| 3 | Lookahead as `kLookaheadSeconds × rate` | Derived from the two excursions separately | Rounding the sum falls one sample short at 24 545 of 192 001 integer rates |

## The invariant, twice

**Muting one channel re-timed another.** `/code-review` measured it: at `CACHAÇA` 100, muting the
ganzá moved BB's hits on 11 of 12 steps by up to 21 ms. The velocity and ghost draws were
*conditional* — a gated channel returned before its draw — so draws-per-step depended on mute, solo,
`GHOST` and the pattern, shifting every later step's jitter.

Fixed by drawing unconditionally, and **the fix was itself incomplete.** `/simplify`'s altitude pass
found that `SynthVoice::trigger` drew five more values for the triângulo's detune — one lane only,
after the audibility gate, only for a claimed voice. Muting the *triângulo* still re-levelled
everything else. The header asserting the invariant listed that very detune as part of the stream in
the sentence above the claim, and no test could see it: the mute test measures onset *positions*,
which come from the jitter stream.

**Now keyed.** Every humanisation value is `humanisedValue(seed, step, lane, purpose, index)` —
splitmix64, integer-only so it is bit-identical across compilers. A value is a function of its key,
so draw order, lane iteration order, gating, how many partials the triângulo has, and anything a
later phase adds below `scheduleStep` are all irrelevant **by construction**. There is no stream to
keep in step.

Measured so it is not mistaken for an optimisation: the hash costs **2.460 ns** against
`juce::Random::nextFloat`'s **2.346**, and the whole per-step humanisation is 16 ns of a 10 667 µs
block.

## Other findings from the reviews

**One `render` call site, not three.** A `MixBus` added at the normal path alone would have left the
ring-out after **Stop** unlimited and at unity master — at the default master of 82, `(0.82)² =
0.672`, so pressing Stop would make a decaying zabumba jump **+3.5 dB** and lose the wet path.
Audible, on the most common gesture in the plugin.

**The lookahead cannot live in the `Clock`**, settled definitively and recorded in `Humanisation.h`:
shifting each span's start breaks the exact tiling that makes no-duplicate/no-gap structural, by
`L × (rate₂ − rate₁)` at every tempo change; shifting the watermark keeps tiling but makes the lead
`shift / rate`, so doubling the tempo halves it to 16 ms while the engine still adds 32.

**A solved TODO still written as open work.** `scheduleStep`'s doc ended with "NOT yet solved, and
03-02 must… offsets are clamped at zero" — describing this plan's own shipped fix. Written in 03-01
as a note to my future self, then implemented and never revisited.

**`CACHAÇA` was normalised at three use sites**, one of which carried a `>= 0.0f` test that looked
dead (jlimit already bounds it) but was catching **NaN**, which jlimit passes through. The other two
had no guard. Now one `ids::normalisedPercent` at the boundary.

**Tests 25% faster for one line.** APVTS is a `juce::Timer`, so every rig is a timer, and when it is
the only one alive its destruction tears down JUCE's shared timer thread for the next construction
to recreate. Measured per lifecycle: ctor 0.169 ms, prepare 0.567 ms, **dtor 2.179 ms — a wait, not
work**. One `Timer` held for the process: 1.24 s → 0.93 s.

**Three efficiency findings measured cheap and were left alone**: iterating all 176 voices is
0.077 µs idle and 0.079 µs with every slot pending; the O(128) voice claim is 0.09% of a saturated
block; `highpassed` is 1.4 ms of a 647 ms suite.

## The measurements were wrong before the code was — nine times now

Across 03-01 and 03-02:

| Wrong measurement | Why it was wrong |
|---|---|
| Threshold-crossing onset comparison | A noise burst and a 130 Hz sine reach 5% of peak tens of samples apart |
| Peak of filtered **noise** as a velocity probe | Itself a random variable — read a 0.567 spread against a multiplier that cannot go below 0.75 |
| 2400-sample search radius | Reached into the previous hit's tail; wrong hit on one step in twelve |
| Band that measured HH while claiming BB | Four kit lanes share one `ghost` parameter |
| A highpass that **attenuates rather than erases** | Its 5e-4 residual read as an onset; failed a test against *correct* code |
| 2σ bound on 16 jitter trials | A 2.5σ realisation trips it once in eighty runs, and did |
| Per-bucket floors for uniformity | Triangular gives 8/24/24/8 against uniform's 16 — both pass |
| Two lanes compared across **separate renders** | A per-lane draw consumes the stream in the same order; the test could not fail |
| `bandEnergy` on a pure tone | Its semitone grid samples 900, 953.5, 1010.2, 1070.3 — **never 1000 Hz** |

The last one was found by the new instrument self-tests, on their first run. `TestHarness.h` already
carried the right pattern in `checkAllocationCounterRegisters` — "whoever asserts on it must also
prove it can register a reading" — applied to one instrument out of ten. The other nine are now
self-tested against synthetic signals with known answers, and the `bandEnergy` limitation is
documented at the helper.

## 03-03's limiter design input

**1.454 (+3.25 dBFS)**, worst of 36 realisations, hottest CARUARU.

Against the **1.336** a single realisation reports. 03-02 first asserted "humanisation does not raise
the worst peak" from one draw and handed that to 03-03; a review measured 1.408 under a different
draw order, because ghosts *add* voices and voices sum. The claim was false and the figure 12% low —
and even the corrected single-draw number is 9% low.

It could not be improved from outside: the seeds were private with no injection point.
`setHumanisationSeedOffset` is that seam, and it exists for exactly this.

## Deferred

| Item | Effort | Why |
|------|--------|-----|
| Test duplication: `renderSteps`, `stepPeaks`, `stepDisplacements` helpers | S | The block-aligned render idiom appears 12 times in two spellings; the per-step window idiom 5 times; `JitterRig` is used in 3 of the 5 places it fits. ~110 lines |
| `bandEnergy`'s semitone grid in `testGhostsOnlyWhereAllowed` | S | 30.6 ms of that test's 54.5 ms is the instrument, not the render. A whole-tone grid halves it |
| `testNoAllocationsWhileRendering`'s 2000-block window | S | 147 ms for 4 checks; 500 blocks would still cover hundreds of steal/retire cycles. The number is in the assertion text |
| `getVoicesDropped()` still has no reader | S | This plan's predecessor added two write sites to make it reachable. Either assert it or call it write-only |
| Move measurement out of `TestHarness.h` | S | ~250 of its 424 lines are DSP used by one suite; `ClockTest` and `StateRoundTripTest` recompile on every edit to it |

## Next

**03-03: character bus, limiter, master.** Its limiter threshold sizes from 1.454 above. The
altitude review traced that `engine.render(buffer)` plus a sibling `MixBus` works — now that there is
one call site — but flagged that `getLatencySamples()` and `getTailLengthSeconds()` become chain sums
once a second stage exists, and that `timbre`/`charMix`/`limiterOn`/`master` are output-stage values,
so `resolveChannelSettings` may want splitting rather than growing.

---
*Completed: 2026-09-08*
