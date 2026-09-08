---
phase: 03-voices-mix-bus
plan: 01
completed: 2026-09-08T07:20:00Z
duration: ~3.5h
description: "The sequencer became audible: a persistent VoiceEngine renders seven synth voices plus a sampled zabumba through per-channel gain, pan, mute and solo"
type: Summary
about: "Forró Box"
---

# Phase 3 Plan 01: Voices — Summary

**The plugin makes sound for the first time in the project.** Seven synthesised voices transcribed
from `PLANNING.md`'s Voice Specifications, plus a zabumba playing four embedded one-shots, summed
through per-channel gain and stereo pan with mute and solo applied. 842 checks green under GCC,
Clang and MSVC; Windows VST3 bundle builds; 33 negative controls run.

## Objective

Give the velocities a voice, and get the engine's *lifetime* right — 02-04's altitude review had
recorded that `BlockEmitter` is the right adapter and the wrong owner, and named the three features
that break a block-scoped object. All three arrive in 03-02.

## What Was Built

| File | Purpose | Lines |
|------|---------|-------|
| `src/VoiceEngine.h` / `.cpp` | Persistent engine: pools, sampler, one seeded RNG, output stage | 366 / 449 |
| `src/Voices.h` / `.cpp` | Declarative voice recipes + the synthesised generator core | 293 / 372 |
| `src/ZabumbaSampler.h` / `.cpp` | Decode, measure, classify, crossfade and read the one-shots | 186 / 358 |
| `assets/samples/*.wav` + `README.md` | Four embedded one-shots, 456 KB, with their measurements | — / 70 |
| `tests/VoiceTest.cpp` | The measuring suite | 1793 |
| `tests/TestHarness.h` | Gained the shared audio measurements and the counter self-test | +135 |
| `scripts/render-audition.sh` | Renders each groove to a WAV for A/B listening | 45 |
| `CMakeLists.txt` | Inlined binary-data helper (see deviations), new sources, new suite | +130 |

## Acceptance Criteria

| AC | Description | Status |
|----|-------------|--------|
| AC-1 | Audible and sample-accurate; onsets within 1 sample; no NaN | **Pass** |
| AC-2 | Each voice measurably the voice the spec names | **Pass** |
| AC-3 | Velocity, VOL, PITCH, DECAY, PAN all do what they say | **Pass** (one clause revised — see deviations) |
| AC-4 | Zabumba plays the embedded one-shots, velocity-layered | **Pass** (two clauses revised) |
| AC-5 | Mute and solo gate audio and nothing else | **Pass** — all 1024 combinations swept |
| AC-6 | Audio-thread contract holds | **Pass** — 0 allocations across 2000 blocks |
| AC-7 | Rendering deterministic and block-size independent | **Pass** |

## Verification

| Check | Result |
|-------|--------|
| GCC / Clang / MSVC | 842 / 842 each |
| Windows VST3 bundle | builds, PE32+ DLL |
| Groove cross-check vs `data.js` | OK, `data.js` unmodified |
| Allocations, 2000 blocks with voices sounding | 0 |
| Suite wall time | 5.22 s → **0.95 s** |
| Four grooves render | 0.59 s → **0.13 s** (same compiler) |
| `assets/samples` | 460 K |
| Negative controls | **33 run, 30 detect** |

## Deviations

| # | Planned | Actual | Why |
|---|---------|--------|-----|
| 1 | Resample to host rate once in `prepare` | Rate folded into the read increment | `PITCH` already forces a fractional read, so pre-resampling interpolates twice for more memory and worse quality. The property the plan wanted is tested: nothing resampled in `render`, and the hit lasts the same *time* at 44.1 k and 48 k |
| 2 | AC-4: rendered **peak** rises with velocity | Asserted on **RMS** | The layers' peak/RMS ratios are 3.8, 6.2 and 3.6, so peak is not monotonic even though loudness is. Forcing it would mean peak-normalising, which makes the softest sample the loudest thing in the set |
| 3 | Four velocity layers | **Three** layers + one alternate articulation | Measurement contradicted the premise the engine decision rested on — see below |
| 4 | `./scripts/build-linux.sh` in the verify steps | Direct cmake into `build-linux` / `build-clang` | That script has never existed; my plan named it wrongly |
| 5 | `juce_add_binary_data` | Inlined without its `WORKING_DIRECTORY` | JUCE hardcodes it to the source dir; cmd.exe refuses a UNC current directory, which failed the Windows build outright. Every path it passes `juceaide` is already absolute |
| 6 | Voices cut on transport stop | Voices ring out | `PLANNING.md`'s "stopping clears the playhead" is about visible state; cutting a decay clicks. `getTailLengthSeconds()` now reports the real tail |

## The measurement that changed the plan

The hybrid engine decision was taken on a recorded description of the samples as "4 one-shot
variants, single articulation". Measurement contradicted it:

| File | Length | Peak | RMS | Peak/RMS | Centroid | Brightness |
|------|--------|------|-----|----------|----------|------------|
| `ZAB_LOW_04` | 0.265 s | −23.1 dBFS | 0.0182 | 3.8 | 125 Hz | 0.235 |
| `ZAB_LOW_02` | 0.457 s | −2.5 dBFS | 0.1223 | 6.2 | 126 Hz | 0.222 |
| `ZAB_LOW_01` | 0.498 s | −3.1 dBFS | 0.1958 | 3.6 | 96 Hz | 0.143 |
| `ZAB_LOW_03` | 0.344 s | −2.0 dBFS | 0.0373 | **21.3** | **527 Hz** | **0.810** |

`01`, `02` and `04` are one strike at three levels — 87–98% of their energy below 160 Hz, centroids
within 30 Hz. `03` is the stick (*pá*) hit: 78% of its energy *above* 160 Hz. Three velocity
layers ordered by measured RMS (which comes out reversed from filename order), RMS-normalised, and
crossfaded so no velocity step jumps more than 6 dB.

## Findings from the spec that the spec table alone would have got wrong

Cross-checking `PLANNING.md`'s table against `audio.js` found three things the table omits:

- the triângulo's outer envelope peak is `0.5v`; the table gives only the `0.12/(i+1)` partial weights
- the envelope floor is `0.0001` absolute, not `0.001`
- **`pitchFactor` is applied per component, and inconsistently.** Ganzá's bandpass tracks pitch, the
  triângulo's partials do but its bandpass does not, and CX's, HH's and pandeiro's noise filters are
  all fixed. A global pitch factor would be wrong for five of the eight lanes

## `/code-review` — 8 findings, all resolved

Two false claims of mine, two real latent bugs, four traps. The two worth carrying forward:

- **`checkEqual(getVoicesDropped(), 0, …)` could not fail.** `claimSynthVoice` steals rather than
  returning null, so both `++voicesDropped` branches were dead. The pool arithmetic it guarded was
  *also* wrong — the header claimed "seven lanes × 17 = 119" when only the triângulo reaches 17 and
  the real sum is 51. Measured peak concurrency is 48/53/**70**/61 at velocity 40/64/90/127, and
  mid-velocity is the *worse* case because the crossfade sounds two layers there.
- **`fileSampleRate` was per-set, not per-file.** Taken from the first file that decoded and applied
  to every slot, making the class's own documented promise false. A 44.1 kHz replacement — which the
  source archive contains — would play 8.8% fast while the others stayed correct.

## `/simplify` — four agents, and it rewrote the generator core

| Angle | Outcome |
|-------|---------|
| Reuse | 9 findings; also **verified five things are correctly hand-rolled** — `juce::dsp::Panner` adds a fixed +3 dB and a 50 ms glide the Web Audio law does not have, and JUCE has no band-limited oscillator at all |
| Simplification | 22 findings; dead fields, derivable state, duplication |
| Efficiency | 8 findings, all measured; **three were "looks expensive, measures free" and were left alone** |
| Altitude | Confirmed four things at the right depth, and found the seam mismatch below |

**The seventh assertion in this project that could not fail — and it was the one guarding the
allocation counter.** My copy of the counter self-test dropped the `volatile` escape that
`ClockTest.cpp` documents as necessary (beside a note recording that a real control was *missed* for
exactly that reason), and it called `check()` between the two counter reads. `check()` takes a
`juce::String`, `juce::String` has no small-string optimisation, so a literal always allocates
through the same replaced `operator new[]` — the assertion passed on its own description string.

**`bandlimitedSquare` was 74% of the whole engine's render cost.** 107.7 ns/sample for the triângulo
against 5.5–21.1 for every other lane, and 65.9% of a 512-sample budget with its pool saturated. Now
each partial's sin and cos are carried forward by a rotation — legal precisely because these partials
never sweep — with the odd-harmonic count precomputed so three of five partials skip the recurrence.
Verified rather than assumed: all four grooves render to within **7.15e-07, −119.9 dB below peak**,
which is the 24-bit quantisation floor of the WAV itself.

**`testMuteSoloTruthTable` spent 2658 ms of 4822 for two checks — and not on the truth table.**
Constructing a processor is 0.126 ms and destroying one 0.037 ms, but construct-then-destruct in a
loop measured 2.277 ms with 2.064 ms *blocked*: JUCE's timer thread, which APVTS drives, was torn
down and recreated every time the live count went 1 → 0 → 1.

## The altitude finding that contradicts this plan's own success criterion

My plan claimed the persistent-engine shape "can carry 03-02's jitter queue without being
restructured". That is **true for late offsets and false for early ones**, and I would not have found
it without the review.

`app.js` draws `CACHAÇA`'s timing jitter **once per step** and moves every lane of that step
together. My seam was one `schedule()` per lane, which invites a per-lane draw — not a compile error
and not a test failure, just the whole step breathing against the lanes flamming apart. Fixed:
`scheduleStep` takes all eight velocities and one offset, which also hands the engine the "this lane
had no hit" fact that ghost notes fire on. A net deletion.

**Not fixed, and 03-02 must:** jitter is bipolar and offsets are clamped at zero, so there is no
representation for a hit *earlier* than its step. ±22 ms at 48 kHz is ±1056 samples, wider than two
512-sample blocks, so clamping would collapse the early half onto the block boundary and make the
render block-size dependent — the one property AC-7 exists to protect. The fix is a scheduling origin
delayed by the jitter's own maximum, so `offset = lookahead + jitter` is non-negative by
construction.

## Negative controls — 33 run, 30 detect

Run against a committed tree in a throwaway build directory, refusing to run dirty, asserting each
mutation reached disk first. One anchor failed to match and was scored **invalid rather than
passing** — 02-01's rule earning its keep.

Six controls went undetected across the rounds and each produced a change:

| Undetected | Why the tests could not see it | Resolution |
|------------|--------------------------------|------------|
| Layers normalised by **peak** not RMS | Velocity spans 18 dB across the sampled points; peak-normalisation's inter-layer error is 4.7 dB and the crossfade smears it. The ramp stayed monotonic while every layer sat at the wrong level | Assert `gain × measured RMS` equals the target — spans 11× when wrong |
| Ganzá's bandpass stops tracking `PITCH` | The `PITCH` test measured only TOM, whose pitch lives in an oscillator. Ganzá has **no oscillator** — its bandpass centre *is* its pitch | Measure ganzá at 6.8 kHz and an octave up, **plus** HH's deliberately fixed highpass as the mirror |
| Odd-harmonic count forced to 1 | The spectral band is 5–12 kHz and every partial is inside it; the dropped harmonics are at 16.2 and 20.6 kHz | Assert the 14–22 kHz band against the noise floor (measured 10.4×) |
| Sweep ratio forced to 1 | bb's loud band is 45–150 Hz and its *start* frequency of 130 Hz is inside it. The obvious repair fails too: bb has 8× more energy near the sweep start than its end | Measure which frequency dominates **early against late** (margins 16×/33× and 75×/177×) |
| Per-slot file rate, per-voice channel, mono pan law | **No-ops on the shipped data** — all four files are 48 kHz and lane 0 is the only sampled lane | Invariant tripwires, each proven to fire: a second `usesSample` lane fails 3 checks, a mono slot fails 5 |

## Deferred

| Item | Effort | Why deferred |
|------|--------|--------------|
| Merge the two voice pools into one `PooledVoice` | M | The right shape, and unlocks both per-strip `LOAD` and the *pá* articulation — but it is a rewrite of `render`, and 03-01 is green across three compilers. `usesSample` is `constexpr`, so `LOAD` cannot be added without changing the discriminator's type |
| Whether to embed `ZAB_LOW_03` at all | S | The brightness classifier's only production effect is to exclude a file nothing can play. Not embedding it would delete the classifier, its threshold and the alternate counter. Product-shaped, so recorded rather than decided |
| Designated initialisers for `voiceSpecs` | S | 11 anonymous positional values per row in a C++20 project; named fields would be shorter *and* more diffable against `PLANNING.md` |
| Gain/pan smoothers have no owner | S | Named in STATE's Phase 3 design input, but 03-02 and 03-03 both omit them. VOL/PAN are constant per block, so automating VOL steps at block boundaries |
| `SampleVoice` carries `pos` and `position` | S | Same quantity in two units, plus `lengthSamples` backing a near-unreachable loop clause |
| Test duplication | S | `renderSingleHit` needs a setup hook (14 sites bypass it), a `loadProfile` helper (3 copies), and `renderBlocks` in the harness (~13 copies across all three suites) |

## Process note

The efficiency agent wrote timing instrumentation **into `tests/VoiceTest.cpp` in the repo** rather
than the scratchpad. It reverted it, but the reuse agent had meanwhile reported "25 hand-inlined
timing blocks" as a finding about my code — an artifact of another agent's scratch work. Acting on it
would have been a self-inflicted change justified by nothing. Parallel review agents need read-only
scope, or a worktree each.

## Next

**03-02: `CACHAÇA` humanisation** — per-step timing jitter, velocity variation, ghost notes. Its
first design constraint is recorded above: the bipolar jitter needs a delayed scheduling origin, and
its draw belongs in `scheduleStep`, once per step.

---
*Completed: 2026-09-08*
