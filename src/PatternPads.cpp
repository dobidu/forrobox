#include "PatternPads.h"

#include "PluginProcessor.h"

namespace forrobox
{

const LaneSet& lanesForRow (int channelIndex)
{
    static constexpr LaneSet none {};

    return juce::isPositiveAndBelow (channelIndex, static_cast<int> (detail::channelToLanes.size()))
             ? detail::channelToLanes[static_cast<size_t> (channelIndex)]
             : none;
}

int writeLaneForRow (int channelIndex)
{
    const auto& covered = lanesForRow (channelIndex);

    // A row covering exactly one lane writes that lane.
    if (covered.size() == 1)
        return covered.front();

    // The composite row writes CAIXA, found by NAME and at COMPILE time —
    // `detail::compositeEditLane`, beside the `ghostingKitLane` this used to
    // say it worked like while actually hand-rolling a std::strcmp loop with a
    // runtime assert and a fallback. An index would silently point at another
    // instrument the day `ids::lanes` is reordered; a missing "cx" now fails to
    // build rather than asserting in a debug session.
    return covered.empty() ? -1 : detail::compositeEditLane();
}

int displayedVelocity (const State& state, const LaneSet& covered, int step)
{
    auto loudest = 0;

    for (const auto lane : covered)
    {
        if (! juce::isPositiveAndBelow (lane, State::kNumLanes)
            || ! juce::isPositiveAndBelow (step, State::kMaxSteps))
            continue;

        loudest = juce::jmax (loudest,
                              static_cast<int> (state.lanes[static_cast<size_t> (lane)]
                                                          [static_cast<size_t> (step)]));
    }

    return loudest;
}

int readStepWindow (const ::ForroBoxAudioProcessor* processor)
{
    return processor != nullptr ? processor->currentStepWindow()
                                : forrobox::ids::stepWindows.front();
}

State snapshotPattern (::ForroBoxAudioProcessor& processor, std::uint32_t& generation)
{
    auto handle = processor.lockPatternState();

    generation = processor.getPatternPublicationCount();

    return *handle;
}

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
            // EXACTLY 16 bytes of capture, which is libstdc++'s std::function
            // small-buffer limit — measured at 0 allocations here and 1 per pad
            // at 24. One more captured word (a lane, a view id) would silently
            // put 160 heap allocations into every rebuild. /simplify measured
            // the cliff rather than assuming it.
            pad->onClick = [this, row, step] { toggle (row, step); };

            host.addAndMakeVisible (*pad);
            pads.push_back (std::move (pad));
        }
    }

    // THE PADS ARE THE BACKGROUND LAYER, in both hosts, and a rebuild must not
    // change that. `addAndMakeVisible` appends to the host's child list, so a
    // rebuild silently re-ordered the HOST — which is how a STEPS change buried
    // the sequencer's playhead under 162 pads, and would bury the kit panel's
    // close button next. Sending them back here makes the rebuild z-order
    // neutral, so no future sibling has to declare itself always-on-top to
    // survive one. In reverse, so the pads keep their own order. /simplify.
    for (auto it = pads.rbegin(); it != pads.rend(); ++it)
        (*it)->toBack();

    if (onRebuilt != nullptr)
        onRebuilt();
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

void PatternPads::flashLitPads()
{
    for (auto& pad : pads)
        pad->flash();   // `StepPad::flash` ignores an unlit pad
}

void PatternPads::advanceFlash (double seconds) noexcept
{
    for (auto& pad : pads)
        pad->advanceFlash (seconds);
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
