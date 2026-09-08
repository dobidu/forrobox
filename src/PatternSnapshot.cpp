#include "PatternSnapshot.h"

namespace forrobox
{

bool PatternPublisher::publishIfChanged (const PatternLanes& lanes)
{
    const juce::SpinLock::ScopedLockType held (lock);

    if (lanes == lastPublished)
        return false;

    staging = lanes;
    lastPublished = lanes;
    generation.fetch_add (1, std::memory_order_release);
    return true;
}

bool PatternReader::refresh (PatternPublisher& publisher) noexcept
{
    // TRY, never enter: tryEnter is a single compare-exchange and this runs on
    // the audio thread, where waiting is a dropout. A failure costs one block of
    // staleness — the previous snapshot stays valid and usable.
    const juce::SpinLock::ScopedTryLockType lock (publisher.lock);

    if (! lock.isLocked())
        return false;

    const auto generation = publisher.generation.load (std::memory_order_acquire);

    if (generation == held.load (std::memory_order_relaxed))
        return false;   // nothing new — the common case, and it copies nothing

    snapshot = publisher.staging;
    held.store (generation, std::memory_order_relaxed);
    copies.fetch_add (1, std::memory_order_relaxed);
    return true;
}

std::uint8_t PatternReader::velocityAt (int lane, int storageIndex) const noexcept
{
    if (! juce::isPositiveAndBelow (lane, State::kNumLanes)
        || ! juce::isPositiveAndBelow (storageIndex, State::kMaxSteps))
        return 0;

    return snapshot[static_cast<size_t> (lane)][static_cast<size_t> (storageIndex)];
}

} // namespace forrobox
