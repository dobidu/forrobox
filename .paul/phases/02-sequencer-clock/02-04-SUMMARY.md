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

### The mechanism, twice

**What shipped is not what I built first.** The first implementation was a hand-rolled seqlock, and it
worked — reviewed, tested, negative-controlled, green under three compilers. `/simplify`'s reuse agent
then found that my "JUCE has nothing, so hand-roll it" conclusion was right on the check and wrong on
the conclusion: `juce_Convolution.h:250-253` documents the primitive for exactly this job — *"use some
wait-free construct (a lock-free queue or a SpinLock/GenericScopedTryLock combination) to transfer
ownership to the audio thread without allocating."*

So the final mechanism is JUCE's: the writer takes a `SpinLock` and assigns the staging table; the
audio thread **try**-locks and, failing, keeps the snapshot it already has. `tryEnter` is one
compare-exchange that never spins, so the reader is still wait-free.

That switch removed three things, not just lines:

- **The relaxed atomic staging bytes.** The efficiency agent measured that copy at **57.8 ns** —
  atomics block vectorisation by construction — against **1.92 ns** for the plain `memcpy` a lock
  allows. My header had claimed the formal race-freedom "costs nothing"; it cost 30×. It now genuinely
  costs nothing, because the lock does the job the atomics were doing.
- **The generation-parity trick and its fences.**
- **The worst failure mode in the file.** Two interleaved publishes could leave the seqlock's
  generation permanently *odd*, after which every refresh failed and the audio thread held one stale
  table for the rest of the session, silently. A lock has no latched state — and because the lock
  lives in the publisher rather than in the processor, "two publishes cannot interleave" is enforced by
  the type instead of documented as a caller obligation. That was the altitude agent's separate
  recommendation, reached for free.

271 lines became 196.

### Why it is not the mechanism that was recorded either

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
| AC-8 Three compilers | ✅ | GCC, Clang, MSVC clean of our warnings; 606 checks under each; loads in Ableton Live 12 |

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

## `/simplify` Findings

Four agents. Two changed the implementation; one corrected a measured claim in my own comments; one
found a hole in the guarantee I thought was structural.

### `lockPatternState()` had zero production callers

The altitude agent's sharpest finding. Both state methods took `stateLock` directly, so
**"every writer publishes automatically" was enforced at neither of its two production sites** — the
handle was exercised only by tests, and `setStateInformation` carried all the real traffic through an
explicit publish. The next person adding a writer would have read that method as the example and
copied the bypass. Both methods now go through the handle; the explicit publish is deleted; no bare
`stateLock` use remains.

### The intra-block counter was a symptom of the emitter's shape

Also altitude, and better than my design. `BlockEmitter` held a *live, refreshable* reader, so
"a step reads a different table than its block-mate" was expressible — and the only way to observe it
was to race a whole processor and count from inside. Holding the **lanes** instead makes it
unrepresentable: there is no refresh for a step to call.

That deleted the counter, its atomic, its public accessor, the per-step load on the audio thread, and
**the test that read it** — a probabilistic race detector whose threshold had to be measured, and which
at my first guess was green against the very regression it existed to catch. A structural guarantee
beats a tuned race detector.

### Deleted, all with no callers

`getStepReadout` (which additionally promised group atomicity the memory model does not give —
release/acquire makes the velocities at least as *new* as the step, not part *of* it),
`getHeldPatternGeneration`, `hasHit`, the `publications` counter (it was the generation halved),
`havePublished`, and the public `publish()` whose interaction with `publishIfChanged` needed a
paragraph to explain why it was not a bug.

The eight lane velocities became one atomic 64-bit word, so they cannot straddle two steps among
themselves. Consistency with the step *index* still needs one published struct — Phase 5's trigger
FIFO, logged in STATE rather than half-built here.

### The suite was 89 ms of nothing

Measured: `seedOf` re-verifying bytes a failed refresh cannot have touched was 54 ms of a 130 ms suite,
and surplus `sleep_for` on a liveness check was another 35 ms. Sampled and reduced. **The suite is
0.05 s.**

### Skipped, with reason

- **Splitting `LockedState` into read and write handles.** It would delete `publishIfChanged`
  entirely — but the read consumer it exists for is Phase 4's editor, which does not exist. Inventing
  a second handle for a hypothetical caller is speculative; recorded instead.
- **Enforcing the writer invariant with an assertion.** Moot: the publisher owns its lock now, so the
  invariant is structural.

## What the Negative Controls Caught

Sixteen mutations across two mechanisms — the seqlock, then the try-lock that replaced it. Against the
final implementation all seven detect. Along the way:

- **`failed > 0` could not distinguish what it claimed.** A refresh returns false both when the lock
  was busy and when there is nothing new, so a reader changed to *block* instead of trying still shows
  plenty of failures whenever it outruns the writer — and a control proved it by passing. The reader
  now counts **contention** separately, which is the actual observable for "the reader never waits".
  The control reports `0 contended of 200000` when it fires.
- **Nothing tested the property `publishIfChanged` exists for.** A control removing the change check
  passed the whole suite. There is now a case asserting twenty read-only handle acquisitions cost zero
  copies and publish nothing, while a write through the same handle costs exactly one.
- **"Commit before verifying" was a fake mutation** — I added a comment, which changes nothing.
- **"Refresh per step" was MISSED three times**: the mutation did not compile, then only declared a
  member without calling refresh, then the publication target was too low. It is now moot — the
  regression is unrepresentable.

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
| `src/PatternSnapshot.h` / `.cpp` | New — `PatternPublisher`, `PatternReader`, on JUCE's `SpinLock`/try-lock pairing |
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
