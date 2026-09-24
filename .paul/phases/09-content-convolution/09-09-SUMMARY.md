---
phase: 09-content-convolution
plan: 09
subsystem: audio
tags: [midi, input, gm, real-time, latency]

requires:
  - phase: 07-midi-export
    plan: 03
    provides: "the live MIDI tap playVelocity carries, and the gate rule input inherits"
  - phase: 03-voices-mix-bus
    plan: 02
    provides: "the lookahead every trigger is delayed by, which input now pays too"
provides:
  - "gm::laneForNote — the inverse of the map the plugin emits, derived from the same table"
  - "VoiceEngine::soundVelocity — the schedule half of playVelocity, without the MIDI-out queue"
  - "VoiceEngine::noteOn — an incoming note, gated, lookahead-aligned, not humanised"
  - "setParametersResolvedForTest — a seam for the degraded audio path"

affects: []

tech-stack:
  added: []
  patterns:
    - "Read raw MIDI bytes on the audio thread; juce::MidiMessage heap-allocates above 8 bytes"
    - "A path that skips the lookahead is early by exactly what the plugin promised the host"

key-files:
  created: []
  modified: [src/GmPercussion.h, src/VoiceEngine.h, src/VoiceEngine.cpp,
             src/PluginProcessor.h, src/PluginProcessor.cpp, tests/VoiceTest.cpp, README.md]

key-decisions:
  - "The note map is the emitted one inverted, so the plugin can play its own output"
  - "Input layers with the sequencer, follows the mute/solo gate, and is not humanised"
  - "Input is delayed by the lookahead — alignment beats live feel"
  - "Omni input: every channel accepted, though the plugin emits only on 10"

duration: ~2h
started: 2026-09-24T13:30:00-03:00
completed: 2026-09-24T16:00:00-03:00
description: "MIDI input — the instrument answers notes instead of discarding them"
type: Summary
about: "Forró Box"
---

# Phase 9 Plan 09: MIDI input — Summary

**`processBlock` reads the buffer before it clears it. A note on the GM percussion map sounds its
lane, at the velocity sent, layered over whatever the sequencer is doing, gated by the same mute and
solo the grid obeys — and never echoed back to the host that sent it.**

This is the last plan of Phase 9 and of the v0.1 milestone.

## Performance

| Metric | Value |
|--------|-------|
| Tasks | 2 of 2 |
| Checks | 4964 → **4974** |
| Mutations | 8, each confirmed applied on disk before its result was read |
| `/code-review` | once after Task 2 — **7 findings, 0 HIGH, all 7 addressed** |
| Parameters | 47, unchanged — a played note is not automation |

## Acceptance Criteria Results

| Criterion | Status | Evidence |
|-----------|--------|----------|
| AC-1: a note sounds its lane | **Pass** | The map round-trips for every lane, note 36 resolves to zabumba, note 7 is refused and silent |
| AC-2: it layers rather than replacing | **Pass** | By ADDITIVITY: `(pattern + note) − pattern − note` is zero to 1e-6, which proves both halves at once |
| AC-3: it respects the gate | **Pass** | A muted channel's note is silent; mutation removing the gate fails the check |
| AC-4: it does not echo | **Pass** | 40 blocks scanned, no note-on of the watched note; mutation routing input through `playVelocity` fails it |
| AC-5: the audio thread stays clean | **Pass** | Zero allocations across 2000 blocks each CARRYING notes; offset clamped, mutation removing the clamp fails |

## Verification

| | Result |
|---|---|
| GCC 13 / Clang 18 | **4974 / 4974**, real build exit 0, real run exit 0 |
| MSVC 2022 | **4974 / 4974** — read from the build log, because the process hangs after printing (09-06) |
| Cross-checks | all six, run together |
| Warnings from our sources | zero |

## Decisions Made

Every behaviour here is **invented**: `PLANNING.md` specifies MIDI input nowhere. Each follows a
precedent already in the codebase, and each is written where it can be overturned.

| Decision | Rationale | Impact |
|----------|-----------|--------|
| The map is `noteForLane` inverted | Record this plugin's MIDI output, play it back in, and the same lanes fire. Any other map would leave the instrument unable to play its own output | `laneForNote` scans `laneNotes`; note 36 has two owners and resolves to the first, which is a choice, not a derivation |
| It layers with the sequencer | A drum machine you play along with. An incoming note stopping the groove is what nothing asks for | Proved by additivity rather than asserted |
| It follows the mute/solo gate | 07-03 settled the analogous output question: a mute is a mix decision, not an input filter | Gated at schedule, so a muted channel consumes no voices |
| It is NOT humanised | `CACHAÇA` jitters a hit around its STEP, and a played note has no step | No jitter, no ghost notes — but see the lookahead below, which is not the jitter |
| It IS delayed by the lookahead | See the review section. Without it an input note is early by exactly the latency the plugin declared | A note played live is heard 32 ms after the key — the cost of being aligned with everything else |
| Omni, not channel 10 only | The plugin emits on `gm::kPercussionChannel`, so filtering would be symmetric — but a controller on channel 1, which is most of them out of the box, would do nothing and give the user no way to find out why | Accepted on every channel, and the asymmetry is written down where it happens |

## What the tests caught, and what they could not

**A test that could not fail, found by a mutation that should have died.** Routing input through
`playVelocity` causes an echo, and `testMidiInputLayersAndDoesNotEcho` did not notice. Temporary
instrumentation — a second loop identical to the test's, placed immediately after it — DID see the
echoed note-on. The difference was not the code but the block: `setPlaying` leaves `resetPending`
raised, and the first block to see it calls `flushAllNotesOff`, which ends `pendingMidiCount = 0`.
**The transport start discarded the echo before it could be drained**, so the test passed for a
reason that had nothing to do with its claim. The note now arrives at block 5 and the mutation dies.

It is a MIDI-queue effect only: the same reset deliberately does not clear voices, so a note arriving
in the first block still sounds.

**And two of my own tests failed on correct code** before that, both for reasons of the same family:
the echo test was contaminated by sequencer notes queued before the grid was cleared, and the offset
test ignored the engine's delay and expected sound in a block where nothing sounds either way.

## What `/code-review` found

Seven findings, none HIGH. The two that mattered are timing and a gate that defaulted open.

### An input note was early by exactly the latency the plugin declares

`scheduleStep` adds `lookaheadSamples` to every grid hit (`VoiceEngine.cpp:176`) and
`updateReportedLatency` declares that delay to the host, which shifts the plugin's output earlier to
compensate. `noteOn` passed the raw block offset — so an input note landed **1536 samples, 32 ms,
ahead of the sequencer it is meant to layer with, and ahead of everything else on the timeline**. A
MIDI clip doubling the grid would have flammed against it.

The plan's "NOT humanised" decision was the source of the error: `CACHAÇA`'s *jitter* is keyed on a
step and rightly does not apply, but the *lookahead* is not the jitter — it is the fixed delay the
jitter is representable inside. The two were conflated. Input now pays it, and a new test asserts the
onset lands at exactly the reported latency, where the sequencer's own step 0 lands.

The cost is stated rather than hidden: a note played live is heard 32 ms after the key. The
alternative was a path early by precisely the amount the plugin promised the host it was late.

### The gate defaulted OPEN when the parameters did not resolve

The note loop sat beside the `parametersResolved` guard rather than inside it. When that flag is
false — a renamed parameter ID, which `getRawParameterValue` answers with null and which Release
ships past its `jassert` — `beginBlock` was skipped while the loop still ran, leaving `blockSettings`
default-constructed. **`ChannelSettings::audible` defaults to `true`.** Every other reader on this
path degrades to silence; this one would have degraded to "every incoming note sounds, at a default
mix, while the sequencer is mute". The loop is now inside the guard, and a test seam
(`setParametersResolvedForTest`) makes the check able to fail.

### `getMessage()` allocates on the audio thread — and this suite cannot see it

`juce::MidiMessage` stores up to 8 bytes inline and heap-allocates above that, so constructing one
from a SysEx, an MTC full-frame or an MPE configuration message mallocs inside `processBlock` — while
a comment three lines up claimed nothing was allocated. The loop now reads `metadata.data` directly
and never builds a `MidiMessage`.

**This fix has no failing mutation, and that is stated rather than papered over.** The counter
replaces global `operator new` (`ClockTest.cpp:37`); `MidiMessage::allocateSpace` calls `std::malloc`
directly (`juce_MidiMessage.cpp:353`). Restoring `getMessage()` is a mutation this harness does not
detect. The defect is real and the fix is right; the proof is by reading JUCE, not by measurement.

### AC-2 had no test at all

`testMidiInputLayersAndDoesNotEcho` says "layers" in its name and then **blanks the grid** before
sending anything. It asserted the echo property and nothing else. Layering is now proved by
additivity: with the output stage pinned transparent the engine's summation is linear, so
`(pattern + note) − pattern − note == 0` holds exactly if and only if the note added its voice and
changed nothing else — a note that stole the sequencer's voices, shifted its timing or re-seeded its
humanisation would all break it. Measured worst difference: below 1e-6.

### Three more

- **AC-5's allocation clause had no test.** Every existing window handed `processBlock` an empty
  buffer, so the input loop ran zero times inside them. A window carrying a dense note stream now
  reports zero across 2000 blocks — and a probe allocation in that loop makes it read 16000, so the
  window is known to be live rather than assumed to be.
- **The offset test's reasoning was false when written.** It claimed the engine's delay put the note
  outside block 0; at the time `noteOn` added no delay, so the window discriminated by a single
  sample for a reason nobody had written down. The lookahead fix is what made the sentence true, and
  the comment now says so.
- **A doc comment was stacked on the wrong declaration for the sixth plan running** — `noteOn` was
  inserted between `setUserSamples`'s comment and `setUserSamples`. A second, pre-existing orphan in
  the same region (`prepare`'s comment, separated from `prepare` since an earlier plan) was fixed
  alongside it.

## Deviations from Plan

| Type | Count | Impact |
|------|-------|--------|
| Behaviour added beyond the plan | 1 | The lookahead on input. The plan's decisions did not mention it and its absence was a real timing defect |
| Tests added beyond the plan | 3 | Alignment, layering by additivity, and the degraded path |
| Test seam added | 1 | `setParametersResolvedForTest`, on 09-07's `setLatencyOverrideForTest` precedent |
| Documented rather than changed | 1 | Omni input. The asymmetry with the emitted channel is deliberate and now stated |
| Fix without a failing mutation | 1 | The raw-byte read. Stated openly, with the reason the harness cannot observe it |

## Notes for Future Plans

**Any future path that triggers a voice must add `lookaheadSamples`.** There are now three sources —
the grid, the ghost roll and MIDI input — and the third got it wrong. A trigger that skips it is not
merely early: it is early by exactly the figure the plugin reports to the host, so the host's own
compensation doubles the error rather than hiding it.

**The allocation counter does not see `std::malloc`.** It replaces `operator new` only, and JUCE uses
`std::malloc` directly in at least `MidiMessage`. A future claim of "allocation-free" on a path
touching JUCE's own containers should say which of the two it has actually measured.
