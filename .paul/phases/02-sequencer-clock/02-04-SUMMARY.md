---
phase: 02-sequencer-clock
plan: 04
type: Summary
about: "Forró Box"
description: "The audio thread can read the pattern grid with no lock, no allocation and no torn read — closing Phase 2 and unblocking Phase 3's voices and Phase 6's profile reload."
completed: 2026-09-08
---

# 02-04 Summary: Lock-free pattern handover

## What Was Built

`processBlock` no longer has a comment explaining why it must not touch the pattern grid. It reads it,
through a snapshot it owns privately, while the message thread keeps editing freely.

`PROJECT.md` set the constraint — "the audio thread picking up new pattern tables via double-buffer or
`AbstractFifo` swap, never in-place mutation" — and `PLANNING.md` named the prototype's own
`loadProfile()` reassigning `state.grid` under the scheduler as exactly the race to prevent.

### The mechanism, and why it is not the one that was recorded

STATE recorded "double-buffer + atomic index" from 02-02 planning. It does not survive tracing: with
two slots the writer's target is the slot the reader is not using, but **after two publications the
only remaining target is the slot the audio thread is holding** — and two publications inside one
~5 ms block are reachable through a profile reload that publishes then fixes up, a drag across pads,
or `setStateInformation`. Nothing in that design enforces the spacing it depends on. Superseded at
plan time, with the trace, and confirmed by the user.

What shipped instead: the writer bumps a generation to odd, writes the staging table, bumps it to
even. The reader copies into its own buffer **only when the generation changed**, verifying before and
after; on a collision it keeps the snapshot it already had. Both sides are wait-free — the reader
never spins and never retries, a collision costs one block of staleness — and when nothing has been
published it copies nothing at all.

Two details worth their own decisions:

- **The staging bytes are relaxed atomics, not plain bytes.** A textbook seqlock reads plain bytes
  while the writer may be writing them, which is a data race and formally undefined behaviour however
  well it works. Relaxed atomics are race-free by definition and compile to the same loads on x86 and
  ARM, so the caveat costs nothing.
- **`LockedState` publishes when it is destroyed**, so a writer cannot forget. "Every writer must
  remember" is the invariant that fails, and this project has already been bitten by a partial
  guarantee — 01-02's `getPatternState()` handed out a mutable reference with a lock covering only
  some paths. It calls `publishIfChanged`, because the handle is taken for *read* access too and an
  unconditional publish would make every read force the audio thread into a pointless 256-byte copy.

### Checked before hand-rolling

`juce::AbstractFifo` and `juce::SingleThreadedAbstractFifo` are queues — every item produced is meant
to be consumed. This is latest-value publication: the audio thread wants the newest table and does not
care how many it missed. JUCE has no value-swap or triple-buffer utility anywhere in the modules.

## Acceptance Criteria Results

| AC | Result | Evidence |
|----|--------|----------|
| AC-1 Fully visible or not at all | ✅ | A real writer thread; every snapshot matches a published seed, none a mixture |
| AC-2 Reader wait-free, allocates nothing | ✅ | Under saturation some refreshes give up rather than retrying; allocation delta 0 |
| AC-3 Latest picked up promptly | ✅ | Observed most publications at a realistic rate; copies nothing when nothing published |
| AC-4 Every writer publishes | ✅ | Handle publishes on release; `setStateInformation` explicitly; both negative-controlled |
| AC-5 Read once per block through the snapshot | ✅ | One publication → exactly one copy; no block reads two tables |
| AC-6 A step's velocity is the grid's | ✅ | Distinguishable per lane and per step; 32-step window reads storage index, not window index |
| AC-7 Timing unchanged | ✅ | Every 02-02 and 02-03 property still green |
| AC-8 Three compilers | ✅ | GCC, Clang, MSVC clean of our warnings; 604 checks under each; loads in Ableton Live 12 |

## `/code-review` Findings

Eight. The seqlock core survived scrutiny — the reviewer traced the fence pairing against
`[atomics.fences]`, confirmed generation overflow preserves the odd/even invariant, that a reader
seeing only stale bytes gets a self-consistent *old* table rather than a torn one, and that
`LockedState`'s destructor publishes under the lock because the `ScopedLock` member is declared first
and destroyed last.

**The critical finding was that my headline test proved nothing** — and it is the mistake I had
written a rule about two plans earlier.

| # | Severity | Finding | Resolution |
|---|----------|---------|------------|
| 1 | Critical | **The intra-block case could not fail.** A 2048-sample block at 132 BPM / 48 kHz spans 0.376 of a sixteenth, so no block ever emitted two steps, the "did the generation change between steps" branch was unreachable, and the reviewer proved it by moving the refresh into the callback — the exact regression the counter exists to catch — with the suite still green | Blocks are now 32768 samples (six steps each) with a `static_assert` that a block holds at least two. See also the threshold measurement below |
| 2 | Medium | The companion message claimed "blocks with several steps each" at 0.376 steps per block | It measures and reports the figure now |
| 3 | Medium | `PatternReader::held` and `copies` were plain scalars written on the audio thread while exposed as message-thread diagnostics | Relaxed atomics |
| 4 | Medium | `lastStepVelocities` and `currentStep` were all-relaxed but documented as the pair Phase 5 reads cross-thread — relaxed gives no ordering, so the reader could see the new step beside a lane holding the previous one's value | `currentStep` releases last, `getCurrentStep` acquires, `getStepReadout()` reads the group |
| 5 | Medium | `publish()` did not maintain `lastPublished`, so mixing it with `publishIfChanged` silently dropped a publication | `publish()` maintains it |
| 6 | Medium | The writer-side invariant is "called under `stateLock`", not "called from the message thread" — `setStateInformation` may run on a host loader thread | Documented, with the consequence: two interleaved publishes can leave the generation permanently odd, after which every refresh fails for the rest of the session |
| 7 | Low | `fbtest::allocations` was a plain `size_t`, and this is the first diff to run worker threads in a suite that samples it | Atomic |
| 8 | Low | A processor prepared with 64 samples was rendered with 512 | Fixed; Phase 3 will size voice buffers against `prepareToPlay` |

### The threshold was guessed, and that made the fix insufficient

Fixing the block size was not enough. The case is a **probabilistic race detector**: it catches a
table changing between two steps of one block only if a publication lands in that window. So the
publication target decides whether it detects anything, and mine was 25. Injecting the regression and
sweeping:

| target | blocks | intra-block changes seen |
|--------|--------|--------------------------|
| 25 | ~8 | **0** |
| 200 | 63 | 15 |
| 2000 | 6385 | 1333 |

At 25 the assertion was green against the very regression it exists to catch, *even after* the block
size was fixed. It is now 300, with the measurement in the comment, because the next person to lower
it for speed needs to know what it buys.

## What the Negative Controls Caught

Nine mutations. Six detected immediately; three needed the controls themselves fixed:

- **"Commit before verifying" was a fake mutation** — I added a comment, which changes nothing.
  Rewritten to commit a torn table on failure; detected.
- **"Refresh per step" did not compile**, then compiled but was MISSED twice — first because the
  mutation only declared a member without calling refresh, then because the target was too low. The
  measurement above is what finally made it detect.
- The per-step hazard resisted a one-line mutation, which is itself weak evidence the shape is right —
  but not evidence of the assertion, so the **counter's own instrument control** (forcing it to see a
  mismatch) is recorded separately.

## Deviations From Plan

- **The mechanism is a generation counter, not a two-slot index** — traced at plan time, confirmed
  with the user, and recorded in STATE as superseding the 02-02 decision.
- **`publishIfChanged` was not in the plan.** The plan said the handle should publish on release; that
  is only affordable if a redundant publish is free, because the handle is taken for reads too.
- **`getStepReadout` and the release/acquire pairing** came from the review, not the plan.
- **The intra-block generation counter** was not planned. It exists because counting copies could not
  distinguish per-block from per-step refresh — `refresh` is idempotent once the generation is held.

## Decisions Made

| Decision | Rationale |
|----------|-----------|
| Generation counter + reader-private snapshot | A two-slot index has no safe target after two publications inside one block |
| Relaxed atomic staging bytes | Removes the seqlock's formal data race at no measured cost |
| `LockedState` publishes on destruction, via `publishIfChanged` | Automatic beats remembered; the change check keeps reads free |
| The writer invariant is the lock, not the thread | `setStateInformation` may run off the message thread; only the lock serialises it |
| `currentStep` released last, velocities read as a group | Phase 5 reads them cross-thread; relaxed gave no ordering |
| The step reads **storage** index, not window index | 02-01 settled that the 32 slots are storage and `steps` is a view |

## Deferred

- **Enforcing the writer invariant** rather than documenting it — an assertion that the lock is held
  would need the publisher to know about the lock. Logged.
- **One published struct for the audio→UI channel.** `currentStep`, `emittedSteps` and the velocities
  are separate atomics; the release/acquire pairing makes the step and its velocities consistent, but
  Phase 5's trigger FIFO is where this belongs, as STATE already records.
- **`MSB8064` (22×)** — unchanged in kind.

## Files Created / Modified

| File | Change |
|------|--------|
| `src/PatternSnapshot.h` / `.cpp` | New — `PatternPublisher`, `PatternReader` |
| `src/PluginProcessor.h` / `.cpp` | Publisher and reader; `LockedState` publishes on release; stack `BlockEmitter`; step readout |
| `tests/StateRoundTripTest.cpp` | Handover suite: torn-read detection, saturation, velocity mapping, per-block, restore |
| `tests/TestHarness.h` | Allocation counter shared and made atomic |
| `CMakeLists.txt` | The new source |

## Next

**Phase 2 is complete.** Step advance, host sync, the musical content and now the data path all exist,
and the plugin is still deliberately silent.

**Phase 3: Voices & mix bus** — eight synth voices triggered from these velocities, `CACHAÇA`
humanisation, per-channel gain and pan into a character bus and limiter. Its goal is A/B listening
against the prototype, and **planning it is blocked on the samples-versus-synthesised decision**
recorded in STATE: a sample library arrived mid-Phase-1 that is mostly tempo-locked loops, which do not
fit a step sequencer with per-step velocity and ghost notes.
