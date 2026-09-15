/* ============================================================================
   FORRÓ BOX — pattern handover

   Lets the audio thread read the pattern grid while the message thread keeps
   editing it, with no allocation, no waiting, and no possibility of reading a
   half-written table.

   PROJECT.md states the constraint: "the audio thread picking up new pattern
   tables via double-buffer or AbstractFifo swap, never in-place mutation".
   PLANNING.md names the prototype's own loadProfile() reassigning state.grid
   while the scheduler reads it as exactly the race to prevent.

   ── Why a spin try-lock ────────────────────────────────────────────────────
   JUCE has no value-swap or latest-value container — AbstractFifo and
   SingleThreadedAbstractFifo are queues, where every item produced is meant to
   be consumed, and this is publication: the audio thread wants the newest table
   and does not care how many it missed. But JUCE does document the primitive
   for precisely this job (juce_Convolution.h:250-253): "use some wait-free
   construct (a lock-free queue or a SpinLock/GenericScopedTryLock combination)
   to transfer ownership to the audio thread without allocating."

   So: the writer takes the lock and assigns the staging table; the audio thread
   TRY-locks, and if it cannot get in it keeps the snapshot it already has and
   tries again next block. tryEnter() is a single compare-exchange that never
   spins, so the reader is wait-free — a collision costs one block of staleness,
   never a wait. When nothing has been published it copies nothing at all.

   This replaced a hand-rolled seqlock, which worked and was tested but was 271
   lines against 60, needed relaxed atomic bytes to avoid a formal data race,
   and carried one genuinely bad failure mode: two interleaved publishes could
   leave its generation counter permanently odd, after which every refresh
   failed and the audio thread held one stale table for the rest of the session,
   silently. A lock has no such latched state — and because the lock lives HERE
   rather than in the processor, "two publishes cannot interleave" is enforced by
   the type instead of documented as a caller obligation.

   ── The audio thread never waits, and never blocks the writer for long ─────
   The writer can spin while the audio thread holds the lock, so the audio
   thread holds it only for a 256-byte copy. That is the trade JUCE's
   recommendation makes, and it is the right way round: a message thread
   spinning for a microsecond is invisible, an audio thread waiting is a
   dropout.
============================================================================ */
#pragma once

#include "ForroBoxState.h"

#include <atomic>
#include <cstdint>

namespace forrobox
{

/** The eight lanes, exactly as `State` holds them. */
using PatternLanes = decltype (State::lanes);

/** Publishes pattern tables for the audio thread.

    Owns the lock that serialises its writers, so callers cannot get the
    serialisation wrong. Any thread may publish; concurrent publishes are
    ordered rather than corrupting. */
class PatternPublisher
{
public:
    /** Publishes `lanes` if they differ from what was last published.

        The change check is what lets every writer publish unconditionally
        without cost: the LockedState handle publishes when it is destroyed, and
        that handle is taken for READ access too, so an unconditional publish
        would make every read force the audio thread into a pointless copy. A
        256-byte compare is cheaper than that, and it means "every writer
        publishes" can be automatic rather than remembered — which is the whole
        point, because remembering is what fails.

        Returns true if it published. */
    bool publishIfChanged (const PatternLanes& lanes);

    /** How many complete publications there have been.

        A diagnostic until 05-03, when the EDITOR started following it: the
        sequencer grid edge-detects this to know the pattern changed, so it sees
        a host recall, a profile load and its own click, and does not see the
        reads `~LockedState` is also taken for.

        DO NOT MAKE IT UNCONDITIONAL. Dropping `publishIfChanged`'s compare —
        to save the 256-byte memcmp, say — would make every READ look like a
        change and the editor would repaint sixty times a second. That contract
        was written only in the consumer's header, where someone optimising this
        file would never see it. /simplify. */
    std::uint32_t publicationCount() const noexcept
    {
        return generation.load (std::memory_order_relaxed);
    }

private:
    friend class PatternReader;

    juce::SpinLock lock;
    PatternLanes staging {};
    PatternLanes lastPublished {};

    // Written under the lock, read without it by publicationCount(), so atomic.
    // No parity trick and no fences: the lock does that job now.
    std::atomic<std::uint32_t> generation { 0 };

    static_assert (std::atomic<std::uint32_t>::is_always_lock_free,
                   "the generation is read on the audio thread");
};

/** The audio thread's private view of the pattern grid.

    Owns its own copy, so nothing it hands out can change under a block. Live on
    the audio thread and nowhere else. */
class PatternReader
{
public:
    /** Picks up a newer table if there is one and the lock is free.

        Wait-free: `tryEnter` is one compare-exchange, and a failure means the
        previous snapshot is kept rather than waited for. Call once per block,
        never per step. Returns true when the snapshot changed. */
    bool refresh (PatternPublisher& publisher) noexcept;

    /** The snapshot itself, immutable.

        Handing out the LANES rather than the reader is what makes "every step
        in a block reads one table" structural: a per-block consumer holding
        this cannot refresh, so a step cannot see a table its block-mate did
        not. It replaced a per-step counter that existed only to let a
        probabilistic test look for the bug this makes unrepresentable. */
    const PatternLanes& lanes() const noexcept { return snapshot; }

    /** Velocity at a lane and a STORAGE index, 0..kMaxSteps-1.

        Storage, not the active window: 02-01 settled that the 32 slots are
        storage and the `steps` parameter is a view onto them, so a caller
        indexing by the window would silently play the wrong half of a 32-step
        pattern. Out-of-range asks return 0 rather than reading past the array. */
    std::uint8_t velocityAt (int lane, int storageIndex) const noexcept;

    /** The generation this snapshot came from. 0 means nothing has been
        published and the snapshot is the zeroed grid. */
    std::uint32_t heldGeneration() const noexcept { return held.load (std::memory_order_relaxed); }

    /** How many times refresh() actually copied. Relaxed atomics, not plain
        scalars: refresh() writes these on the audio thread and the processor
        exposes them as diagnostics a message-thread caller may poll. */
    int copyCount() const noexcept { return copies.load (std::memory_order_relaxed); }

    /** How many times refresh() gave up because the writer held the lock.

        This is the observable for "the reader never waits". Counting FAILED
        refreshes cannot show it: a refresh also returns false when there is
        simply nothing new, so a reader changed to block instead of trying still
        shows plenty of failures whenever it outruns the writer — a negative
        control proved exactly that. Contention has to be counted separately to
        be seen. */
    int contentionCount() const noexcept { return contentions.load (std::memory_order_relaxed); }

private:
    PatternLanes snapshot {};
    std::atomic<std::uint32_t> held { 0 };
    std::atomic<int> copies { 0 };
    std::atomic<int> contentions { 0 };
};

} // namespace forrobox
