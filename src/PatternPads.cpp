#include "PatternPads.h"

#include "PluginProcessor.h"
#include "SequencerGrid.h"

namespace forrobox
{

PatternPads::PatternPads (ForroBoxLookAndFeel& lookAndFeelToUse, juce::Component& hostToUse)
    : lnf (lookAndFeelToUse), host (hostToUse)
{
}

void PatternPads::setProcessor (::ForroBoxAudioProcessor* processorToUse)
{
    processor = processorToUse;
}

void PatternPads::setRows (std::vector<PatternRow> table)
{
    rows = std::move (table);
    rebuild();
}

void PatternPads::rebuild()
{
    pads.clear();

    stepCount = readStepWindow (processor);

    pads.reserve (rows.size() * static_cast<size_t> (stepCount));

    for (int row = 0; row < getNumRows(); ++row)
    {
        const auto colour = rows[static_cast<size_t> (row)].colour;

        for (int step = 0; step < stepCount; ++step)
        {
            auto pad = std::make_unique<StepPad> (lnf, colour);

            // Every fourth step is a beat marker — app.js:353.
            pad->setBeat (step % 4 == 0);
            pad->onClick = [this, row, step] { toggle (row, step); };

            if (onPadCreated != nullptr)
                onPadCreated (*pad, row, step);

            host.addAndMakeVisible (*pad);
            pads.push_back (std::move (pad));
        }
    }
}

StepPad* PatternPads::padFor (int row, int step) const
{
    if (! juce::isPositiveAndBelow (row, getNumRows())
        || ! juce::isPositiveAndBelow (step, stepCount))
        return nullptr;

    const auto index = static_cast<size_t> (row) * static_cast<size_t> (stepCount)
                     + static_cast<size_t> (step);

    return index < pads.size() ? pads[index].get() : nullptr;
}

void PatternPads::refreshFromState()
{
    if (processor == nullptr)
        return;

    // Read ONCE per refresh, not once per pad: taking the state handle 160 times
    // would take its lock 160 times, and the handle publishes on destruction.
    // The generation comes out from INSIDE the lock with the snapshot it belongs
    // to — see `snapshotPattern` for the bug that ordering exists to prevent.
    std::uint32_t generation = 0;
    const auto snapshot = snapshotPattern (*processor, generation);

    for (int row = 0; row < getNumRows(); ++row)
    {
        const auto& covered = rows[static_cast<size_t> (row)].read;

        for (int step = 0; step < stepCount; ++step)
            if (auto* pad = padFor (row, step))
                pad->setVelocity (displayedVelocity (snapshot, covered, step));
    }

    // What this view is now showing. Recorded HERE rather than in a caller's
    // poll, so the refresh `toggle` does for itself counts too.
    lastPatternGeneration = generation;
}

void PatternPads::refreshIfStateChanged()
{
    if (processor == nullptr)
        return;

    if (readStepWindow (processor) != stepCount)
    {
        rebuild();

        if (onRebuilt != nullptr)
            onRebuilt();

        refreshFromState();
        return;
    }

    if (processor->getPatternPublicationCount() != lastPatternGeneration)
        refreshFromState();
}

void PatternPads::toggle (int row, int step)
{
    if (processor == nullptr || ! juce::isPositiveAndBelow (row, getNumRows()))
        return;

    const auto lane = rows[static_cast<size_t> (row)].write;

    if (! juce::isPositiveAndBelow (lane, State::kNumLanes)
        || ! juce::isPositiveAndBelow (step, State::kMaxSteps))
        return;

    {
        auto handle = processor->lockPatternState();

        auto& slot = handle->lanes[static_cast<size_t> (lane)][static_cast<size_t> (step)];

        // ONE toggle velocity for every view. `seq::kToggleOnVelocity` is pinned
        // to app.js:392, and two editors writing two different "on" values would
        // be a groove that changed depending on which one you used.
        slot = static_cast<std::uint8_t> (slot > 0 ? seq::kToggleOffVelocity
                                                   : seq::kToggleOnVelocity);

        // An edited pattern no longer matches the profile it came from.
        // `togglePad` calls `markCustom` in the prototype — the dirty flag fired
        // from every control's onChange.
        handle->dirty = true;

        // The handle publishes to the audio thread on destruction, which is what
        // makes "every writer must remember" not an invariant anyone can forget.
    }

    refreshFromState();
}

} // namespace forrobox
