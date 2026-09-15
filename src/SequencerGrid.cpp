#include "SequencerGrid.h"

#include "PluginProcessor.h"
#include "Playhead.h"

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

int SequencerLayout::rowGap (int availableHeight) noexcept
{
    constexpr auto rows = ChassisLayout::kNumStrips;

    // What is left once the five pads have their fixed height, shared between
    // the four gaps. See seq::kDeclaredRowGap: the stylesheet's 7 does not fit,
    // and the pad height is the number that survives.
    const auto leftover = availableHeight - rows * pad::kHeight;

    return juce::jmax (0, leftover / (rows - 1));
}

juce::Rectangle<int> SequencerLayout::padBounds (juce::Rectangle<int> pads, int index,
                                                 int stepCount) noexcept
{
    // `repeat(N, 1fr)` with a gap — the same law that places the five channel
    // strips, now in one place rather than written out here too.
    return tileAcross (pads, index, stepCount, pad::kGap);
}

SequencerLayout SequencerLayout::forBounds (juce::Rectangle<int> bounds) noexcept
{
    SequencerLayout out;

    auto interior = bounds.reduced (seq::kPadSide, 0)
                        .withTrimmedTop (seq::kPadTop)
                        .withTrimmedBottom (seq::kPadBottom);

    // ── the head row: SEQUENCER + the isolate hint left, STEPS + 16/32 right ──
    const auto headHeight = flexRow (textBox (type::Style::sectionLabel),
                                     textBox (type::Style::seqHint),
                                     Button::heightOf (Button::Variant::base));

    out.head = interior.removeFromTop (headHeight);
    interior.removeFromTop (seq::kHeadMarginBottom);

    {
        auto row = out.head;

        // Right first: the two step buttons and their label are content-sized,
        // and the hint on the left takes what is left.
        const auto stepsWidth = Button::widthOf (Button::Variant::base, "32");

        out.steps32 = centredInRow (row, row.removeFromRight (stepsWidth)
                                             .withHeight (Button::heightOf (Button::Variant::base)));
        row.removeFromRight (seq::kStepsGap);
        out.steps16 = centredInRow (row, row.removeFromRight (stepsWidth)
                                             .withHeight (Button::heightOf (Button::Variant::base)));
        row.removeFromRight (seq::kStepsGap);

        const auto stepsLabelWidth = juce::roundToInt (
            type::trackedWidth (type::Style::seqHint, "STEPS"));

        out.stepsLabel = centredInRow (row, row.removeFromRight (stepsLabelWidth)
                                                .withHeight (textBox (type::Style::seqHint)));

        // Left: the section label, then the hint.
        const auto sectionWidth = juce::roundToInt (
            type::trackedWidth (type::Style::sectionLabel, "SEQUENCER"));

        out.sectionLabel = centredInRow (row, row.removeFromLeft (sectionWidth)
                                                  .withHeight (textBox (type::Style::sectionLabel)));
        row.removeFromLeft (seq::kHeadGap);

        const auto hintWidth = juce::roundToInt (
            type::trackedWidth (type::Style::seqHint, isolateHintText()));

        out.isolateHint = centredInRow (row, row.removeFromLeft (hintWidth)
                                                 .withHeight (textBox (type::Style::seqHint)));
    }

    // ── five rows, the gap derived from what the region left ─────────────────
    const auto gap = rowGap (interior.getHeight());

    for (int i = 0; i < ChassisLayout::kNumStrips; ++i)
    {
        auto& row = out.rows[static_cast<size_t> (i)];

        row.bounds = interior.removeFromTop (pad::kHeight);

        if (i < ChassisLayout::kNumStrips - 1)
            interior.removeFromTop (gap);

        auto cells = row.bounds;

        row.label = cells.removeFromLeft (seq::kLabelWidth);
        cells.removeFromLeft (seq::kLabelGap);
        row.pads = cells;

        auto label = row.label;

        row.chip = centredInRow (label, label.removeFromLeft (seq::kChipWidth)
                                             .withHeight (seq::kChipHeight));
        label.removeFromLeft (seq::kChipGap);
        row.name = centredInRow (label, label.withHeight (
                                            textBox (type::Style::sequencerRowLabel)));
    }

    return out;
}

const juce::String& isolateHintText()
{
    // app.js:319, in Brazilian Portuguese as every instructional string is.
    static const juce::String text { juce::CharPointer_UTF8 ("CLIQUE O NOME P/ ISOLAR") };
    return text;
}

SequencerGrid::SequencerGrid (ForroBoxLookAndFeel& lookAndFeelToUse)
    : lnf (lookAndFeelToUse)
{
    setOpaque (true);
}

SequencerGrid::~SequencerGrid() = default;

void SequencerGrid::attachParameters (juce::AudioProcessorValueTreeState& state)
{
    apvts = &state;
    processor = dynamic_cast<::ForroBoxAudioProcessor*> (&state.processor);

    rebuildPads();

    // AFTER the pads, so it is the last child and paints above them — the
    // prototype's `z-index: 5` (css:489). Added once; `rebuildPads` clears only
    // the pads.
    if (playhead == nullptr)
    {
        playhead = std::make_unique<Playhead>();
        addAndMakeVisible (*playhead);
    }

    playhead->toBehind (nullptr);   // front-most among this grid's children

    // ── the STEPS buttons (05-03) ───────────────────────────────────────────
    //
    // 05-01 reserved their boxes; no plan had claimed them until now. In
    // ids::stepWindows order, which IS the parameter's choice order — passing
    // them in visual order would let a reordered layout silently re-map the
    // parameter.
    if (stepButtons.front() == nullptr)
    {
        for (size_t i = 0; i < stepButtons.size(); ++i)
        {
            stepButtons[i] = std::make_unique<Button> (
                lnf, Button::Variant::base,
                juce::String (forrobox::ids::stepWindows[i]));

            addAndMakeVisible (*stepButtons[i]);
        }
    }

    if (auto* stepsParameter = dynamic_cast<juce::RangedAudioParameter*> (
                                   state.getParameter (ids::steps)))
    {
        std::vector<Button*> buttons;

        for (auto& button : stepButtons)
            buttons.push_back (button.get());

        // RESET FIRST. `unique_ptr::operator=` destroys the old attachment only
        // AFTER the new one is fully constructed — and the buttons are not
        // rebuilt, so the old one's guards would then null the `onClick` the new
        // one had just installed. The buttons would still light correctly, from
        // the new attachment's parameter half, and do nothing when clicked.
        //
        // Exactly the hazard ScopedControlCallbacks.h warns about: "the second
        // clear would run after whatever re-installed the callbacks". Every
        // other owner avoids it by clearing controls and attachments together
        // (`header = {}`, `controls = {}`); this one holds its buttons across a
        // re-attach, so it has to say so. Found by /code-review.
        stepsAttachment.reset();

        stepsAttachment = std::make_unique<ChoiceButtonsAttachment> (*stepsParameter,
                                                                     std::move (buttons));
    }

    resized();
    refreshFromState();

    // One tick, two jobs. The grid already polls at 60 Hz for the playhead, so
    // following the state is a second edge-detect on the same timer rather than
    // a second timer.
    playheadPoll.tick = [this] { refreshIfStateChanged(); updatePlayhead(); };
    playheadPoll.startTimerHz (seq::kPlayheadPollHz);

    updatePlayhead();
}

juce::Rectangle<int> SequencerGrid::rowsArea() const noexcept
{
    const auto first = layout.rows.front().pads;
    const auto last  = layout.rows.back().pads;

    return juce::Rectangle<int>::leftTopRightBottom (first.getX(), first.getY(),
                                                     first.getRight(), last.getBottom());
}

juce::Rectangle<int> SequencerGrid::playheadBounds() const noexcept
{
    if (playhead == nullptr || ! playhead->isVisible())
        return {};

    return playhead->getBounds();
}

void SequencerGrid::updatePlayhead()
{
    if (playhead == nullptr)
        return;

    // Hidden, not frozen. `.playhead { opacity: 0 }` and `.playhead.on
    // { opacity: 1 }` (css:490, 492) — a stopped transport leaves no line at
    // all rather than one parked wherever the groove happened to stop.
    if (processor == nullptr || processor->isTransportStopped())
    {
        playhead->setVisible (false);
        return;
    }

    const auto strip = layout.rows.front().pads;

    if (strip.isEmpty() || stepCount <= 0)
    {
        playhead->setVisible (false);
        return;
    }

    const auto centre = Playhead::lineCentreFor (processor->getDisplayPositionInSteps(),
                                                 strip, stepCount);

    playhead->setBounds (Playhead::boundsForLineAt (centre, rowsArea()));
    playhead->setVisible (true);
}

void SequencerGrid::rebuildPads()
{
    // Idempotent. Appending would stack a second set of pads invisibly over the
    // first and let a second call index the layout past its five rows — the
    // shape `Chassis::attachParameters` records.
    pads.clear();

    stepCount = readStepCount();

    pads.reserve (static_cast<size_t> (ChassisLayout::kNumStrips * stepCount));

    for (int row = 0; row < ChassisLayout::kNumStrips; ++row)
    {
        // The same accent binding the strips and the row chips use, protected by
        // the static_assert at the top of Chassis.h.
        const auto colour = theme::accent (static_cast<theme::Accent> (row));

        for (int step = 0; step < stepCount; ++step)
        {
            auto pad = std::make_unique<StepPad> (lnf, colour);

            // Every fourth step is a beat marker — app.js:353.
            pad->setBeat (step % 4 == 0);
            pad->onClick = [this, row, step] { toggleCell (row, step); };

            addAndMakeVisible (*pad);
            pads.push_back (std::move (pad));
        }
    }
}

StepPad* SequencerGrid::padFor (int row, int step) const
{
    if (! juce::isPositiveAndBelow (row, ChassisLayout::kNumStrips)
        || ! juce::isPositiveAndBelow (step, stepCount))
        return nullptr;

    const auto index = static_cast<size_t> (row * stepCount + step);

    return index < pads.size() ? pads[index].get() : nullptr;
}

int SequencerGrid::readStepCount() const
{
    // ONE reader. `rebuildPads` used to inline this and the poll would have
    // needed its own copy — two readings of one parameter, able to disagree
    // about how many pads there should be.
    if (apvts != nullptr)
        if (const auto* steps = apvts->getRawParameterValue (ids::steps))
            return ForroBoxAudioProcessor::stepsForChoiceIndex (
                juce::roundToInt (steps->load (std::memory_order_relaxed)));

    return forrobox::ids::stepWindows.front();
}

void SequencerGrid::refreshIfStateChanged()
{
    if (processor == nullptr)
        return;

    // The STEP WINDOW first: it changes how many pads there are, so a velocity
    // refresh against the old count would leave 16 pads showing a 32-step
    // window. `/graphify` found the prototype does the same — `setSteps` calls
    // `renderPads()`, not just a repaint (app.js:588).
    if (const auto steps = readStepCount(); steps != lastStepCountSeen)
    {
        rebuildPads();
        resized();
        refreshFromState();
        return;
    }

    // Then the pattern. `publishIfChanged` increments this when the lanes
    // DIFFER and not otherwise, and `~LockedState` calls it for every writer —
    // so this sees a host recall, a profile load and the grid's own click, and
    // does not see the reads the handle is also taken for.
    if (processor->getPatternPublicationCount() != lastPatternGeneration)
        refreshFromState();
}

void SequencerGrid::refreshFromState()
{
    if (processor == nullptr)
        return;

    // Read ONCE per refresh, not once per pad: taking the state handle 160 times
    // would take its lock 160 times, and the handle publishes on destruction.
    // The generation is read INSIDE the lock, with the snapshot it belongs to.
    //
    // It was read at the end, after the pads were painted. `publishIfChanged`
    // runs inside `~LockedState` while the lock is still held, so a host
    // recalling a project in the window between the copy and the record made the
    // grid paint the OLD pattern and record the NEW generation — and the next
    // tick then saw no change and never refreshed. The 05-01 bug, re-introduced
    // in a narrower window. Found by /code-review.
    //
    // Reading it early can only ever cause one harmless extra refresh.
    std::uint32_t generation = 0;

    const auto snapshot = [this, &generation]
    {
        auto handle = processor->lockPatternState();
        generation = processor->getPatternPublicationCount();
        return *handle;
    }();

    // The lane cover is per ROW, so the loop is nested — the derivation happens
    // once per row because of where it SITS, not because it was memoised into an
    // array the flat list then needed a bounds guard to protect.
    for (int row = 0; row < ChassisLayout::kNumStrips; ++row)
    {
        const auto& covered = lanesForRow (row);

        for (int step = 0; step < stepCount; ++step)
            if (auto* pad = padFor (row, step))
                pad->setVelocity (displayedVelocity (snapshot, covered, step));
    }

    // What this grid is now showing. Recorded HERE rather than in the poll, so
    // the refresh `toggleCell` does for itself counts too and its own edit does
    // not come back around a frame later as a second refresh.
    lastPatternGeneration = generation;
    lastStepCountSeen = stepCount;
}

void SequencerGrid::toggleCell (int row, int step)
{
    if (processor == nullptr)
        return;

    const auto lane = writeLaneForRow (row);

    if (! juce::isPositiveAndBelow (lane, State::kNumLanes)
        || ! juce::isPositiveAndBelow (step, State::kMaxSteps))
        return;

    {
        auto handle = processor->lockPatternState();

        auto& slot = handle->lanes[static_cast<size_t> (lane)][static_cast<size_t> (step)];

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

void SequencerGrid::resized()
{
    layout = SequencerLayout::forBounds (getLocalBounds());

    {
        const std::array<juce::Rectangle<int>, 2> boxes { layout.steps16, layout.steps32 };

        for (size_t i = 0; i < stepButtons.size() && i < boxes.size(); ++i)
            if (stepButtons[i] != nullptr)
                stepButtons[i]->setBounds (boxes[i]);
    }

    // The sweep's geometry comes from the pad strip, so a re-layout moves it —
    // `/graphify` found the prototype does the same, `scale() -> layoutPlayhead()`
    // (app.js:736) and `renderPads() -> layoutPlayhead()`.
    updatePlayhead();

    for (int row = 0; row < ChassisLayout::kNumStrips; ++row)
    {
        // Once per row, not once per pad.
        const auto strip = layout.rows[static_cast<size_t> (row)].pads;

        for (int step = 0; step < stepCount; ++step)
            if (auto* pad = padFor (row, step))
            {
                const auto cell = SequencerLayout::padBounds (strip, step, stepCount);

                // The pad ASKS for the bounds its reserved cell needs: a lit
                // pad's glow falls outside its box, and a Component's paint is
                // clipped to its own.
                pad->setBounds (StepPad::boundsForPadRect (cell));
            }
    }
}

void SequencerGrid::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds();

    g.setColour (lnf.token (theme::Token::sunken));
    g.fillRect (area);

    const auto shadows = lnf.shadows();
    surface::wellShadow (g, area, shadows.wellShadow, shadows.wellRadius);

    g.setColour (lnf.token (theme::Token::line));
    g.fillRect (area.getX(), area.getY(), area.getWidth(), 1);

    const auto clip = g.getClipBounds();

    paintHeadRow (g, clip);
    paintRowLabels (g, clip);
}

void SequencerGrid::paintHeadRow (juce::Graphics& g, juce::Rectangle<int> clip) const
{
    g.setColour (lnf.token (theme::Token::fgFaint));

    if (layout.sectionLabel.intersects (clip))
        type::drawTracked (g, type::Style::sectionLabel, "SEQUENCER",
                           layout.sectionLabel.toFloat(), juce::Justification::centredLeft);

    if (layout.isolateHint.intersects (clip))
        type::drawTracked (g, type::Style::seqHint, isolateHintText(),
                           layout.isolateHint.toFloat(), juce::Justification::centredLeft);

    if (layout.stepsLabel.intersects (clip))
        type::drawTracked (g, type::Style::seqHint, "STEPS",
                           layout.stepsLabel.toFloat(), juce::Justification::centredLeft);
}

void SequencerGrid::paintRowLabels (juce::Graphics& g, juce::Rectangle<int> clip) const
{
    for (int i = 0; i < ChassisLayout::kNumStrips; ++i)
    {
        const auto& row = layout.rows[static_cast<size_t> (i)];

        if (! row.label.intersects (clip))
            continue;

        // The same accent binding the strips use, protected by the static_assert
        // at the top of Chassis.h.
        g.setColour (theme::accent (static_cast<theme::Accent> (i)));
        g.fillRoundedRectangle (row.chip.toFloat(), static_cast<float> (seq::kChipRadius));

        // `--fg-dim` at rest; 05-03's isolate raises it to `--fg`.
        g.setColour (lnf.token (theme::Token::fgDim));
        type::drawTracked (g, type::Style::sequencerRowLabel,
                           juce::String (juce::CharPointer_UTF8 (
                               ids::channelInfos[static_cast<size_t> (i)].displayName)),
                           row.name.toFloat(), juce::Justification::centredLeft);
    }
}

} // namespace forrobox
