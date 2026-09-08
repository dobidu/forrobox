/* ============================================================================
   FORRÓ BOX — pattern handover

   Lets the audio thread read the pattern grid while the message thread keeps
   editing it, with no lock, no allocation, and no possibility of reading a
   half-written table.

   PROJECT.md states the constraint: "the audio thread picking up new pattern
   tables via double-buffer or AbstractFifo swap, never in-place mutation".
   PLANNING.md names the prototype's own loadProfile() reassigning state.grid
   while the scheduler reads it as exactly the race to prevent.

   ── Why a generation counter and not a two-slot index ───────────────────────
   02-02 recorded "double-buffer + atomic index". It does not survive tracing.
   With two slots the writer's target is the slot the reader is not using — but
   after two publications the only remaining target IS the slot the audio thread
   is holding, and two publications inside one ~5 ms block are reachable: a
   profile reload that publishes then fixes up, a drag across pads,
   setStateInformation. Nothing in that design enforces the spacing it depends
   on.

   Here the writer bumps a generation to odd, writes the staging table, and
   bumps it to even. The reader copies into its OWN buffer only when the
   generation changed, verifying it before and after; on a collision it keeps the
   snapshot it already had. Both sides are wait-free — the reader never spins and
   never retries, it just costs one block of staleness — and when nothing has
   been published the reader copies nothing at all.

   ── Why the staging bytes are atomic ───────────────────────────────────────
   A textbook seqlock reads plain bytes while the writer may be writing them,
   which is a data race and formally undefined behaviour however well it works
   in practice. Relaxed atomic bytes are race-free by definition and compile to
   the same plain loads and stores on x86 and ARM, so the formal caveat costs
   nothing and is worth not having.

   ── Not JUCE's ─────────────────────────────────────────────────────────────
   juce::AbstractFifo and juce::SingleThreadedAbstractFifo are queues: every
   item produced is meant to be consumed. This is latest-value publication —
   the audio thread wants the newest table and does not care how many it missed.
   JUCE has no value-swap or triple-buffer utility (checked across
   ~/JUCE/modules), so this is hand-rolled deliberately rather than by omission.
============================================================================ */
#pragma once

#include "ForroBoxState.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace forrobox
{

/** The eight lanes, as the message thread holds them. */
using PatternLanes = std::array<State::Lane, static_cast<size_t> (State::kNumLanes)>;

/** Publishes pattern tables for the audio thread. Written only from the message
    thread; there is exactly one writer by design. */
class PatternPublisher
{
public:
    /** Makes `lanes` the table the audio thread will pick up next.

        Message thread only. Cheap — a generation bump, 256 byte stores and
        another bump — but not audio-thread safe, and it does not need to be:
        every writer is a user gesture or a state load. */
    void publish (const PatternLanes& lanes) noexcept;

    /** Publishes only if `lanes` differs from what was last published.

        Message thread only. This is what lets every writer publish
        unconditionally without cost: the LockedState handle publishes when it is
        destroyed, and that handle is also taken for READ-only access, so an
        unconditional publish would make every read bump the generation and force
        the audio thread into a pointless 256-byte copy. A 256-byte compare on
        the message thread is cheaper than that, and it means "every writer
        publishes" can be automatic rather than remembered — which is the whole
        point, because remembering is what fails.

        Returns true if it published. */
    bool publishIfChanged (const PatternLanes& lanes) noexcept;

    /** The current generation. Even means "a complete table is published"; odd
        means a write is in progress. Starts at 0, which is even and describes
        the zeroed staging table honestly: an empty grid. */
    std::uint32_t currentGeneration() const noexcept
    {
        return generation.load (std::memory_order_acquire);
    }

    /** How many times publish() has been called. Diagnostic — a concurrency
        test that cannot say how many publications the reader actually saw is not
        evidence of anything. */
    std::uint32_t publicationCount() const noexcept
    {
        return publications.load (std::memory_order_relaxed);
    }

private:
    friend class PatternReader;

    static constexpr size_t kByteCount =
        static_cast<size_t> (State::kNumLanes) * static_cast<size_t> (State::kMaxSteps);

    std::array<std::atomic<std::uint8_t>, kByteCount> staging {};

    // What was last published. Message-thread-only, so plain — the audio thread
    // never touches it.
    PatternLanes lastPublished {};
    bool havePublished { false };
    std::atomic<std::uint32_t> generation { 0 };
    std::atomic<std::uint32_t> publications { 0 };

    static_assert (std::atomic<std::uint32_t>::is_always_lock_free,
                   "the generation is read on the audio thread");
    static_assert (std::atomic<std::uint8_t>::is_always_lock_free,
                   "the staging bytes are read on the audio thread");
};

/** The audio thread's private view of the pattern grid.

    Owns its own copy, so nothing it returns can change under it mid-block. Live
    on the audio thread and nowhere else. */
class PatternReader
{
public:
    /** Picks up a newer table if there is one. Wait-free: returns immediately
        whether it succeeded or not, and on a collision with the writer it keeps
        the snapshot it already had rather than waiting or retrying.

        Call once per block, never per step. Returns true when the snapshot
        changed. */
    bool refresh (const PatternPublisher& publisher) noexcept;

    /** Velocity at a lane and a STORAGE index, 0..kMaxSteps-1.

        Storage, not the active window: 02-01 settled that the 32 slots are
        storage and the `steps` parameter is a view onto them, so a caller
        indexing by the window would silently play the wrong half of a 32-step
        pattern. Out-of-range asks return 0 rather than reading past the array. */
    std::uint8_t velocityAt (int lane, int storageIndex) const noexcept;

    /** Whether that slot holds a hit at all. Velocity 0 is a rest. */
    bool hasHit (int lane, int storageIndex) const noexcept
    {
        return velocityAt (lane, storageIndex) > 0;
    }

    /** The generation this snapshot came from. 0 means nothing has been
        published yet, and the snapshot is the zeroed grid. */
    std::uint32_t heldGeneration() const noexcept { return held; }

    /** How many times refresh() actually copied. Diagnostic: it must stay flat
        while nothing is published, and the per-block-not-per-step property is
        checked against it. */
    int copyCount() const noexcept { return copies; }

private:
    // Two buffers so a failed verification costs nothing: the copy goes into the
    // inactive one and the active one only changes once the generation has been
    // confirmed unchanged. Copying into the live snapshot and undoing it would
    // mean the block could see torn data.
    std::array<PatternLanes, 2> buffers {};
    int active { 0 };
    std::uint32_t held { 0 };
    int copies { 0 };
};

} // namespace forrobox
