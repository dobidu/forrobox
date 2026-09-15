---
phase: 05-sequencer-grid
plan: 02
subsystem: ui
tags: [juce, vst3, lock-free, atomics, playhead, 60fps]

requires:
  - phase: 02-sequencer-clock
    provides: the position-driven clock and its Span.startInSteps
  - phase: 03-voices-mix-bus
    provides: the lookahead that makes the output delay real
  - phase: 05-sequencer-grid
    provides: 05-01's SequencerGrid and its reserved pad strips
provides:
  - StepSnapshot / StepPublisher — one group-atomic audio-to-UI publication
  - displayPositionInSteps — the lookahead-corrected playhead position
  - Playhead — the continuous sweep
  - HitVisualiser — one channel's trigger LED and activity meter
  - outputDelaySamples() / isTransportStopped() — one definition each
affects: [05-03 STEPS and grid refresh, 05-04 overlay and dimming, 06 profile loading]

tech-stack:
  added: []
  patterns:
    - "A value that fits in one lock-free word needs no publication mechanism at all"
    - "One corrected timeline serves every view; do not re-derive it per consumer"
    - "Movers are components; static furniture is painted"
    - "Cull each piece of a paint against the clip, not just the region"

key-files:
  created:
    - src/StepSnapshot.h
    - src/StepSnapshot.cpp
    - src/Playhead.h
    - src/Playhead.cpp
    - src/HitVisualiser.h
    - src/HitVisualiser.cpp
  modified:
    - src/PluginProcessor.h
    - src/PluginProcessor.cpp
    - src/Chassis.h
    - src/Chassis.cpp
    - src/SequencerGrid.cpp
    - src/MixBus.h
    - scripts/verify-geometry.py

key-decisions:
  - "The snapshot is ONE 64-bit word because a velocity is 7 bits, not 8 — no double-buffer needed"
  - "The playhead is corrected for outputDelaySamples(); the per-step jitter deliberately is not"
  - "The LEDs fire when the corrected position reaches a step, not on a frame countdown"
  - "The playhead sweeps linearly in musical time; the prototype glides and waits under swing"

patterns-established:
  - "Pin a design constant to its source file; an unenrolled constant is a check that cannot fail"
  - "A test that renders in bulk before polling models an editor that does not exist"

duration: ~6h
completed: 2026-09-15
description: "One group-atomic publication and the three views that read it: playhead sweep, trigger LEDs, activity meters"
type: Summary
about: "Forró Box"
---

# Phase 5 Plan 02: The Groove Made Visible — Summary

**One 64-bit group-atomic publication replacing three loose atomics, and the three views that read
it: a continuous playhead, per-channel trigger LEDs, and activity meters.**

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: One publication, group-atomic, audio thread still allocation- and lock-free | **Pass** | One `uint64_t`; `publish` measured at 6-7 ns against the old path's 5-6. Tear test strengthened from 7/10 to 10/10 detection on a deliberately split publication |
| AC-2: The playhead sweeps continuously at the clock's position | **Pass** | Driven from `displayPositionInSteps`; passes through every pad centre at 16 and 32 steps; hidden when stopped. Five negative controls |
| AC-3: LEDs and meters follow triggers and respect mute/solo | **Pass** | `max` on trigger, `x0.82` per told frame, gated on `resolveChannelSettings()`. Five negative controls |
| AC-4: The strip head's LED box is reserved and cross-checked | **Pass** | 8 px diameter and 7 px gap against `css:275-281`; both mutations fail naming the constant |
| AC-5: Nothing regressed on three compilers | **Pass** | 3278/3278 on GCC, Clang and MSVC with `DISPLAY` unset; three verify scripts green; VST3 installed, hashes match |

## Skill Audit

| Expected | Invoked | Notes |
|----------|---------|-------|
| `/graphify` | ✓ | Consumed at planning; gave `setSteps → renderPads → layoutPlayhead`, which is why the sweep's geometry derives from the pad strip |
| `/code-review` | ✓ | After Task 1, as the plan gated. Seven findings, five fixed, two recorded as the next tasks' contract |
| `/simplify` | ✓ | During UNIFY. Four agents; 16 changes applied, 2 deferred with measurements |
| `/impeccable` | — | Optional, not invoked |

All required skills invoked.

## Task Commits

| Task | Commit | Type |
|------|--------|------|
| Task 1: group-atomic publication | `fb550d8`, `8a529b8` | feat, fix |
| `/code-review` findings answered | `987cdbc` | fix |
| Task 2: the playhead | `81e9347` | feat |
| Task 3: LEDs and activity meters | `fa67ee5` | feat |
| Checkpoint render | `43b417a` | test |
| `/simplify` findings | `dc48571` | refactor |
| `/simplify` performance findings | `5d2d571` | perf |

## Decisions Made

| Decision | Rationale |
|----------|-----------|
| One 64-bit word, not a double-buffer | A velocity is 0-127 by invariant, so 7 bits; 8 lanes + a 6-bit step = 62 bits. The plan specified a double-buffer on the assumption of 16 bytes — the domain's own range deleted the mechanism |
| The playhead is lookahead-corrected | Publication is at grid time, audio leaves `outputDelaySamples()` later — ~28% of a sixteenth at 132 BPM. `PluginProcessor.cpp` predicted this and named Phase 5 as owner; the plan did not |
| The per-step jitter is NOT corrected | Bipolar by design, so no single offset answers it, and a playhead that jittered would read as broken rather than as humanised |
| LEDs fire on position-crossing, not a frame countdown | The countdown re-derived in frames a correction already published in steps, and coupled LED timing to the poll rate |
| The sweep is linear in musical time | The prototype glides to each step centre and waits under swing. **Put to the user at the checkpoint and NOT answered** — see Issues |

## Deviations from Plan

### Summary

| Type | Count | Impact |
|------|-------|--------|
| Auto-fixed | 12 | Essential; 9 were defects in my own code found by review |
| Scope additions | 2 | The lookahead correction, and the clipped-repaint test |
| Deferred | 2 | Recorded in PROJECT.md with measurements |

### Auto-fixed

**From `/code-review` (7 findings, 5 fixed):** the lookahead correction shipped with **no test at all**
— `getDisplayPositionInSteps` had one occurrence in the tree, its own declaration, so inverting its
sign passed 3208/3208. The first replacement test *also* could not fail: it compared against the
emitted step index with a tolerance of one whole step, and the correction is 0.256 of a step,
confirmed by running the inverted mutant against it. Also: the position was never reset on any stop
path; the output delay was computed in two places and only one was the full sum; `getLastStepVelocity`
changed behaviour under a no-op refactor; a comment claimed velocities were clamped "by every setter"
when `State::lanes` is public with no setter.

**From `/simplify` (9 more):** `theme::accentFill`'s second parameter is the accent *intensity* and
applies the accent bar's `0.3 + 0.7i`, so passing the computed `0.4 + 0.6i` composed them into
`0.58 + 0.42i` — `Knob.h:77` warns against this by name and my comment claimed it was the accent
bar's law, the one law it must not be. `surface::wellShadow`'s fourth parameter is `depth` in pixels
and was passed an alpha, collapsing the gradient to 0.4 px of opaque black. `Playhead.h` was in no
header list, so **none of its nine constants were checked** — which is how `kTrailGap` shipped as 0
under a comment saying `right: 3px`. `kStoppedPosition` had zero production readers. Plus four test
defects: two assertions that were arithmetic consequences, a width check comparing 49 against 3, a
check reading the constant it tested, and a solo block that dropped the mute block's null check.

### Scope additions

**The lookahead correction** — `PluginProcessor.cpp`'s emitter had predicted the playhead would run
32 ms ahead of the audio and named Phase 5 as the owner; AC-2 said only "derived from the clock's
position".

**A clipped-repaint test** — every render check paints the whole chassis, so the per-piece culling
introduced by the performance pass would have been invisible if wrong.

### Deferred (recorded in PROJECT.md)

- **The LED and meter should be child Components.** Measured: culling took steady state from
  ~612 µs/frame (3.7% of a core) to ~220 µs (1.3%); the conversion would reach ~86 µs. Structural
- **`paintStrip` culls against a bounding box**, so two lit strips at opposite ends repaint all five
  (429 µs vs 152 µs adjacent). Largely subsumed by the above

## Issues Encountered

| Issue | Resolution |
|-------|------------|
| The swing divergence was put to the user and **not answered** | Approval came without comment on it, so the linear sweep stands *unjudged* rather than endorsed. Open for 05-03 or later |
| Two of my own measurement instruments could not fail | The meter fill compared against the wrong ground and measured 239 px at both 0.25 and 1.0; the tick counter counted the fill's gradient, 22 divisions where there are 15 |
| A mute/solo test rendered 40 blocks before polling once | Models an editor that does not exist. It passed only because the old design fired whatever step it last saw — with a real pattern that lights the wrong channels |

## Next Phase Readiness

**Ready:** the publication, the corrected timeline and all three views exist; 05-03 and 05-04 drop
into settled geometry and can read `isTransportStopped()` and `getDisplayPositionInSteps()`.

**Concerns:** the component conversion above; the swing divergence is unjudged; 05-03 still inherits
05-01's two gaps (STEPS 16/32, and the grid not refreshing on host recall).

**Blockers:** None.

---
*Built with PAUL Framework v1.4*
*Phase: 05-sequencer-grid, Plan: 02*
*Completed: 2026-09-15*
