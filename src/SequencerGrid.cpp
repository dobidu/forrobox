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

const juce::String& isolateHintText()
{
    // app.js:319, in Brazilian Portuguese as every instructional string is.
    static const juce::String text { juce::CharPointer_UTF8 ("CLIQUE O NOME P/ ISOLAR") };
    return text;
}

SequencerGrid::SequencerGrid (ForroBoxLookAndFeel& lookAndFeelToUse)
    : lnf (lookAndFeelToUse), padGrid (lookAndFeelToUse, *this)
{
    setOpaque (true);
}

SequencerGrid::~SequencerGrid() = default;

void SequencerGrid::attachParameters (juce::AudioProcessorValueTreeState& state)
{
    apvts = &state;
    processor = dynamic_cast<::ForroBoxAudioProcessor*> (&state.processor);

    padGrid.setProcessor (processor);

    // The row's CURRENT dim, not the default: a rebuild happens on a STEPS
    // change, which can land while a row is muted or another is isolated, and a
    // fresh pad at full opacity would undim half a row until something else
    // changed. `refreshRowStates` edge-detects, so it would not put it back.
    padGrid.onPadCreated = [this] (StepPad& pad, int row, int)
    {
        pad.setDimmed (rowDimmed[static_cast<size_t> (row)]);
    };

    // A rebuild replaces every pad, and a fresh pad has no bounds until this
    // grid gives it some.
    padGrid.onRebuilt = [this] { resized(); };

    padGrid.setRows (rowTable());

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
    playheadPoll.tick = [this]
    {
        refreshIfStateChanged();
        refreshRowStates();
        updatePlayhead();
    };
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

    if (strip.isEmpty() || getStepCount() <= 0)
    {
        playhead->setVisible (false);
        return;
    }

    const auto centre = Playhead::lineCentreFor (processor->getDisplayPositionInSteps(),
                                                 strip, getStepCount());

    playhead->setBounds (Playhead::boundsForLineAt (centre, rowsArea()));
    playhead->setVisible (true);
}



int SequencerGrid::readStepCount() const
{
    // THE PROCESSOR's reader, not a second one. This read the raw parameter
    // itself with a fallback of `ids::stepWindows.front()` while the processor's
    // three copies fell back to `stepsForChoiceIndex (0)` — the same value only
    // because `stepWindows[0] == front()`, which nothing said. Found by
    // /simplify.
    return readStepWindow (processor);
}

void SequencerGrid::refreshIfStateChanged() { padGrid.refreshIfStateChanged(); }

bool SequencerGrid::isRowDimmed (int row) const
{
    return juce::isPositiveAndBelow (row, ChassisLayout::kNumStrips)
           && rowDimmed[static_cast<size_t> (row)];
}

void SequencerGrid::setIsolatedRow (int row)
{
    const auto wanted = juce::isPositiveAndBelow (row, ChassisLayout::kNumStrips) ? row : -1;

    if (isolatedRow == wanted)
        return;

    isolatedRow = wanted;

    // The label colours change on the isolate alone, whether or not any row's
    // DIM changed — `css:460` raises the isolated row's label to `--fg` and the
    // row it was taken from drops back to `--fg-dim`.
    for (const auto& row_ : layout.rows)
        repaint (row_.label);

    refreshRowStates();
}

void SequencerGrid::refreshRowStates()
{
    // Resolved ONCE per poll, not once per row, and through the processor's own
    // resolver rather than by reading `mute` and `solo` here: solo's precedence
    // is decided across all five channels, and a second copy of that rule in the
    // UI would disagree with the engine the first time it changed. The same
    // reason `Chassis::pollVisualisers` gives for the LEDs.
    //
    // No processor means no gate to read — a headless grid still isolates, which
    // is what the tests drive.
    const auto settings = processor != nullptr
                            ? processor->resolveChannelSettings()
                            : VoiceEngine::Settings {};

    for (int row = 0; row < ChassisLayout::kNumStrips; ++row)
    {
        const auto index = static_cast<size_t> (row);

        // EITHER, not both — `app.js:518`. An isolated row that is also muted
        // stays dimmed, because it is still silent.
        const auto silenced = processor != nullptr && ! settings.channels[index].audible;
        const auto notFocused = isolatedRow >= 0 && row != isolatedRow;
        const auto dim = silenced || notFocused;

        if (dim == rowDimmed[index])
            continue;

        rowDimmed[index] = dim;

        // The pads dim THEMSELVES, one component alpha each. A translucent
        // rectangle painted over the row would be simpler and wrong: the
        // playhead is a sibling that sweeps across all five rows, and a scrim
        // over one row would dim the part of the line crossing it.
        for (int step = 0; step < getStepCount(); ++step)
            if (auto* pad = padFor (row, step))
                pad->setDimmed (dim);

        repaint (layout.rows[index].label);
    }
}

int SequencerGrid::rowLabelAt (juce::Point<int> position) const
{
    for (int row = 0; row < ChassisLayout::kNumStrips; ++row)
        if (layout.rows[static_cast<size_t> (row)].label.contains (position))
            return row;

    return -1;
}

void SequencerGrid::mouseUp (const juce::MouseEvent& event)
{
    const auto row = rowLabelAt (event.getPosition());

    if (row < 0)
        return;

    // Clicking the isolated row's own label clears it — app.js:509.
    setIsolatedRow (row == isolatedRow ? -1 : row);
}

void SequencerGrid::setHoveredLabelRow (int row)
{
    if (row == hoveredLabelRow)
        return;

    const auto previous = hoveredLabelRow;
    hoveredLabelRow = row;

    // `.seq-rowlabel:hover { color: var(--fg) }` — css:458, and the cursor says
    // the name is clickable, which is what the head row's hint promises.
    setMouseCursor (row >= 0 ? juce::MouseCursor::PointingHandCursor
                             : juce::MouseCursor::NormalCursor);

    for (const auto candidate : { previous, row })
        if (candidate >= 0)
            repaint (layout.rows[static_cast<size_t> (candidate)].label);
}

// One body, two entries. `mouseExit` was the whole of `mouseMove` again with the
// row pinned to -1 — compare, save the previous, store, set the cursor, repaint
// both. /simplify.
void SequencerGrid::mouseMove (const juce::MouseEvent& event)
{
    setHoveredLabelRow (rowLabelAt (event.getPosition()));
}

void SequencerGrid::mouseExit (const juce::MouseEvent&) { setHoveredLabelRow (-1); }

void SequencerGrid::refreshFromState() { padGrid.refreshFromState(); }

std::vector<PatternRow> SequencerGrid::rowTable() const
{
    std::vector<PatternRow> table;

    for (int row = 0; row < ChassisLayout::kNumStrips; ++row)
        table.push_back ({ lanesForRow (row), writeLaneForRow (row),
                           // The same accent binding the strips and the row chips
                           // use, protected by the static_assert at the top of
                           // Chassis.h.
                           theme::accent (static_cast<theme::Accent> (row)) });

    return table;
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

        for (int step = 0; step < getStepCount(); ++step)
            if (auto* pad = padFor (row, step))
            {
                const auto cell = SequencerLayout::padBounds (strip, step, getStepCount());

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

        // `.seq-row.dimmed { opacity: 0.32 }` — css:461 dims the WHOLE row, chip
        // and name with it, so this is a factor on both colours rather than a
        // second alpha on one of them. The pads in the same row carry the same
        // number as a component alpha; see StepPad::setDimmed for why they are
        // not covered by a scrim from here.
        const auto rowAlpha = rowDimmed[static_cast<size_t> (i)] ? pad::kDimmedAlpha : 1.0f;

        // The same accent binding the strips use, protected by the static_assert
        // at the top of Chassis.h.
        g.setColour (theme::accent (static_cast<theme::Accent> (i))
                         .withMultipliedAlpha (rowAlpha));
        g.fillRoundedRectangle (row.chip.toFloat(), static_cast<float> (seq::kChipRadius));

        // `--fg-dim` at rest; `--fg` when isolated (css:460) or hovered
        // (css:458). Two rules, one colour: both name `var(--fg)`.
        const auto lit = i == isolatedRow || i == hoveredLabelRow;

        g.setColour (lnf.token (lit ? theme::Token::fg : theme::Token::fgDim)
                         .withMultipliedAlpha (rowAlpha));
        type::drawTracked (g, type::Style::sequencerRowLabel,
                           juce::String (juce::CharPointer_UTF8 (
                               ids::channelInfos[static_cast<size_t> (i)].displayName)),
                           row.name.toFloat(), juce::Justification::centredLeft);
    }
}

} // namespace forrobox
