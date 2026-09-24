---
phase: 09-content-convolution
plan: 07
subsystem: audio
tags: [convolution, ir, dsp, parameters, real-time]

requires:
  - phase: 03-voices-mix-bus
    plan: 03
    provides: "the limiter the IR stage sits upstream of, and char_mix's existing meaning"
provides:
  - "Convolver — the IR stage, lazy, gated, real-time safe"
  - "conv_mix — the 47th parameter, no UI, host-exposed"
  - "LOAD IR… wired, async, with the path persisted and degrading"
  - "updateReportedLatency — one writer for CACHAÇA's delay plus the IR stage's"

affects: [09-08, 09-09]

tech-stack:
  added: []
  patterns:
    - "Never free an object the audio thread may be inside; lower a gate instead"
    - "A check whose two sides are structurally equal cannot fail"

key-files:
  created: [src/Convolver.h, src/Convolver.cpp]
  modified: [src/ParameterIDs.h, src/PluginProcessor.h, src/PluginProcessor.cpp,
             src/ForroBoxState.h, src/ForroBoxState.cpp, src/SidePanel.h,
             src/SidePanel.cpp, CMakeLists.txt,
             tests/StateRoundTripTest.cpp, tests/VoiceTest.cpp]

key-decisions:
  - "MIX keeps its meaning; the convolution gets conv_mix — the user's call"
  - "The stage runs BEFORE the mix bus, so the limiter still protects the output"
  - "The engine is built on first load and never destroyed"

patterns-established:
  - "An audio-thread handle is published through an atomic, not a bare pointer"

duration: ~3h
started: 2026-09-24T09:00:00-03:00
completed: 2026-09-24T12:00:00-03:00
description: "LOAD IR… — an impulse response through juce::dsp::Convolution, with its own wet parameter"
type: Summary
about: "Forró Box"
---

# Phase 9 Plan 07: `LOAD IR…` and the convolution stage — Summary

**`LOAD IR…` stops being a stub. It opens a file chooser, loads an impulse response through
`juce::dsp::Convolution`, and `conv_mix` — the plugin's 47th parameter — sets how much you hear. The
path persists, and a project whose IR has moved plays dry while still naming what it wants.**

## Performance

| Metric | Value |
|--------|-------|
| Tasks | 2 of 2 |
| Checks | 4848 → **4877** |
| Mutations | 7, each confirmed applied on disk before its result was read |
| `/code-review` | once after Task 2 — **10 findings, 3 HIGH, all 10 fixed** |
| Parameters | 46 → **47**, five count tripwires moved deliberately |

## Acceptance Criteria Results

| Criterion | Status | Evidence |
|-----------|--------|----------|
| AC-1: audible and bounded | **Pass** | Byte-for-byte identical to dry at 0, audibly different and unclipped at 100 |
| AC-2: `MIX` did not change meaning | **Pass** | `char_mix` untouched; `conv_mix` is separate and the count is 47 |
| AC-3: the audio thread stays clean | **Pass** | Zero allocations across 2000 blocks THROUGH the convolution — and the load transient is measured and bounded rather than hidden |
| AC-4: latency is honest | **Pass** | The reported value is CACHAÇA's delay **plus** the stage's, with a non-zero term forced so the check can actually fail |
| AC-5: a missing IR degrades | **Pass** | Plays dry, keeps the path, and an unreadable file is now refused outright |

## Verification

| | Result |
|---|---|
| GCC 13 / Clang 18 | **4877 / 4877**, real exit 0 |
| MSVC 2022 | **4877 / 4877** — read from the build log, because the process hangs after printing (09-06's finding; the `FATAL` line is the kill) |
| Cross-checks | all six, run together |
| Warnings from our sources | zero |

## Decisions Made

| Decision | Rationale | Impact |
|----------|-----------|--------|
| `MIX` keeps its meaning | `PLANNING.md:841` says "MIX becomes the convolution wet amount", which would give one automatable parameter two meanings depending on whether a file happened to load. The user chose a separate parameter — PROJECT.md's rule for prototype/plugin conflicts, and 07-03's exact precedent with `midi_gate` | `conv_mix` is the 47th, with no UI; the host exposes it generically |
| The stage runs BEFORE the mix bus | `MixBus::process` is a per-sample loop and `juce::dsp::Convolution` is block-based; splitting the one audio path proven since 03-03 is not worth it | **The limiter still protects the output** — an IR with gain cannot push past it. That property decided the placement |
| The engine is built on first load | `juce::dsp::Convolution` starts a background thread on construction whether or not an IR ever arrives. Held by value it took the suite from 5.3 s to over two minutes — ~95 processor constructions | A plugin with no IR pays nothing: no thread, no FFT buffers |
| And never destroyed | See the review section — destroying it raced the audio thread | `clear()` lowers a gate instead |

## What the tests caught before the review did

**The first draft discarded `CACHAÇA`'s latency.** `PluginProcessor.cpp:130` has reported
`outputDelaySamples()` since Phase 3 — 1536 samples, 32 ms, because 03-02 delays the whole engine so
a humanised hit can land early as well as late. Two new `setLatencySamples` calls **overwrote** it.
The plugin would have played 32 ms late while telling the host it was on time. That is why AC-4 was
written as "the reported value equals what the stages actually add" rather than against a number.

**And one of my comments was an overclaim.** I wrote that the zero-wet early return is "the
difference between the same buffer and the same buffer within a rounding error". It is not:
`applyGain (1.0f)` and `addFrom (…, 0.0f)` are exact, so the output is bit-identical either way.
**The mutation removing the branch passed**, which is how it was found. What it actually buys is the
FFT work of an inaudible reverb.

## What `/code-review` found

Ten findings, **three HIGH**, all fixed. The audio-thread ones are the serious part.

### A use-after-free, reachable from any host

`Convolver::clear()` ran `convolution.reset()` from the message thread — destroying the engine, and
its internal queue thread, while the audio thread could be inside `convolution->process`. The path
is `setStateInformation` → `restoreImpulseResponse`, and hosts call that **during playback**: project
load, preset change, undo.

The engine is now **never destroyed** once built. `clear()` lowers an `std::atomic<bool>` gate that
the audio thread acquires before touching the pointer. The cost is a thread that outlives its IR;
the alternative was freeing an object another thread was inside.

### And the lazy construction published a half-built engine

`convolution = std::make_unique<…>()` made the pointer non-null; `prepare()` ran on the *next* line.
An audio callback in that window would call `process` on an unprepared engine. The engine is now
built into a local, prepared, and only then moved into the member — with the atomic gate raised last,
release-ordered, so everything is visible before anything is reachable.

### The "one function, because two writers disagreed once already" comment was false

`prepareToPlay` still ended with its own `setLatencySamples (outputDelaySamples())`, 18 lines below
the `updateReportedLatency()` that was supposed to replace it — **so the second writer won**. Every
host device change would silently drop the IR stage's latency. It survived only because the engine's
head is structurally zero.

### And the check that should have caught it could not

AC-4's assertion compared `getLatencySamples()` against `humanisation + convolverLatencyForTest()` —
but `convolverLatencyForTest()` is always 0 by construction, so both sides reduced to `humanisation`
and the check meant "latency is unchanged". It caught the *first* draft only because that draft
dropped CACHAÇA's delay entirely.

A test-only seam now forces a non-zero stage latency, and with it the mutation restoring the second
writer fails: **expected 2313, got 1536**.

### Five more

- **An undecodable file reported success.** `loadImpulseResponse` checked only `existsAsFile()`, and
  JUCE's loader returns an empty buffer at `sampleRate == 0` when it cannot make a reader — so an mp3
  renamed `.wav` collapsed the bus to near-silence with no diagnostic. A reader is now built where
  failure can still be reported, and a test renames a text file to `.wav` to prove it.
- **The UI discarded the result**, so a failed load closed the dialog and looked like success —
  where 07-02 one control over raises a message box for exactly this reason. It does now.
- **`convMixParam` was read without the `parametersResolved` guard** every other raw parameter read
  goes through, so a failed APVTS lookup would null-deref on the audio thread.
- **The IR path was not validated as absolute.** A project saved on Windows and opened on Linux
  trips `parseAbsolutePath`'s assertion on every load in a debug build. The string is still kept.
- **`restoreImpulseResponse` reloaded unconditionally**, re-reading the file and forcing a JUCE
  engine swap — which the allocation test documents as allocating on the audio thread — on every
  undo and A/B compare.

Plus doc comments stacked onto the wrong declarations, for the **fifth** time in five plans.

## Deviations from Plan

| Type | Count | Impact |
|------|-------|--------|
| False comment corrected | 1 | The bit-exactness overclaim, found by a passing mutation |
| Extra work | 1 | The lazy engine, the atomic gate and the reader check are all beyond what the plan described; each closes a defect the plan did not anticipate |

## Notes for Future Plans

**09-08's per-strip `LOAD` faces the same persistence question and now has an answer to copy**: keep
the path verbatim, validate it is absolute before it becomes a `juce::File`, refuse a file no reader
can open, and tell the user when a load fails.

**`juce::dsp::Convolution` allocates on the audio thread when it swaps a newly loaded IR in.** Ours
is bounded and once per load, and the allocation test asserts the bound rather than hiding it behind
a long warm-up. Any future stage built on the same class inherits this.
