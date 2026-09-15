/* ============================================================================
   FORRÓ BOX — StepSnapshot

   What the audio thread tells the UI about the step it has just fired: the step
   index and the eight lane velocities that step carried, as ONE value.

   Three separate atomics before this — `currentStep`, `lastStepVelocities` and
   `emittedSteps`. Ordered, so a reader that acquired the step saw velocities at
   least as new, but NOT group-atomic: a reader could hold step N beside step
   N+1's velocities. PROJECT.md recorded that at Phase 2 and nothing read them
   until Phase 5 gave the publication three readers at frame rate.

   ONE 64-BIT WORD, not a struct behind a lock or a buffer swap. A velocity is
   0..127 by invariant — `State::kMaxVelocity`, clamped on load at
   `ForroBoxState.cpp:39` and by every setter — so it is SEVEN bits, not eight.
   Eight lanes is 56 bits, and the step fits in six more. 62 bits total, in a
   type `std::atomic` is lock-free on every platform this ships to.

   That matters because the alternative was a mechanism. At eight bits a lane the
   payload is 16 bytes, `std::atomic` over it is not guaranteed lock-free, and
   `processBlock` is contractually lock-free — so it would have needed a
   double-buffer (whose slots a slow reader can have overwritten under it) or a
   seqlock (which 02-04 judged and rejected for the pattern going the other way).
   A value that fits in one word needs neither, and there is no torn read to
   defend against because there is nothing to tear.

   The step is stored as `step + 1`, so an all-zero word reads as STOPPED. A
   fresh processor and a cleared word therefore agree, which a sentinel of -1
   packed into unsigned bits would not.
============================================================================ */
#pragma once

#include <atomic>
#include <array>
#include <cstdint>

#include "Clock.h"
#include "ForroBoxState.h"

namespace forrobox
{

/** One step and what it played. The decoded form; the wire form is a uint64. */
struct StepSnapshot
{
    int step { Clock::kStoppedStep };
    std::array<std::uint8_t, State::kNumLanes> velocities {};

    bool isStopped() const noexcept { return step == Clock::kStoppedStep; }
};

namespace stepwire
{
    /** Seven bits a lane, because 127 is the domain's own maximum and not a
        convenient truncation — `State::kMaxVelocity`, enforced on every path
        into the lanes. */
    inline constexpr int kVelocityBits = 7;
    inline constexpr std::uint64_t kVelocityMask = (std::uint64_t { 1 } << kVelocityBits) - 1;

    inline constexpr int kStepShift = kVelocityBits * State::kNumLanes;
    inline constexpr int kStepBits  = 6;   ///< 0 = stopped, 1..32 = step 0..31
    inline constexpr std::uint64_t kStepMask = (std::uint64_t { 1 } << kStepBits) - 1;

    static_assert (State::kMaxVelocity <= kVelocityMask,
                   "a velocity must fit in its field — widen kVelocityBits or the word");
    static_assert (State::kMaxSteps + 1 <= (1 << kStepBits),
                   "the step plus its stopped sentinel must fit in kStepBits");
    static_assert (kStepShift + kStepBits <= 64,
                   "the whole snapshot must fit in one 64-bit word — that is the point");

    /** Pack. Velocities above the domain maximum are CLAMPED, not truncated:
        `& mask` on 200 yields 72, which is a plausible-looking wrong answer. */
    std::uint64_t encode (int step, const std::array<std::uint8_t, State::kNumLanes>&) noexcept;

    /** Unpack. The inverse of `encode` for every value the domain allows. */
    StepSnapshot decode (std::uint64_t word) noexcept;
} // namespace stepwire

/** The publication itself: written on the audio thread, read on the message
    thread, and lock-free on both by construction. */
class StepPublisher final
{
public:
    /** AUDIO THREAD. Allocation-free, lock-free, wait-free: one relaxed-ordered
        store and one increment. */
    void publish (int step, const std::array<std::uint8_t, State::kNumLanes>& velocities) noexcept;

    /** AUDIO THREAD. The transport stopped — publish the stopped sentinel so the
        playhead hides rather than freezing where it was. */
    void publishStopped() noexcept;

    /** MESSAGE THREAD. One load, so what comes back is always one real step's
        index beside that same step's velocities. */
    StepSnapshot read() const noexcept;

    /** How many steps have been published. Separate from the word on purpose:
        it answers "was a step emitted twice in one block", which is a question
        about the SEQUENCE and needs no consistency with any one step's
        velocities. `PluginProcessor.h` records why that count exists — 02-02 hit
        the duplicate hazard and `getCurrentStep()` alone could not see it. */
    std::uint32_t publicationCount() const noexcept
    {
        return count.load (std::memory_order_relaxed);
    }

private:
    std::atomic<std::uint64_t> word { 0 };   ///< 0 == stopped, see the header note
    std::atomic<std::uint32_t> count { 0 };

    static_assert (std::atomic<std::uint64_t>::is_always_lock_free,
                   "the snapshot word is stored from the audio thread");
    static_assert (std::atomic<std::uint32_t>::is_always_lock_free,
                   "the publication count is incremented from the audio thread");
};

} // namespace forrobox
