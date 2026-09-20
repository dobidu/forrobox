#include "SequencerGrid.h"

#include "PluginProcessor.h"
#include "Playhead.h"

namespace forrobox
{
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
    : lnf (lookAndFeelToUse), padGrid (lookAndFeelToUse, *this)
{
    setOpaque (true);

    // One zone per row label. `resized` gives them their boxes; until then they
    // are empty and hit nothing, which is the same answer the old
    // `rowLabelAt` gave against an unlaid-out `layout`.
    for (int row = 0; row < ChassisLayout::kNumStrips; ++row)
    {
        auto& zone = labelZones[static_cast<size_t> (row)];

        // EXACTLY 16 BYTES of capture each, both lambdas — libstdc++'s
        // std::function small-buffer limit, which PatternPads.cpp:101 records
        // after /simplify measured the cliff there. One more captured word in
        // either would put five heap allocations into this constructor.
        // Measured again at 06-05: 16 and 16, zero allocations.
        //
        // Clicking the isolated row's own label CLEARS it — app.js:509.
        zone.onClick = [this, row] { setIsolatedRow (row == isolatedRow ? -1 : row); };

        // `.seq-rowlabel:hover { color: var(--fg) }` — css:458. Repaint only;
        // the zone IS the hover state, so nothing here mirrors it.
        //
        // The first version kept a `hoveredLabelRow` member and a
        // previous/current pair to repaint both. The zones are disjoint
        // siblings, so `previous` was always either this row or -1 and the set
        // to repaint was always this row alone — unreachable bookkeeping under
        // a doc comment claiming the zones already answered the question, which
        // they did not until now. /simplify.
        zone.onHoverChanged = [this, row] (bool) { repaint (layout.rows[static_cast<size_t> (row)].label); };

        addAndMakeVisible (zone);
    }
}

SequencerGrid::~SequencerGrid() = default;

void SequencerGrid::attachParameters (juce::AudioProcessorValueTreeState& state)
{
    apvts = &state;
    processor = dynamic_cast<::ForroBoxAudioProcessor*> (&state.processor);

    padGrid.setProcessor (processor);

    // A rebuild replaces every pad, and a fresh pad has neither bounds nor the
    // row's CURRENT dim — a STEPS change can land while a channel is muted or
    // another row is isolated, and `refreshRowStates` edge-detects, so it would
    // not put the dimming back.
    padGrid.onRebuilt = [this]
    {
        resized();

        for (int row = 0; row < ChassisLayout::kNumStrips; ++row)
            for (int step = 0; step < getStepCount(); ++step)
                if (auto* pad = padFor (row, step))
                    pad->setDimmed (rowDimmed[static_cast<size_t> (row)]);
    };

    padGrid.setRows (rowTable());

    // AFTER the pads, so it is the last child and paints above them — the
    // prototype's `z-index: 5` (css:489). Added once; `rebuildPads` clears only
    // the pads.
    if (playhead == nullptr)
    {
        playhead = std::make_unique<Playhead>();
        addAndMakeVisible (*playhead);
    }

    // No z-order call at all, and that is the point.
    //
    // It was `toBehind (nullptr)`, which made the playhead front-most ONCE:
    // `PatternPads::rebuild` destroys every pad and adds the replacements, so a
    // STEPS change left the sweep line under 162 of them for the rest of the
    // session. The first fix was `setAlwaysOnTop (true)` on the playhead —
    // which works, and is compensation for a mutation somewhere else, and would
    // have to be repeated by every sibling a later plan adds here. `rebuild`
    // now sends its pads to the BACK, so nothing this grid owns needs to know
    // about the rebuild at all. /code-review found the bug; /simplify found the
    // depth.

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

        // The confirmation flash, on the tick that already runs — no new timer,
        // and TOLD its elapsed time.
        padGrid.advanceFlash (playheadPoll.secondsSinceLastTick (pad::kFlashSeconds));
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

    for (int row = 0; row < ChassisLayout::kNumStrips; ++row)
        labelZones[static_cast<size_t> (row)]
            .setBounds (layout.rows[static_cast<size_t> (row)].label);

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
        // The ZONE is asked, not a mirrored member. css:460 lifts an isolated
        // or hovered label to `--fg`.
        const auto lit = i == isolatedRow
                      || labelZones[static_cast<size_t> (i)].isHovered();

        g.setColour (lnf.token (lit ? theme::Token::fg : theme::Token::fgDim)
                         .withMultipliedAlpha (rowAlpha));
        type::drawTracked (g, type::Style::sequencerRowLabel,
                           juce::String (juce::CharPointer_UTF8 (
                               ids::channelInfos[static_cast<size_t> (i)].displayName)),
                           row.name.toFloat(), juce::Justification::centredLeft);
    }
}

} // namespace forrobox
