---
phase: 07-midi-out
plan: 03
subsystem: midi
tags: [juce, midi, audio-thread, lock-free, gm-percussion, humanisation]

requires:
  - phase: 07-01
    provides: the GM percussion map, hoisted here into its own header
  - phase: 03-02
    provides: the humanisation seam at VoiceEngine::playVelocity
provides:
  - live MIDI out on the plugin's bus, emitting the humanised performance
  - src/GmPercussion.h — the GM map, the percussion channel and the velocity clamp
  - ids::midiGate — a selectable note-off gate, FIXED or STEP
  - a fixed-capacity, allocation-free pending-MIDI queue with a retrigger rule
affects: [08-polish]

tech-stack:
  added: []
  patterns:
    - "A retrigger of a sounding note extends its gate; it is never cut short by the stale note-off"
    - "A derived block fact is its own member, not a field of the one-handover Settings struct"
    - "An allocation assertion whose warm-up pre-grows the buffer is measuring nothing"

key-files:
  created: [src/GmPercussion.h]
  modified: [src/VoiceEngine.h, src/VoiceEngine.cpp, src/PluginProcessor.h, src/PluginProcessor.cpp, src/ParameterIDs.h, src/MidiExport.cpp, tests/VoiceTest.cpp, tests/StateRoundTripTest.cpp]

key-decisions:
  - "The seam is playVelocity — every humanised hit and every ghost already passes through it"
  - "The engine owns the queue: live MIDI is per-lane per-hit, so it fails the MixBus sibling test"
  - "midi_gate is an APVTS parameter, not ValueTree state — output_mode is the precedent"
  - "Live MIDI follows `audible` (mute AND solo); the file export follows mute only"
  - "sameId moved to ParameterIDs.h — a table of eight notes must not include the synth engine"

patterns-established:
  - "Two lanes sharing a GM note is a spec fact, so the queue must handle retrigger, not assume distinctness"
  - "Assert what matters (nothing left sounding), not a proxy (equal on/off counts)"

duration: ~270min
started: 2026-09-21T05:30:00-03:00
completed: 2026-09-21T09:40:00-03:00
description: "Live MIDI out emitting the humanised performance — and the reviews found a note-truncation bug guaranteed by the spec, a drop counter that could only ever read 2, and an allocation assertion that could not see the path it guarded"
type: Summary
about: "Forró Box"
---

# Phase 7 Plan 3: Live MIDI out — Summary

**The groove drives other instruments. Every hit the plugin plays — swung, jittered, velocity-varied,
ghosts included — leaves as a MIDI note on the plugin's bus, with no allocation and no lock on the
audio thread. The reviews then found that two lanes sharing GM note 36 truncated each other, that
the drop counter could never report more than 2, and that the allocation assertion guarding all of
it was structurally blind.**

## Performance

| Metric | Value |
|--------|-------|
| Duration | ~270 min |
| Tasks | 4 of 4 (3 auto + 1 checkpoint) |
| Qualify | 3 PASS |
| Checks | 3884 → **3906** (+22) |
| Compilers | GCC 13, Clang 18, MSVC 2022 — 0 warnings |
| Cross-checks | 5 green; `verify-midi` run before AND after the GM hoist |
| Exported bytes | **identical** across the hoist, diffed directly |

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: every audible hit leaves as a note, at the sample it sounds | **Pass** | Verified in Ableton and by the suite |
| AC-2: swing, jitter, velocity variation and ghosts all reach the output | **Pass** | CACHAÇA 0 vs 100 produces different offsets; ghosts assert the spec's velocity window |
| AC-3: a note past the block end is emitted in the block containing it | **Pass** | Identical note sequence at 32/64/128/512/1024 |
| AC-4: the gate is selectable and neither mode strands a note | **Pass** | After a stop, no note number is left sounding, in both modes |
| AC-5: the audio thread allocates nothing and takes no lock | **Pass, with a stated exception** | Steady state allocates nothing; a COLD `MidiBuffer` allocates a bounded handful as it grows, which every hosted format pre-sizes away and only Standalone pays, once. Now measured rather than assumed |

## Accomplishments

- **The last inert declaration is gone.** The plugin has declared `NEEDS_MIDI_OUTPUT` since Phase 1
  while `processBlock` opened with `midi.clear()` and wrote nothing back.
- **The humanisation reaches the MIDI**, which is the whole reason Phase 7 planning chose the
  performance over the grid — a doubled instrument drifts *with* this plugin, not against it.
- **The GM map has one home**, and the hoist was proved inert by diffing the exported bytes rather
  than by trusting a green gate.

## Files Created/Modified

| File | Change | Purpose |
|------|--------|---------|
| `src/GmPercussion.h` | Created | The GM map + `static_assert`, the percussion channel, the velocity clamp |
| `src/VoiceEngine.h/.cpp` | Modified | The pending queue, the seam, the retrigger rule, the gate |
| `src/PluginProcessor.h/.cpp` | Modified | The 46th parameter, the drain, the step length, the stop flush |
| `src/ParameterIDs.h` | Modified | `midiGate`, `MidiGate` enum, the two gate constants, `sameId` |
| `src/MidiExport.cpp` | Modified | Its `kGateFraction` now IS `ids::kStepMidiGateFraction` |
| `tests/VoiceTest.cpp` | Modified | Six live-MIDI cases and the cold-buffer allocation measurement |
| `tests/StateRoundTripTest.cpp` | Modified | Eight count assertions, 45 → 46 |

## Decisions Made

| Decision | Rationale | Impact |
|----------|-----------|--------|
| The seam is `playVelocity` | Its own comment calls it "the one place a normalised velocity becomes a voice" — every hit and every ghost passes through carrying the jittered offset. A trigger publication would be a new abstraction with one producer and two consumers, one of which is a direct call | Confirmed by `/simplify`: the engine does NOT publish this data. `StepPublisher` is the processor's, fires at GRID time, and carries the PROGRAMMED velocities with ghosts deliberately absent |
| The engine owns the queue | Weighed against the recorded MixBus decision, whose criterion is "one global stage with no per-voice anything". Live MIDI is per-lane, per-hit, with values that exist only inside `scheduleStep` | It fails the sibling test in the direction of staying put |
| `midi_gate` is an APVTS parameter | PROJECT.md's "state, not parameters" rule is scoped by its own reason — "rather than 160+ automation lanes" — which is about the grid. `output_mode` is the precedent | ValueTree would cost an audio-thread handover to buy less: no host visibility, no automation |
| `sameId` moved to `ParameterIDs.h` | A generic constexpr strcmp with nothing to do with voices. Leaving it forced a header of eight MIDI notes to include the whole synth engine, and gave `VoiceEngine.cpp` a header that includes it back | Three existing callers unaffected |

## Deviations from Plan

### Summary

| Type | Count | Impact |
|------|-------|--------|
| `/simplify` fixes | 17 | Two were correctness bugs; one was a check that could not fail |
| Findings declined, with reasons | 4 | Three measured as noise; one is right but 13 files wide |
| Deferred | 2 | Logged to PROJECT.md |

### Two lanes share GM note 36, and they were truncating each other

The worst finding, and it was **guaranteed by the spec rather than unlucky**. `PLANNING.md:815` and
`:819` both say 36 — zabumba and BB, deliberately, which 07-01 wrote down. When both fire on one
step the queue held `on(36)@t1, off(36)@t1+gate, on(36)@t2, off(36)@t2+gate` with
`t1 < t2 < t1+gate`, so the FIRST note-off arrived after the SECOND note-on and cut it short. At
132 BPM a step is 114 ms, the FIXED gate is 40 ms and CACHAÇA's jitter is ±22 ms — the two land
inside one gate routinely. A retrigger now drops the stale note-off and extends the gate, which is
standard drum behaviour.

**And the check I had written could not see it.** `testLiveMidiNeverStrandsANote` asserted that
note-ons and note-offs balanced — which only held *because* the truncation was emitting one off per
on. Fixing the bug made my own check fail. It now asserts the property that matters: after a stop,
no note number is left in the ON state. A retrigger is legitimately two ons and one off.

### The drop counter could only ever read 2

`atomicMax (droppedMidi, pendingMidiCount + 2 - kMaxPendingMidi)` was wrong twice over.
`Atomics.h` says `atomicMax` is for a running PEAK a reader clears; the established spelling for a
monotonic count is `voicesDropped.fetch_add`, three lines away in the same file. And because the
queue only ever moves in pairs, the argument was the constant 2 — so a "count" under a doc comment
calling it a count could never exceed 2 however many notes were lost. A boolean wearing a number.

### `reset()` did not clear the queue

`VoiceEngine::reset()` zeroes the voices, the RNG, the step counter and **four sibling counters** —
and did not touch `pendingMidi` or `droppedMidi`. Since `prepare()` calls it, a **sample-rate change
left events queued whose offsets were measured at the old rate**. The header also claimed "a stop, a
release and a sample-rate change all go through here"; only the two stop paths did.

### The allocation assertion was structurally blind

`juce::MidiBuffer::addEvent` grows a `juce::Array`, and `clear()` is `clearQuick()` — it never
frees. So the existing test's 16 warm-up blocks took that array to steady-state capacity, and the
measured 2000-block window **could never observe a MIDI allocation**. The assertion was true and
said nothing about `drainMidi`. Now measured on a cold buffer with a bounded, documented non-zero
count — because asserting zero there would be asserting something false. Every hosted format
pre-sizes to 2048 bytes; only Standalone pays it, once.

### Three more of my own claims were false

`kStepMidiGateFraction` was documented as "named once so the two cannot drift" while
`MidiExport.cpp`'s `kGateFraction = 0.8` stayed — two names under a claim there was one, the exact
failure `ids::channelInfos` states its rule about, in the same commit. `noteForLane`'s comment said
the lane "has already travelled through a voice pool" when the tap is the first statement of
`playVelocity`. And the ghost-velocity check accepted 20..45 under a comment computing 25..41 — a
silently widened window, now computed from the spec constants.

### I inserted straight through a stated ordering law

The five live-MIDI cases ran *above* `testMeasurementInstruments()`, whose own line reads "The
instruments first: a broken one makes everything after it meaningless." Moved to last, where they
belong — they drive whole blocks through the processor, so anything broken above would surface here
as a confusing MIDI failure rather than at its cause.

### Findings declined, with reasons

| Finding | Why not |
|---------|---------|
| Make `ids::lanes` a row struct carrying the GM note | The right depth, and stated as such — but ~20 element-level uses across 13 files. Deferred, not dismissed |
| Per-lane note-off countdown instead of queued pairs | Measured saving 153 ns/block = 0.006% of budget, and it forfeits the "queued together so a later early-out cannot strand a note" invariant |
| Hoist `std::lround` out of `midiGateSamples` | Measured 0.07 ns/hit, ~1 ns/block, and it splits the gate's definition across two functions |
| Drop the redundant `juce_audio_basics` include | `juce::MidiBuffer` is used directly in that header. Relying on a transitive pull through `juce_dsp` is the fragility include-what-you-use exists to avoid |

## Issues Encountered

| Issue | Resolution |
|-------|------------|
| The charset gate caught an em dash inside `juce::String(const char*)` in my own new check | Wrapped in `fbtest::utf8`. It also failed the Clang build, since the gate is a build dependency — and the Clang suite then reported green from a stale binary, which was discarded |
| A batch of edits aborted mid-way on a missed anchor, after an earlier edit in the same script had already been written | Re-applied the batch; the lesson is that a multi-edit script must write once at the end, which this one did not |

## Deferred (logged to PROJECT.md)

- **`ids::lanes` should carry the GM note.** The table is still parallel to `ids::lanes` and held in
  step by a `static_assert` — detectable, not impossible, which is the weaker half of the rule
  `channelInfos` states. The project has already made this exact fix twice (`channelInfos`,
  `kitPieces`). ~20 sites across 13 files.
- **Two opposite documented policies for a bad choice index.** `resolveChannelSettings` clamps;
  `stepsForChoiceIndex` argues in its own comment that clamping is wrong ("the default is the
  conservative wrong answer"). Harmless at two modes, but the reasoning now exists in both
  directions, five hundred lines apart.

## Next Phase Readiness

**Ready:** Phase 7 is complete. Phase 8 inherits a plugin whose groove leaves by file, by drag and
by live MIDI.

**Concerns:** The fresh-instance bug the user found in a real host during this plan's checkpoint —
a new instance claims CAMPINA and plays silence — is recorded and still unfixed. It fails
PROJECT.md's own "usable groove in under 30 s, zero config" metric and should open Phase 8.

**Blockers:** None.

---
*Built with PAUL Framework v1.4*
*Phase: 07-midi-out, Plan: 03*
*Completed: 2026-09-21*
