---
phase: 03-voices-mix-bus
plan: 03
completed: 2026-09-08T20:15:00Z
duration: ~4h
description: "The output stage — character bus, limiter, master. First time in the project that nothing clips."
type: Summary
about: "Forró Box"
---

# Phase 3 Plan 03: Character bus, limiter, master — Summary

**The signal path is complete.** Eight voices → character bus → limiter → master, and the four
grooves peak at **0.571 / 0.806 / 0.890 / 0.669** where the unlimited engine reached **1.454**.
1092 checks green under GCC, Clang and MSVC; 20 negative controls for this plan, 19 detecting and
one a documented numeric no-op.

## What Was Built

| File | Purpose |
|------|---------|
| `src/MixBus.h` / `.cpp` | **New.** `timbreSpecs`, the per-sample core, the limiter, the master taper |
| `src/PluginProcessor.h` / `.cpp` | `mixBus` member, `resolveBusSettings`, `timbreChoices()`, both host overrides as chain sums |
| `src/ParameterIDs.h` | The prototype-fidelity tie-breaker, recorded where the next reader will hit it |
| `tests/TestHarness.h` | `maxDifference` |
| `tests/VoiceTest.cpp` | `BusRig`, `useShippedChain`, +~700 lines |

`MixBus` is a **sibling** of `VoiceEngine`, not a member of it: the engine renders, the bus shapes.
`PluginProcessor::processBlock` ends with exactly two calls, in order, on every path.

## Acceptance Criteria

| AC | Description | Status |
|----|-------------|--------|
| AC-1 | The character bus shapes the sound the way the spec says | **Pass** |
| AC-2 | HI-FI is halved and the dry path is not | **Pass** |
| AC-3 | Cutoff, drive and mix changes are smoothed, not stepped | **Pass** — after a real bug |
| AC-4 | The limiter limits, reports, and is transparent when off | **Pass** — after a second pass |
| AC-5 | Master applies a squared taper | **Pass** |
| AC-6 | The whole chain, and the contract | **Pass** |
| AC-7 | The grooves audibly match the prototype | **Pass** — approved at the checkpoint |

## Verification

| Check | Result |
|-------|--------|
| GCC / Clang / MSVC | 1092 / 1092 each |
| Windows VST3 bundle | builds |
| Audition peaks, four grooves | 0.571 · 0.806 · 0.890 · 0.669 — against **1.454** unlimited |
| Reported latency | `engine.getLookaheadSamples() + MixBus::kLatencySamples` (the bus adds 0) |
| Negative controls, this plan | **20 distinct mutations, 19 detect** |
| Allocations, 2000 blocks, full chain | 0 |
| Two fresh instances | bit-identical |
| Groove cross-check | OK, `data.js` unmodified |

## The spec's arithmetic, which does not sum to one

`dry = 1 − wet × 0.5`, and `wet` is halved again for HI-FI. At MIX 100 that is dry 0.75 + wet 0.5 for
HI-FI and 0.5 + 1.0 for the other two — **1.25 and 1.5**. `juce::dsp::DryWetMixer` was rejected for
exactly this: its `linear` rule *is* `dry = 1 − wet`, which is a different mix, not a tidier spelling
of the same one. The paths deliberately sum past unity, and the limiter downstream is what makes that
safe rather than clipped.

Two tonal decisions went the **opposite** way to each other, and the tie-breaker is now written down
in `ParameterIDs.h` rather than re-argued per case:

- the bus **keeps** Web Audio's default lowpass Q of 1.0 — an unchosen default, free and audible to
  reproduce
- it does **not** reproduce `WaveShaperNode`'s clamp outside [−1, 1] — an unchosen artefact that
  would put a hard ceiling at an arbitrary input level, and the grooves reach 1.454

> **reproduce an unchosen prototype default unless reproducing it would make the plugin audibly
> worse at levels the plugin actually reaches.**

## Deviations

| # | Planned | Actual | Why |
|---|---------|--------|-----|
| 1 | Gain reduction as the block's peak ratio | **Instantaneous gain**, `min(|limited|/|shaped|)`, max-since-read | A peak ratio measures two different samples; the meter is a gain meter, like `DynamicsCompressorNode.reduction` |
| 2 | `getDryGain` / `getCutoffHz` / `getMasterGain` accessors | Deleted | Dry is a pure function of wet and master of its parameter, so the UI computes both — and all three were races with zero callers |
| 3 | `dryGain` a member | Derived per sample | One field fewer to keep in step, and now via one shared `dryForWet` |

## AC-3 was a real bug, and my own comment concealed it

`drive` was **not smoothed**. Cutoff, wet and master all were; drive stepped. The comment over it
claimed the step was masked by the other smoothers, and that was false in two of the three
transitions the plugin can actually make:

- **LO-FI ↔ HI-FI** at MIX 40 — a PETROLINA style switch — is a **−2.8 dB single-sample step**
- **LO-FI → CICLOTRON** shares its cutoff and mix, so *nothing else smooths at all*

Found by `/simplify`'s altitude pass, not by a test, because the test only exercised HI-FI → LO-FI,
where the cutoff sweep does mask it. The test now covers three transitions and bounds each against
the **steeper endpoint** rather than the destination — `tanh` has slope `drive` at zero, so the
destination's slope is the wrong reference when drive is what changed.

**Smoothers also ramped in from constructor defaults** instead of snapping, so two identically
configured instances rendered differently for the first 20 ms — which is AC-6's determinism
criterion failing quietly, since both instances were "correct" in isolation. `needsSnap` fixes it.

## The two gaps the controls found after `/simplify`

Nine mutations against the cleanup pass; six failed as intended, two survived and were real, one was
a numeric no-op (`MixBus::kTailSeconds` is 0.0, so dropping it from the chain sum changes no value —
the sum is there for the next stage, not for today's arithmetic).

**`dry = 1 − 0.5 × wet` was written out twice.** Once in `dryGainFor`, which four checks cover, and
again per sample in `process`, which nothing covered. The comment over the second copy claimed that
expressing it there "enforces it" — it did the reverse. An earlier control had already mutated the
*accessor's* copy and been detected, which is precisely why the duplicate looked covered. Changing
the per-sample copy to `1 − wet` shifted every rendered dry path and passed all 1090 checks. Now one
`constexpr dryForWet` that both call.

**Nothing tied the TIMBRE parameter's order to `timbreSpecs`.** `timbreChoices()` builds the strings
from the table, but rewriting the `StringArray` by hand as `{ LO-FI, HI-FI, CICLOTRON }` left the
plugin displaying LO-FI while rendering HI-FI's 16 kHz and 1.2 drive, and passed. Those strings are
what host automation lanes and saved projects carry, so their order is a **compatibility** property.
`testFactoryDefaults` now compares choices to specs index by index.

Both re-run after the fix: both detect.

## The measurements were wrong before the code was — twelve times now

Three more from this plan, on top of 03-02's nine:

| Wrong measurement | Why it was wrong |
|---|---|
| Input slope as the smoothing reference | `tanh` has slope `drive` at zero, so the output step is the *product*; the bound has to come from the steeper endpoint |
| Whole-buffer peak as a limiter check | It measured the 2 ms attack's overshoot, not the steady state |
| `takeGainReductionDb()` called twice in one expression | Argument evaluation order is unspecified — the message read 8.13 dB while the condition read the 0 left behind |

And one worse than a wrong measurement: **the audition diverged from the headline test.**
`renderAuditionFiles` inherited `AudioRig`'s deliberately transparent bus, so it reported clipping at
1.19–1.28 while `testFullChainHeadroom` measured 0.753 on the same grooves. A rig default chosen to
isolate the engine had silently become the shipping configuration's stand-in. `useShippedChain` is
now the named complement, and the audition calls it.

## Other findings from the reviews

**Both host-facing overrides are chain sums now.** `getLatencySamples` and `getTailLengthSeconds`
each add `MixBus`'s contribution, and the bus declares `kLatencySamples = 0` / `kTailSeconds = 0.0`
explicitly rather than being omitted — so a future lookahead limiter changes one constant, not two
call sites in another class.

**One efficiency finding was declined on measurement.** Hoisting `getWritePointer` out of the sample
loop: 13.316 → 13.338 µs/block, inside noise. Recorded rather than argued.

**Every `/simplify` agent now runs read-only in a worktree.** In 03-01 the efficiency agent wrote
timing instrumentation into `tests/VoiceTest.cpp`, and the reuse agent then reported it as a finding
about my code.

## Deferred

| Item | Effort | Why |
|------|--------|-----|
| Per-voice gain and pan smoothing | M | Still unowned. The bus smooths its four values; per-voice VOL/PAN still step on automation |
| Merge the two voice pools | M | 128 synth + 48 sample slots, iterated separately |
| Whether to embed `ZAB_LOW_03` at all | S | It is a *pá* hit misfiled as a low variant; it works as a velocity layer but was not chosen as one |
| Test duplication | S | The block-render idiom is now **16 sites in three spellings**, not 12 in two |
| `getVoicesDropped()` still has no reader | S | Unchanged from 03-02 |
| Move measurement out of `TestHarness.h` | S | ~250 of its lines are DSP used by one suite |
| `bandEnergy`'s semitone grid cost | S | 30.6 ms of one test's 54.5 ms |
| `testNoAllocationsWhileRendering`'s 2000-block window | S | 147 ms for 4 checks |

## For the phase transition

**`ids::outputMode` ships inert.** It is declared, host-visible, automatable and persisted, and
nothing reads it — STEREO vs MULTI-OUT is not implemented in v0.1. Removing it later is a saved-state
compatibility question, so **Phase 4 (UI shell) is the phase that must decide**: draw it as a
working control, draw it disabled, or drop the parameter before any user has state to invalidate.

---
*Completed: 2026-09-08*
