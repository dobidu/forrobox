/* ============================================================================
   FORRÓ BOX — the audio-thread publication shapes that need more than a store

   Most of what the audio thread publishes to the UI is a LAST VALUE: one store,
   one load, and the reader is welcome to miss an intermediate. Eighteen of this
   plugin's atomics are that shape and need nothing from this file.

   Two are not. A running MAXIMUM cannot be published with a load followed by a
   store: that is a read-modify-write spelled as two operations, and anything
   that writes the cell in between is lost or resurrected. `MixBus`'s gain
   reduction found this the hard way at 04-05 — the footer's meter `exchange`s
   the cell to zero, and a store landing after that read put the consumed peak
   back and pinned the meter lit for another frame.

   So the law is written ONCE, here, rather than at each site with its own
   twenty lines of reasoning. `VoiceEngine`'s peak voice count is the second
   instance and had the same bug, uncommented; Phase 5's hit visualisers are the
   third by nature.
============================================================================ */
#pragma once

#include <atomic>

namespace forrobox
{

/** Raise an atomic to `value` if `value` is greater — one atomic operation.

    Allocation-free, lock-free and wait-free enough for an audio callback: with
    a single writer a compare-exchange can only lose to a reader clearing the
    cell, so the loop runs at most once more in practice. `compare_exchange_weak`
    updates `previous` for us on failure, so nothing is re-read.

    Measured at 04-05 against the alternative it replaces: 4.5 ns per audio
    block where the per-sample `std::tanh` calls above it cost 2.26 us — 0.2% of
    one line of the loop it guards. */
template <typename T>
inline void atomicMax (std::atomic<T>& cell, T value,
                       std::memory_order order = std::memory_order_relaxed) noexcept
{
    auto previous = cell.load (order);

    while (previous < value && ! cell.compare_exchange_weak (previous, value, order, order))
    {
        // `previous` now holds whatever the other thread left behind; the
        // condition re-tests it. A reader that zeroed the cell makes the next
        // attempt succeed.
    }
}

} // namespace forrobox
