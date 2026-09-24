---
phase: 09-content-convolution
plan: 08
subsystem: audio
tags: [samples, loading, drag-and-drop, real-time, ownership]

requires:
  - phase: 09-content-convolution
    plan: 07
    provides: "the load-path hazards this plan inherited, and their answers"
provides:
  - "UserSamples — one loaded sample per channel, never-reuse ownership"
  - "LOAD wired on all five strips, by browser and by drag-and-drop"
  - "Per-channel sample paths, persisted, validated and degrading"

affects: [09-09]

tech-stack:
  added: []
  patterns:
    - "Free only at a moment when no reader can exist, and prove it"
    - "A rule that already exists is reused, not restated"

key-files:
  created: [src/UserSamples.h, src/UserSamples.cpp]
  modified: [src/VoiceEngine.h, src/VoiceEngine.cpp, src/PluginProcessor.h,
             src/PluginProcessor.cpp, src/ForroBoxState.h, src/ForroBoxState.cpp,
             src/ParameterIDs.h, src/Chassis.h, src/Chassis.cpp, CMakeLists.txt,
             tests/VoiceTest.cpp, tests/UiTest.cpp]

key-decisions:
  - "The bateria strip targets caixa, via writeLaneForRow rather than a copy of it"
  - "Buffers are never reused; retired ones are freed only in prepare"
  - "No new parameter — a sample is state, not automation"

patterns-established:
  - "An argument is not a proof, and a lifetime needs the second"

duration: ~3h
started: 2026-09-24T13:00:00-03:00
completed: 2026-09-24T17:00:00-03:00
description: "Per-strip LOAD — a user sample per channel, by browser or drag-and-drop"
type: Summary
about: "Forró Box"
---

# Phase 9 Plan 08: per-strip `LOAD` — Summary

**The last stub on the chassis. Each strip's `LOAD` opens a file browser or accepts a file dragged
onto it, the loaded sample replaces that channel's voice, its name appears on the strip, and the
path persists. A sample that has moved costs a sound, not a session.**

## Performance

| Metric | Value |
|--------|-------|
| Tasks | 2 of 2 |
| Checks | 4877 → **4931** |
| Mutations | 7, each confirmed applied on disk before its result was read |
| `/code-review` | once after Task 2 — **10 findings, 1 HIGH + 1 MEDIUM-HIGH, all fixed** |

## Verification

| | Result |
|---|---|
| GCC 13 / Clang 18 | **4931 / 4931**, real exit 0 |
| MSVC 2022 | **4931 / 4931**, read from the build log |
| Cross-checks | all six, run together |
| Warnings from our sources | zero |

## The lifetime argument was wrong, and both its premises were false

`UserSamples` began as a double buffer per channel with an argument attached: a voice could not
still be reading the retired slot because *"a sample voice is bounded by its file's length and by
DECAY's envelope, which is seconds at most"* and *"two loads require two trips through a file
chooser"*.

**Neither holds.** `decayFraction` reaches 1.0 at DECAY 100, so a voice plays the **whole** file —
up to the thirty-second cap. And `restoreUserSamples` calls `load` unconditionally from
`setStateInformation`, which hosts run on the message thread **while audio is playing** for every
preset click, undo and project reload. Two loads therefore need no user interaction at all, and the
third one's `setSize` frees the block a live voice is reading.

Nothing is reused now. A load allocates a **new** buffer and publishes a pointer to it; the previous
one is retired and kept alive. Retired buffers are freed only in `prepare`, which the processor
calls after `VoiceEngine::prepare` has reset every voice — so at that moment no voice can hold a
pointer into any of them. **That is a proof rather than an argument**, which is the distinction the
first version got wrong.

## The plan said caixa; the code replaced the whole kit

09-08's plan stated *"The bateria strip's `LOAD` targets CAIXA"* and explicitly rejected the
alternative — *"one sample across all four would make BB, CX, HH and TOM the same sound"*. The
implementation then gated on `channelForLane`, which maps all four kit lanes to channel 4. Dropping
a snare on BATERIA made the kick, hi-hat and tom that snare.

The fix reuses `writeLaneForRow`, which **already is** the rule: 05-01 made the composite BATERIA
row write caixa, "the backbeat; deep edits live in the kit view". The strip's `LOAD` follows the
strip's own editing rule rather than a second copy of it.

## Two fixes that passed their mutations, and what that meant

After fixing the bateria gate and the sample-length cap, both mutations **passed** — neither fix had
a behavioural test. The bateria test asserted the rule's *shape*; the cap had none at all.

Both now render and measure. Removing the lane filter fails with *"bb keeps its built-in voice",
"hh…", "tom…"*; measuring the cap at the host rate fails with *1920000 vs 1440000*.

## And seven more findings

- **The cap was measured in the wrong rate** — `rate * 30.0` (host-rate samples) compared against
  `lengthInSamples` (file-rate). A 22 kHz file on a 96 kHz host admitted over two minutes and
  produced exactly the hundreds of megabytes the comment said it prevented.
- **`restoreUserSamples` re-decoded every file on every `prepareToPlay`**, with no unchanged-path
  skip — and carried `JUCE_ASSERT_MESSAGE_THREAD` while the standalone build reaches
  `prepareToPlay` from the audio device thread. So it asserted on every debug run and did blocking
  disk I/O inside the device-start callback.
- **The drag highlight did not exist.** `dragTargetStrip` was written by every drag callback and
  read by nothing, so the affordance the code's own comment promised was never drawn — and every
  mouse move during a drag repainted the whole chassis for no visual change.
- **The UI test launched a real file chooser it never dismissed**, leaving an orphaned dialog for
  the leak detector; its assertion was `check (true, …)`, which passed whether or not the button was
  wired. It now asserts the handler exists on all five strips without opening anything.
- **The chooser guard's comment said "per strip index"** for a single `unique_ptr` that was never
  per strip.
- **A "LOAD is still a stub" comment** sat directly above the line wiring its handler.
- **`UserSample::sourceRate`** was written and never read.

The review also corrected a rationale of mine: `paintSampleSlot`'s cache is worth having because a
paint should not take the state lock and build a `juce::String` per strip — **not** because of
priority inversion, since `stateLock` is a `CriticalSection` the audio thread never takes.

## Notes for Future Plans

**09-09 is the last plan in the phase** — MIDI input, the one gap that is specified nowhere. It
will need the behaviour decided explicitly: the note-to-lane map, whether notes layer with the
sequencer or bypass it, and whether they follow mute and solo the way live MIDI out does.

**The ownership pattern here is the one to copy** for anything else the audio thread reads and the
message thread replaces: never reuse, retire, and free only where no reader can exist.
