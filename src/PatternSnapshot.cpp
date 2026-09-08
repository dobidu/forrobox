#include "PatternSnapshot.h"

namespace forrobox
{

void PatternPublisher::publish (const PatternLanes& lanes) noexcept
{
    // Odd generation first, and fenced, so a reader cannot observe the previous
    // EVEN generation on both sides of a read that overlapped these writes. That
    // is the whole job of the odd marker: without the fence the byte stores could
    // become visible before it, and a reader would verify a torn table against an
    // unchanged generation and accept it.
    const auto next = generation.load (std::memory_order_relaxed) + 1;
    generation.store (next, std::memory_order_relaxed);
    std::atomic_thread_fence (std::memory_order_release);

    size_t byte = 0;
    for (const auto& lane : lanes)
        for (auto velocity : lane)
            staging[byte++].store (velocity, std::memory_order_relaxed);

    // Release, so a reader that acquires this even value sees every byte above.
    generation.store (next + 1, std::memory_order_release);
    publications.fetch_add (1, std::memory_order_relaxed);

    // Maintained here, not only in publishIfChanged, so the two cannot disagree
    // about what is current.
    lastPublished = lanes;
    havePublished = true;
}

bool PatternPublisher::publishIfChanged (const PatternLanes& lanes) noexcept
{
    if (havePublished && lanes == lastPublished)
        return false;

    publish (lanes);
    return true;
}

bool PatternReader::refresh (const PatternPublisher& publisher) noexcept
{
    const auto before = publisher.generation.load (std::memory_order_acquire);

    if ((before & 1u) != 0u)
        return false;   // a write is in progress; keep what we have

    if (before == held.load (std::memory_order_relaxed))
        return false;   // nothing new — the common case, and it copies nothing

    auto& candidate = buffers[static_cast<size_t> (1 - active)];

    size_t byte = 0;
    for (auto& lane : candidate)
        for (auto& velocity : lane)
            velocity = publisher.staging[byte++].load (std::memory_order_relaxed);

    // Pairs with the writer's release: everything read above happened before
    // this check, so a generation still equal to `before` means the table we
    // copied was complete and unchanged throughout.
    std::atomic_thread_fence (std::memory_order_acquire);

    if (publisher.generation.load (std::memory_order_relaxed) != before)
        return false;   // the writer moved under us; keep the previous snapshot

    active = 1 - active;
    held.store (before, std::memory_order_relaxed);
    copies.fetch_add (1, std::memory_order_relaxed);
    return true;
}

std::uint8_t PatternReader::velocityAt (int lane, int storageIndex) const noexcept
{
    if (! juce::isPositiveAndBelow (lane, State::kNumLanes)
        || ! juce::isPositiveAndBelow (storageIndex, State::kMaxSteps))
        return 0;

    return buffers[static_cast<size_t> (active)][static_cast<size_t> (lane)]
                  [static_cast<size_t> (storageIndex)];
}

} // namespace forrobox
