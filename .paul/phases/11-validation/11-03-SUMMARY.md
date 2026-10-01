---
phase: 11-validation
plan: 03
subsystem: audio
tags: [bypass, latency, processBlockBypassed, midi, convolver, limiter, pluginval]

requires:
  - phase: 11-validation
    provides: "11-01's gate, which measured the 500 assertions; 11-02's green MSVC run"
provides:
  - "processBlockBypassed: silence, every sent note closed once, input MIDI passed through"
  - "a clean restart on un-bypass: voices, pending MIDI, convolver tail and limiter from rest; the plugin's own clock frozen; SYNC re-locks to the host"
  - "Convolver::reset audio-thread safe, gated on the engine having been built"
  - "VoiceEngine::silence: stops voices without rewinding humanisation or zeroing the overflow counters"
  - "publishHostState: one publisher of the host's tempo/transport for both paths"
affects: [11-04 (the gate's last finding), Phase 13 multi-instance (host state publication)]

key-files:
  modified: [src/PluginProcessor.h, src/PluginProcessor.cpp, src/Convolver.h, src/Convolver.cpp, src/VoiceEngine.h, src/VoiceEngine.cpp, tests/VoiceTest.cpp]

key-decisions:
  - "Freeze + clean restart; close notes + pass input (user, at planning)"
  - "silence(), not reset(): a bypass is a musical gesture, not a device restart"
  - "Convolution::reset's destroyPreviousEngine accepted: it is JUCE's own audio-thread path (installNewEngine)"

duration: ~1 session
completed: 2026-09-30
description: "A bypassed Forró Box is silent, hangs no note, keeps the header live and comes back clean. pluginval Debug's 500 bypass-latency assertions go to 0, so the gate is red only on 11-04's leak"
type: Summary
about: "Forró Box"
---

# Phase 11 Plan 03: Bypass

**pluginval Debug's `juce_AudioProcessor.cpp:599` count went from 500 to 0.** A bypassed instance outputs exact silence, sends a note-off for every note it had sent, passes host MIDI through untouched, keeps the header's host tempo and transport live, and restarts from rest. 4999/4999 on GCC 13, Clang 18 and MSVC 2022, and Windows pluginval PASS.

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: bypassed means silent; :599 gone | **Pass** | Every channel of every bus is exactly 0, even when handed a non-silent buffer, in both gate modes. pluginval Debug :599 is 500 → 0. No lock or allocation is added on the path (/code-review confirmed) |
| AC-2: no hung note; input passes through | **Pass** | No note is left ON after the first bypassed block, in both gate modes. The second bypassed block emits nothing. Three incoming notes come out unchanged on blocks 1 and 2 |
| AC-3: un-bypass restarts clean | **Pass** | Voice tail: a control without bypass rings, and after a bypass the block is silent. **Added by review:** the convolver tail (control rings with the voices silenced by hand, gone after a bypass) and the limiter (a hard-driven, bypassed, restarted rig is sample-identical to a fresh one). Clock: with SYNC off it resumes at the bypassed step; with SYNC on it re-locks to the host (added) |
| AC-4: never-bypassed runs untouched | **Pass** | The suite is unchanged on three compilers. The plan's own "two fresh rigs are identical" check could never fail, so it was replaced by the limiter check above, which can |

## /code-review (required for processor code): 9 findings

| # | Finding | Outcome |
|---|---------|---------|
| 1 | `resetFromAudioThread` was gated on `engineReady`, which falls when the IR is cleared, so a stale tail survived to the next load | **Fixed:** new `engineBuilt` atomic (release on build, acquire in reset) |
| 9 | `Convolver::reset()` was an ungated near-duplicate with no callers | **Fixed:** the gate moved into `reset()`; `resetFromAudioThread` deleted |
| 5 | `engine.reset()` zeroed `droppedMidi`, `voicesStolen` and `peakActiveVoices` | **Fixed:** `VoiceEngine::silence()`; counter-survival check added |
| 6 | `engine.reset()` rewound the RNG and step counter while the clock stayed frozen | **Fixed:** the same `silence()` |
| 2 | Bypass froze `hostBpm` and `hostTransportRolling`, reopening a fixed stale-header bug | **Fixed:** `publishHostState()`, shared by both paths; tested with `FakePlayHead` |
| 3 | "Frozen" is false under SYNC (planBlock re-anchors) | **Fixed:** comments corrected; sync re-lock test added |
| 7 | The restart-vs-fresh test could not fail | **Fixed:** replaced (AC-4 above) |
| 8 | Convolver and mix bus resets were untested | **Fixed:** convolver-tail and limiter checks; both mutations now rejected |
| 4 | `Convolution::reset` → `destroyPreviousEngine` can free on the audio thread if JUCE's queue is full | **Accepted, not changed:** `installNewEngine` takes the same path during every IR load while playing. The command is a `FixedSizeFunction` and nothing is allocated. The restart is a second route into JUCE's own accepted path, not a new class of risk |

## Mutations

**Rejected (11):**
- no flush;
- no restart;
- swallowing input;
- no buffer clear;
- position reset on restart;
- no convolver reset;
- no mix bus reset;
- `reset()` instead of `silence()`;
- no host publish while bypassed;
- a wrong path or callback (via the 11-02 checks, still green).

**Survived (2), recorded honestly:**
- **"Flush on every bypassed block":** an *equivalent* mutant. After the first flush nothing is sounding, so later flushes emit nothing. The "only once" check checks the property, not the guard.
- **"Gate on `engineReady`":** it needs an IR cleared during bypass and then reloaded *asynchronously* (JUCE's background loader, with no prepare). Offline tests can't sequence that, and every reload in the suite goes through `prepareToPlay`, which resets the engine anyway. The gate is argued, not tested.

## Found on the way

- **The "current step" is the HEARD step.** It trails scheduling by the 32 ms lookahead, about 3 blocks. A check read one block after un-bypass sees the pre-bypass step whatever the clock did. My first sync check failed on exactly that, and the first frozen check passed for the same reason. Both now read after 8 blocks.

## Deviations

| Type | Count | Notes |
|------|-------|-------|
| Files beyond the plan | 4 | `Convolver.h/.cpp` (the audio-thread-safe reset) and `VoiceEngine.h/.cpp` (`silence()`), both required by the review's findings |
| Test-only accessor | 1 | `getVoiceEngineForTest()`, following the existing `...ForTest` precedent |
| Plan check replaced | 1 | The unfalsifiable "fresh rigs" check, replaced by the limiter check |

## Next Phase Readiness

**Ready:** 11-04. The gate's Debug verdict now lists only the Typeface leak. **Concerns:** none new. **Blockers:** none.

---
*Phase: 11-validation, Plan: 03 · Completed: 2026-09-30*
