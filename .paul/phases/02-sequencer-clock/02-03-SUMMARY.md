---
phase: 02-sequencer-clock
plan: 03
type: Summary
about: "Forró Box"
description: "SYNC made real: the clock is driven by a musical position range, so host tempo, transport, bar alignment, loops and jumps are one code path rather than a second mode."
completed: 2026-09-07
---

# 02-03 Summary: Host sync

## What Was Built

`SYNC` works. With it on, the processor derives the block's musical span from `AudioPlayHead` instead
of from the `bpm` parameter: step 0 lands on the host's bar, host tempo sets the spacing, and the
host's transport decides whether anything plays.

There is **no second mode**. `Clock::advance` takes "this block starts at position P and covers
`stepsPerSample` steps per sample" rather than "advance by N samples", so synced and free differ only
in where the processor gets the position. The clock holds no position at all, and needs neither the
sample rate nor the tempo.

`PLANNING.md` required this rather than merely permitting it: alignment "must be derived from absolute
host PPQ each block rather than from a monotonically incremented local counter, otherwise a 16- or
32-step pattern drifts out of phase with the project".

### The trade this makes, stated plainly

02-02's clock owned a sample phase, so any partition of a block produced **bit-identical** offsets. A
position-driven clock must convert a block-*relative* position delta, and `(P - start_k) / rate` is
not bit-identical to the absolute form however it is arranged. So partitions are now compared as:

- **step indices, their order and their count — exactly;**
- **sample positions — within one sample** (20 µs at 48 kHz).

Nothing is lost, duplicated, reordered, or placed outside its block; those stay exact. Host sync is
impossible without being position-driven, so this is the right trade, and the tests say so rather
than quietly relaxing.

A second refinement of the same kind: tempo-change invariance is now stated as invariance under
partition **refinement**. Host tempo is sampled once per block by definition, so a block straddling an
automation edge legitimately runs at one tempo while a finer partition switches mid-way — comparing
irregular partitions across such an edge would assert something untrue.

## Acceptance Criteria Results

| AC | Result | Evidence |
|----|--------|----------|
| AC-1 Internal path behaviourally unchanged | ✅ | All of 02-02's clock properties migrated and green; two rewritten because their meaning changed (below) |
| AC-2 A block is described by a musical position range | ✅ | Contiguous spans tile the timeline; no gap, no repeat |
| AC-3 Step 0 lands on the host's bar | ✅ | Bars 0, 1, 7, 100; 32-step windows across two bars; non-bar-aligned origins; SYNC-off ignores the seek |
| AC-4 Host tempo drives; `bpm` ignored while synced | ✅ | Host 90 against a parameter of 132 gives 90's spacing; ramps stay unbroken |
| AC-5 Host transport followed | ✅ | Stopped host emits nothing; joining mid-timeline starts at the host's step |
| AC-6 Loops and jumps handled, decision recorded | ✅ | Loop wrap, forward jump, scrub; the downbeat survives every repetition; no catch-up burst |
| AC-7 Missing playhead information degrades safely | ✅ | 7 field-absence combinations pinned against an internal-tempo reference; NaN/inf/negative positions and tempi |
| AC-8 Audio-thread contract intact | ✅ | Allocation counter 0; no lock, no `juce::String`, no `patternState`; `getPosition()` once per block |
| AC-9 Three compilers | ✅ | GCC, Clang, MSVC clean of our warnings; 566 checks under each; loads in Ableton Live 12 |

## Three Things I Got Wrong Building It

Each was caught by the migrated tests rather than by reading the code.

1. **Rounding to nearest needed a clamp, and the clamp was not translation-invariant.** In a
   one-sample block every placement clamped to 0 while the same placement in a large block landed a
   sample later. Flooring removes the need entirely: membership is decided in position space, so a
   placement in `[start, end)` maps to an offset *provably* inside the block. `floor(x + n) ==
   floor(x) + n` is what makes any partition agree.
2. **Deriving the rate from `end - start` made the conversion depend on block length** — the same step
   landed on sample 4410 in one block of 8192 and 4409 across irregular blocks. The rate is passed
   directly now.
3. **My own test runner accumulated position per block.** 8192 additions of `perSample` do not sum to
   one addition of `8192 × perSample`, so span boundaries drifted and a placement could fall in a gap
   (lost) or an overlap (emitted twice). Replaced with a cursor deriving position by one multiply from
   an exact integer sample count.

## `/code-review` Findings

Eleven. Four serious, and **two of those were things I broke or omitted rather than subtleties**.

| # | Severity | Finding | Resolution |
|---|----------|---------|------------|
| 4 | High | **The runtime null-parameter guard had been deleted** in this plan's restructure — four raw pointers dereferenced on the audio thread behind only a `jassert` that compiles away in Release | Restored, before `planBlock`, which reads two of them. Not reachable from a test; verified by inspection and said so |
| 6 | High | The host-transport check ran **after** the ppq check, so a host reporting `PositionInfo` without a position left the sequencer free-running on stop | `getIsPlaying` is a plain bool that is always present; checked first |
| 7 | High | A **large finite** position was UB and hung the audio thread — `static_cast<long long>` past `LLONG_MAX` yields `LLONG_MIN` and the loop iterates ~9.2e18 times; past 2^53 the loop stops advancing | Position and rate bounded. The control confirmed it: `detected — hangs` |
| 1 | High | **Step 0 only locked to the bar in 4/4 starting at ppq 0** — the position came from quarter-notes since the origin, which coincides there, so every 4/4 test passed while the anchor the plan told me to use went unread | Anchored on `getPpqPositionOfLastBarStart`, `getTimeSignature`, `getBarCount`. Where a bar is not a whole number of patterns the pattern rotates — inherent, now pinned (3/4 → 0, 12, 8, 4; 7/8 → 0, 14) |
| 3 | Medium | **A looping host lost the step at the loop start on every repetition.** The wrap lands inside a block for most block sizes, and the step then sits behind the next block's start position | The block is split at the loop point into two segments, the second's offsets shifted to where it begins. Traced first: a one-bar loop at 120 BPM and 512-sample blocks loses its downbeat every bar |
| 2 | Medium | Consecutive spans can **overlap**, and a pure-function clock emits the overlapping step twice — two triggers on one musical step in Phase 3 | First patched with a filter at the consumer; `/simplify` then showed the filter was **asymmetric** and the patch was at the wrong depth. Replaced by one position watermark — see below |
| 8 | Medium | **The tempo-ramp case could not fail** — it recorded only on change, and `getCurrentStep()` exposes just the last step of a block, so a duplicate was invisible by construction | The fake host can advance at a tempo other than the one it reports, and the processor exposes emitted/dropped counters — observability Phase 5 needs anyway |
| 5 | Medium | `internalPositionInSteps` was frozen while synced, so any fallback resumed from a stale value and restarted the pattern | It follows the host now |
| 9 | Low | The host-stopped path left `Clock::lastEmittedStep` on a live step | Reset there too |
| 10 | Low | Swing parity taken on the windowed index flips at each wrap for an odd window | Parity taken on the absolute step |
| 11 | Low | Negative positions accepted, where the plan said reject | **Deliberate deviation**: a host count-in reports negative ppq and the groove should play through it on the same grid. Tested |

**One finding I disagreed with after checking.** The review expected the loop wrap to need duplicate
filtering. Splitting the block means it produces none, so that assertion checks zero — the filter is a
backstop for reported-tempo mismatch, not the mechanism that makes loops work.

## `/simplify` Findings

Four agents. The altitude review found something the code review had not, and it was **my own fix
being at the wrong depth**.

### The duplicate filter was a symptom patch — and asymmetric

I had made the processor drop a step at or behind the last one emitted. That caught the *overlap*
case, where two consecutive spans cover the same step. It was **blind to the gap case**: when the
host's next position overshoots the computed end, the step in between is never emitted at all, and no
counter noticed.

The root cause was two representations of one concept — an internal-path position plus a watermark of
the last step emitted, with the synced path writing the internal path's variable to keep them in
step. There is now **one `positionInSteps` that both paths own**: the synced path re-anchors it when
the host has genuinely moved (more than a step of disagreement) and otherwise lets it tile forward.
Spans tile by construction, so duplicates and gaps are *impossible* rather than filtered.

Deleted with it: `lastEmittedStepPosition`, `previousSpanStart`, `havePreviousSpan`,
`currentSegmentOffset`, `StepEvent::position`, and the `droppedSteps` counter. `stepTriggered` is two
statements again.

### The offset channel made the clock's own documentation false

The processor was shifting the sample offset inside its listener via a mutable member, so
`Clock.h`'s documented "`sampleOffset` is within the block, `[0, numSamples)`" was **false for the
second segment of a split block** — while a clock test asserted exactly that invariant. `Clock::Span`
now carries the block offset and the clock adds it. Any other listener would have had to know to add a
value it could not see.

### The bar-anchoring fallback restored the flaw it existed to fix

Deriving the bar number from the bar start assumes the current meter has held since position 0 —
false in exactly the two situations anchoring exists for. `getBarCount` is now required alongside the
bar start and meter, and an absent one falls back to the project origin rather than inventing a
number. Lifting the rule to a free function was worth it on its own: the HIGH finding it belonged to
survived a whole review pass precisely *because* it could only be reached by driving a whole
processor.

### Applied from the other three angles

| Angle | Fix |
|-------|-----|
| Efficiency | Dropped the `floor(start) - 1` first candidate — every emitter satisfies `e >= floor(start)`, so it could never emit at any swing value: one guaranteed-wasted iteration per call against roughly two useful ones |
| Efficiency | Noted why the division is **not** a hoisted reciprocal: not bit-identical, and a reciprocal can flip the floor at a boundary, breaking the partition stability the comment rests on |
| Efficiency | A **pre-existing** case pushed an 800 KB base64 string through `ValueTree` and XML to exercise a guard that rejects on *length* before decoding — 35% of the whole suite for no extra coverage. Suite wall time halved |
| Reuse / Simplification | Three assertions that could not fail — two `check(true)` and a `blocksRendered` comparison asserting its own argument back — now assert the specified behaviour, with a note that reaching the line *is* the no-hang evidence |
| Reuse / Simplification | Two `queryCount` checks that together meant equality became one; `FakePlayHead` computes `beatsPerBar` once; stale references to `resolveSpan` and to "02-03's handover" corrected |

### The efficiency review corrected my premise, with numbers

I had assumed the suite's cost was block rendering. Measured: **processor construction is 72.5 µs,
`processBlock` is 53–75 ns** — one rig costs as much as ~1000 rendered blocks. `Clock::advance` is
5.0 ns, against a 5.33 ms budget at 256/48 kHz. The 188×8-block loop sweep I worried about is 1.1% of
the suite and *is* the assertion.

### Skipped, with reason

- **Collapsing the host-sync cases into one table.** The simplification agent argued against it and I
  agree: each case pins exactly one property, and a shared row shape would hide which field each is
  about. The plumbing duplication is worth removing; the cases are not.
- **Sharing one processor across the degradation matrix** (~1.1 ms). At 10 ms total, not worth the
  coupling between cases.
- **`resetPending` folded into a transport snapshot.** The altitude review argued *against* this and
  changed my mind: it is a consume-once command edge, not latest-wins data, so folding it into a
  snapshot forces either a generation counter the audio thread must write back, or dropped resets. It
  also pointed out where the snapshot pressure actually is — the three independent audio→UI atomics
  Phase 5 will read at frame rate. Recorded against Phase 5's FIFO instead of 02-04.

## What the Negative Controls Caught

Thirteen mutations across two rounds. Four were reported MISSED; **three were real test gaps and one
was a broken control**:

- **All the degradation cases started from a fresh processor**, so nothing could see a stale-position
  fallback, a stopped host that omits its position, or a large position. Those properties only exist
  in a *sequence*. Three new cases close them; the controls then detected all three, the magnitude one
  as `hangs`.
- **The F6 control mutation was wrong** — it moved the ppq *declaration* rather than the ppq *guard*,
  so the transport check still ran first and the mutation reproduced nothing. Rewritten; detected.
- The restored null guard is not reachable from a test without deliberately building a broken
  parameter layout. Recorded as inspection-only rather than left looking covered.

## Deviations From Plan

- **`advance` takes a rate, not an end position.** The plan specified `(start, end, numSamples)`.
  Deriving the rate from `end - start` made placement depend on block length, so the rate is passed.
- **Negative host positions accepted**, where the plan said reject — see finding 11.
- **The loop-point split was not in the plan.** AC-6 said loops must be handled; tracing showed the
  block-straddle case drops the downbeat every repetition, which is not "handled".
- **Observability counters added to the processor.** Not planned, but the duplicate hazard is
  unmeasurable without them, and Phase 5's hit visualiser wants them regardless.

## Decisions Made

| Decision | Rationale |
|----------|-----------|
| The clock takes a position and a rate; it holds no position | Host jumps have nothing to contradict; one code path for synced and free |
| Floor rather than round-to-nearest, and no clamp | The clamp was the only thing breaking partition invariance; flooring makes the offset provably in range |
| Partition equality: indices exact, positions within one sample | Bit-identity is unreachable once position-driven, and host sync requires that. Stated rather than hidden |
| Split the block at the host's loop point | Otherwise the downbeat is lost on every loop repetition |
| Filter duplicate steps at the consumer, reset on a backwards jump | Overlapping spans are a property of hosts, not a bug to fix upstream; a loop must still replay |
| Bar anchoring from the host's bar, not the project origin | `PLANNING.md` says "lock step 0 to the host bar", and only the anchored form survives a non-aligned origin or a meter change |
| Negative positions accepted | A count-in should play on the same grid |

## Deferred

- **Non-4/4 pattern rotation.** A 16-step pattern cannot both fit a 12-step bar and stay 16 steps.
  Pinned by tests; a future "pattern length follows the meter" option would be the real answer.
- **Loop split needs host loop points.** Hosts that omit them get the backwards-jump reset, which
  prevents duplicates but cannot recover the dropped step at the wrap.
- **`MSB8064` (22×)** — unchanged in kind.

## Files Created / Modified

| File | Change |
|------|--------|
| `src/Clock.h` / `.cpp` | Position-and-rate API; no internal position; `StepEvent::position`; magnitude bounds |
| `src/PluginProcessor.h` / `.cpp` | `planBlock`, loop split, duplicate filter, bar anchoring, observability counters |
| `tests/FakePlayHead.h` | New — scriptable host: tempo, transport, bar anchor, bar count, loop points, per-field omission, reported-vs-actual tempo mismatch |
| `tests/ClockTest.cpp` | All 02-02 properties migrated; two rewritten; partition contract stated |
| `tests/StateRoundTripTest.cpp` | Host-sync suite: bar lock, meters, tempo, transport, loops, duplicates, degradation, stateful sequences |
| `src/ParameterIDs.h` | `kMinBpm`/`kMaxBpm` moved here with the tempo itself |

## Next

**02-04: Lock-free pattern handover** — a double-buffer and atomic index so the audio thread can read
pattern tables. `PLANNING.md` names the prototype's `loadProfile` reassigning `state.grid` while the
scheduler reads it as exactly the race to prevent.

The `resetPending` question recorded at 02-02 is now **answered: keep it separate.** It is a
consume-once command edge, not latest-wins data publication, and folding it into a snapshot would
force either a generation counter the audio thread has to write back — turning a read-only
publication into an RMW every block — or dropped resets when two arrive between blocks. The
two-variable consistency it needs already exists in `setPlaying`'s release paired with
`processBlock`'s acquire.

Recommended landing site for 02-04's read, from the altitude review: read the atomic index **once per
block** in `processBlock`, never per step, and hand the resulting pointer down through a
stack-allocated per-block emitter implementing `StepListener`. `StepListener` being an interface
already supports that at zero cost, and it avoids repeating the mutable-member channel this plan just
removed.
