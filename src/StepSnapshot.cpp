#include "StepSnapshot.h"

namespace forrobox
{

namespace stepwire
{

std::uint64_t encode (int step, const std::array<std::uint8_t, State::kNumLanes>& velocities) noexcept
{
    std::uint64_t word = 0;

    for (size_t lane = 0; lane < velocities.size(); ++lane)
    {
        // CLAMPED, not masked. `& kVelocityMask` turns 200 into 72 — a value
        // that looks like a real hit and would light a pad at the wrong
        // brightness rather than failing. The lanes are clamped on every path in
        // (ForroBoxState.cpp:39 and State's setters), so this cannot fire today;
        // it is here so that it still cannot when a later writer forgets.
        const auto clamped = static_cast<std::uint64_t> (
            juce::jmin (velocities[lane], State::kMaxVelocity));

        word |= clamped << (kVelocityBits * static_cast<int> (lane));
    }

    // step + 1, so a zero word is STOPPED rather than step 0. Out-of-range
    // steps become stopped rather than aliasing onto a real step.
    const auto encodedStep = juce::isPositiveAndBelow (step, State::kMaxSteps)
                               ? static_cast<std::uint64_t> (step + 1)
                               : std::uint64_t { 0 };

    return word | (encodedStep << kStepShift);
}

StepSnapshot decode (std::uint64_t word) noexcept
{
    StepSnapshot out;

    const auto encodedStep = (word >> kStepShift) & kStepMask;

    out.step = encodedStep == 0 ? Clock::kStoppedStep
                                : static_cast<int> (encodedStep) - 1;

    for (size_t lane = 0; lane < out.velocities.size(); ++lane)
        out.velocities[lane] = static_cast<std::uint8_t> (
            (word >> (kVelocityBits * static_cast<int> (lane))) & kVelocityMask);

    return out;
}

} // namespace stepwire

void StepPublisher::publish (int step,
                             const std::array<std::uint8_t, State::kNumLanes>& velocities) noexcept
{
    // Relaxed: the word carries everything a reader needs, so there is nothing
    // else for it to be ordered against. The three atomics this replaced needed
    // release/acquire precisely because they were three.
    word.store (stepwire::encode (step, velocities), std::memory_order_relaxed);
    count.fetch_add (1, std::memory_order_relaxed);
}

void StepPublisher::publishStopped() noexcept
{
    // NOT counted: nothing was emitted. The count answers "how many steps
    // fired", and a stop that incremented it would make a stopped transport
    // look like a stuck one.
    word.store (0, std::memory_order_relaxed);
}

StepSnapshot StepPublisher::read() const noexcept
{
    return stepwire::decode (word.load (std::memory_order_relaxed));
}

} // namespace forrobox
