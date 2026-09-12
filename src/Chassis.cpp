#include "Chassis.h"

#include "KnobAttachment.h"
#include "ValueTooltip.h"

namespace forrobox
{

ChassisLayout ChassisLayout::forBounds (juce::Rectangle<int> bounds) noexcept
{
    ChassisLayout out;

    auto remaining = bounds;

    out.header    = remaining.removeFromTop (kHeaderHeight);
    out.footer    = remaining.removeFromBottom (kFooterHeight);
    out.sequencer = remaining.removeFromBottom (kSequencerHeight);
    out.main      = remaining;                      // whatever is left: the 1fr row

    auto mainRow  = out.main;
    out.sidePanel = mainRow.removeFromRight (kSidePanelWidth);
    out.matrix    = mainRow;

    // Five equal columns with 1 px gaps. The width does not divide by five
    // evenly at the design size (609 px of matrix), so the columns are placed
    // from exact fractional edges and rounded — which distributes the remainder
    // instead of accumulating it in the last strip.
    const auto gaps       = static_cast<float> (kStripGap * (kNumStrips - 1));
    const auto stripWidth = (static_cast<float> (out.matrix.getWidth()) - gaps)
                          / static_cast<float> (kNumStrips);

    for (int i = 0; i < kNumStrips; ++i)
    {
        const auto left  = static_cast<float> (out.matrix.getX())
                         + static_cast<float> (i) * (stripWidth + static_cast<float> (kStripGap));
        const auto right = left + stripWidth;

        out.strips[static_cast<size_t> (i)] =
            juce::Rectangle<int>::leftTopRightBottom (juce::roundToInt (left),
                                                      out.matrix.getY(),
                                                      juce::roundToInt (right),
                                                      out.matrix.getBottom());
    }

    for (int i = 0; i < kNumStrips; ++i)
        out.stripLayouts[static_cast<size_t> (i)] =
            stripInteriorOf (out.strips[static_cast<size_t> (i)], i);

    return out;
}

ChassisLayout::StripLayout ChassisLayout::stripInteriorOf (juce::Rectangle<int> strip,
                                                           int channelIndex) noexcept
{
    // The single derivation of a strip's interior. paintStrip reads this rather
    // than re-running the removeFromTop chain, and so do the tests — the
    // stacking order lives in one place or it lives in four.
    StripLayout out;

    auto content = strip.reduced (kStripPadSide, 0)
                       .withTrimmedTop (kStripPadTop)
                       .withTrimmedBottom (kStripPadBottom);

    out.headRow = content.removeFromTop (kHeadRowHeight);

    content.removeFromTop (kAccentBarMarginTop);
    out.accentBar = content.removeFromTop (kAccentBarHeight);
    content.removeFromTop (kAccentBarMarginBottom);

    // Everything below the accent bar. Kept whole so the tiling assertion has
    // one rectangle to sum the stack against.
    out.controls = content;

    // ── the stack, PLANNING.md:276-294 in order ────────────────────────────
    //
    // One cursor walked downward, so the order here IS the spec's order and a
    // reordered pair is a moved box rather than two numbers that still add up.
    content.removeFromTop (kSampleSlotMarginTop);
    out.sampleSlot = content.removeFromTop (kSampleSlotHeight);

    content.removeFromTop (kHitVisualiserMarginTop);
    out.hitVisualiser = content.removeFromTop (kHitVisualiserHeight);

    content.removeFromTop (kStripDividerMargin);
    out.dividerTop = content.removeFromTop (kStripDividerHeight);
    content.removeFromTop (kStripDividerMargin);

    out.knobGrid = content.removeFromTop (kKnobGridHeight);

    content.removeFromTop (kStripDividerMargin);
    out.dividerBottom = content.removeFromTop (kStripDividerHeight);
    content.removeFromTop (kStripDividerMargin);

    content.removeFromTop (kPatternRowMarginTop);
    out.patternCycler = content.removeFromTop (kPatternRowHeight);

    content.removeFromTop (kMuteSoloMarginTop);
    out.muteSolo = content.removeFromTop (kMuteSoloHeight);

    content.removeFromTop (kGhostRowMarginTop);
    out.ghostLabel = content.removeFromTop (kGhostLabelHeight);
    content.removeFromTop (kGhostLabelGap);
    out.ghostFader = content.removeFromTop (kFaderHeight);

    // `.subdots` exists only on the channel with sub-lanes — bateria (app.js
    // gates it on `inst.subs`). Identified by its ACCENT rather than the
    // literal index 4: that binding is the one the static_assert at the top of
    // Chassis.h protects, so it cannot drift from the channel table.
    //
    // Walked off the SAME cursor as every other box rather than built by
    // arithmetic. It was `withY().withHeight()`, which made it the one box
    // removeFromTop's clamping could not contain — and the only one the
    // containment loop in the tests skipped.
    if (static_cast<theme::Accent> (channelIndex) == theme::Accent::bateria)
    {
        content.removeFromTop (kSubDotsMarginTop);
        out.subDots = content.removeFromTop (kSubDotSize);
    }

    // ── the knob cells: row-major across two columns ───────────────────────
    //
    // VOL / PITCH / DECAY / PAN, the order app.js:180-183 instantiates them in.
    // Derived here rather than at the call site so the gaps exist once.
    {
        const auto cellWidth = (out.knobGrid.getWidth() - kKnobGridColGap) / kKnobGridCols;

        for (int i = 0; i < static_cast<int> (out.knobCells.size()); ++i)
        {
            const auto row = i / kKnobGridCols;
            const auto col = i % kKnobGridCols;

            // `justify-items: center` (css:326) centres each knob in its cell,
            // so the cell is the full column width and the dial centres inside.
            out.knobCells[static_cast<size_t> (i)] = juce::Rectangle<int> (
                out.knobGrid.getX() + col * (cellWidth + kKnobGridColGap),
                out.knobGrid.getY() + row * (kKnobCellHeight + kKnobGridRowGap),
                cellWidth,
                kKnobCellHeight);
        }
    }

    return out;
}


Chassis::Chassis (ForroBoxLookAndFeel& lookAndFeelToUse)
    : lnf (lookAndFeelToUse)
{
    setOpaque (true);
    setSize (ChassisLayout::kWidth, ChassisLayout::kHeight);
}

Chassis::~Chassis() = default;

namespace
{
/** One channel parameter, or nullptr with an assertion.

    A missing parameter is a wiring error, not a case to paper over: a control
    bound to nothing would look right and do nothing. Shared by the knobs and
    by the three controls below them so the assertion is written once. */
juce::RangedAudioParameter* rangedParameter (juce::AudioProcessorValueTreeState& apvts,
                                             const char* channelId, const char* parameterId)
{
    auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (
                          apvts.getParameter (ids::channelParam (channelId, parameterId)));

    jassert (parameter != nullptr);

    return parameter;
}
} // namespace

void Chassis::attachParameters (juce::AudioProcessorValueTreeState& apvts, ValueTooltip* tooltip)
{
    // Idempotent. Appending would let a second call index stripLayouts past its
    // five entries, and a second set of children would stack invisibly on the
    // first — each attachment clears its own callbacks as it goes.
    stripKnobs.clear();

    for (auto& controls : stripControls)
        controls = {};

    for (int channel = 0; channel < ChassisLayout::kNumStrips; ++channel)
    {
        const auto& info = ids::channelInfos[static_cast<size_t> (channel)];

        // The same accent binding paintStrip uses, and the one the static_assert
        // at the top of this header protects.
        const auto colour = theme::accent (static_cast<theme::Accent> (channel));

        for (const auto& spec : ChassisLayout::knobSlots)
        {
            auto* parameter = rangedParameter (apvts, info.id, spec.param);

            if (parameter == nullptr)
                continue;

            auto knobComponent = std::make_unique<Knob> (lnf, ChassisLayout::kStripKnobSize,
                                                         spec.polarity, colour, spec.label);
            knobComponent->setTooltip (tooltip);

            auto knobAttachment = std::make_unique<KnobAttachment> (*parameter, *knobComponent);

            addAndMakeVisible (*knobComponent);

            stripKnobs.push_back ({ std::move (knobComponent), std::move (knobAttachment),
                                    channel, static_cast<int> (&spec - ChassisLayout::knobSlots.data()) });
        }

        auto& controls = stripControls[static_cast<size_t> (channel)];

        // ── the three stubs ────────────────────────────────────────────────
        //
        // Built, shown and left unwired. No onClick, no attachment, no state:
        // clicking LOAD or an arrow visibly presses and changes nothing, which
        // is what a v0.1 stub should look like to a reviewer.
        controls.load = std::make_unique<Button> (lnf, Button::Variant::load, "LOAD");
        // fromUTF8, not the implicit const char* conversion. U+2039 came out as
        // "a<EUR>1/2" on the reference render — juce::String's char* constructor
        // does not assume UTF-8, and every other accented literal in this file
        // already goes through fromUTF8 for the same reason.
        controls.patternPrev = std::make_unique<Button> (
            lnf, Button::Variant::arrow, juce::String::fromUTF8 (ChassisLayout::kArrowPrev));
        controls.patternNext = std::make_unique<Button> (
            lnf, Button::Variant::arrow, juce::String::fromUTF8 (ChassisLayout::kArrowNext));

        // ── mute and solo ──────────────────────────────────────────────────
        controls.mute = std::make_unique<Button> (lnf, Button::Variant::muteSolo, "M",
                                                  Button::OnStyle::mute);
        controls.solo = std::make_unique<Button> (lnf, Button::Variant::muteSolo, "S",
                                                  Button::OnStyle::solo);

        if (auto* muteParameter = rangedParameter (apvts, info.id, ids::mute))
            controls.muteAttachment = std::make_unique<ToggleAttachment> (*muteParameter,
                                                                         *controls.mute);

        if (auto* soloParameter = rangedParameter (apvts, info.id, ids::solo))
            controls.soloAttachment = std::make_unique<ToggleAttachment> (*soloParameter,
                                                                          *controls.solo);

        // ── the ghost fader, and the readout that hangs off it ─────────────
        controls.ghost = std::make_unique<Fader> (lnf, colour);

        if (auto* ghostParameter = rangedParameter (apvts, info.id, ids::ghost))
        {
            // ONE listener on the parameter. The readout is downstream of the
            // fader, not a second attachment — two attachments can disagree,
            // and a percentage beside a fader showing a different position is
            // the worst kind of wrong because both look plausible.
            controls.ghost->onProportionChanged =
                [this, ghostParameter, channel] (float)
                {
                    auto& strip = stripControls[static_cast<size_t> (channel)];
                    strip.ghostText = ghostParameter->getCurrentValueAsText();

                    // Only the readout's own box, not the strip: a repaint of
                    // the whole strip on every drag frame would redraw five
                    // knobs and a fader to change six characters.
                    repaint (layout.stripLayouts[static_cast<size_t> (channel)].ghostLabel);
                };

            controls.ghostAttachment =
                std::make_unique<ProportionAttachment<Fader>> (*ghostParameter, *controls.ghost);

            // AFTER onProportionChanged is installed, so the readout is
            // populated by the same update that positions the fader rather
            // than staying empty until the first drag.
            controls.ghostAttachment->sendInitialUpdate();
        }

        for (auto* child : { controls.load.get(), controls.patternPrev.get(),
                             controls.patternNext.get(), controls.mute.get(),
                             controls.solo.get() })
            addAndMakeVisible (*child);

        addAndMakeVisible (*controls.ghost);
    }

    resized();
}

void Chassis::resized()
{
    layout = ChassisLayout::forBounds (getLocalBounds());

    // Each knob into the cell it RECORDED, not one derived from its position in
    // the vector. The dial is centred in its cell (`justify-items: center`,
    // css:326) and the cell already includes the micro-label row.
    for (const auto& placed : stripKnobs)
    {
        const auto cell = layout.stripLayouts[static_cast<size_t> (placed.channel)]
                              .knobCells[static_cast<size_t> (placed.slot)];

        placed.knob->setBounds (
            juce::Rectangle<int> (ChassisLayout::kStripKnobSize,
                                  Knob::preferredHeight (ChassisLayout::kStripKnobSize, true))
                .withCentre ({ cell.getCentreX(), cell.getCentreY() }));
    }

    for (int channel = 0; channel < ChassisLayout::kNumStrips; ++channel)
    {
        auto& controls = stripControls[static_cast<size_t> (channel)];

        if (controls.load == nullptr)
            continue;   // attachParameters has not run; the chassis is a surface

        const auto& interior = layout.stripLayouts[static_cast<size_t> (channel)];

        // ── sample slot: `display:flex; gap:7px` — name then LOAD ──────────
        //
        // The button is placed first and takes its own preferred width; the
        // name gets what is left, which is `flex: 1` (css:291). Its vertical
        // centring is the row's `align-items: center`, and the button is the
        // tallest child so it defines the row.
        {
            auto slot = interior.sampleSlot;
            const auto loadWidth = controls.load->preferredWidth();

            controls.load->setBounds (slot.removeFromRight (loadWidth)
                                          .withSizeKeepingCentre (loadWidth,
                                                                  controls.load->preferredHeight()));
        }

        // ── pattern cycler: arrow, screen, arrow ───────────────────────────
        //
        // The two arrows are fixed at 22x26 (css:231) and the screen is
        // `flex: 1`. Both arrows are vertically centred in the row rather than
        // filling it, which is what `align-items: center` does and what makes
        // the row's height the taller of the two children.
        {
            auto row = interior.patternCycler;
            const auto arrow = juce::Rectangle<int> (Button::kArrowWidth, Button::kArrowHeight);

            controls.patternPrev->setBounds (
                row.removeFromLeft (Button::kArrowWidth)
                   .withSizeKeepingCentre (arrow.getWidth(), arrow.getHeight()));

            controls.patternNext->setBounds (
                row.removeFromRight (Button::kArrowWidth)
                   .withSizeKeepingCentre (arrow.getWidth(), arrow.getHeight()));
        }

        // ── mute / solo: two `flex: 1` buttons with a 5 px gap ─────────────
        //
        // The gap comes out of the middle and the halves take the rest, so an
        // odd width gives one button the extra pixel rather than leaving a
        // one-pixel seam at the right edge.
        {
            auto row = interior.muteSolo;
            const auto gap = ChassisLayout::kMuteSoloGap;
            const auto half = (row.getWidth() - gap) / 2;

            controls.mute->setBounds (row.removeFromLeft (half));
            row.removeFromLeft (gap);
            controls.solo->setBounds (row);
        }

        // ── the ghost fader ────────────────────────────────────────────────
        //
        // Asks the fader for the bounds its reserved box needs, exactly as the
        // knob cell asks the knob for its height. The thumb then hangs into the
        // strip's 11 px side padding, which is where it hangs in the browser.
        controls.ghost->setBounds (Fader::boundsForBox (interior.ghostFader));
    }
}

void Chassis::paint (juce::Graphics& g)
{
    // The chassis ground. Every region paints over it, so a region that fails
    // to paint shows as --bg rather than as whatever was in the buffer.
    g.fillAll (lnf.token (theme::Token::bg));

    paintHeader (g, layout.header);
    paintMatrix (g, layout.matrix);
    paintSidePanel (g, layout.sidePanel);
    paintSequencer (g, layout.sequencer);
    paintFooter (g, layout.footer);
}

void Chassis::paintRaisedHighlight (juce::Graphics& g, juce::Rectangle<int> area,
                                    juce::Colour highlight) const
{
    // The colour is a parameter, not read from one field here: the header and
    // the side/footer share this MECHANISM but not its value (see
    // theme::Shadows). Reaching for a single field is what shipped the light
    // header at 0.50 where the stylesheet says 0.05.
    g.setColour (highlight);
    g.fillRect (area.getX(), area.getY(), area.getWidth(), 1);
}

void Chassis::paintWellShadow (juce::Graphics& g, juce::Rectangle<int> area,
                               juce::Colour shadow, float depth) const
{
    const auto height = juce::jmin (static_cast<float> (area.getHeight()), depth * 3.0f);

    g.setGradientFill (juce::ColourGradient::vertical (shadow, static_cast<float> (area.getY()),
                                                       shadow.withAlpha (0.0f),
                                                       static_cast<float> (area.getY()) + height));
    g.fillRect (area.withHeight (juce::roundToInt (height)));
}

void Chassis::paintHeader (juce::Graphics& g, juce::Rectangle<int> area) const
{
    // `linear-gradient(180deg, <raised + 3% white>, <raised>)` plus the top
    // highlight and a 1 px bottom border.
    const auto raised = lnf.token (theme::Token::raised);

    g.setGradientFill (juce::ColourGradient::vertical (
        theme::mix (raised, juce::Colours::white, ChassisLayout::kHeaderGradientWeight), static_cast<float> (area.getY()),
        raised, static_cast<float> (area.getBottom())));
    g.fillRect (area);

    paintRaisedHighlight (g, area, lnf.shadows().headerHighlight);

    g.setColour (lnf.token (theme::Token::line));
    g.fillRect (area.getX(), area.getBottom() - 1, area.getWidth(), 1);
}

void Chassis::paintMatrix (juce::Graphics& g, juce::Rectangle<int> area) const
{
    // The 1 px gaps BETWEEN the strips are the dividers — nothing draws a
    // border, because two adjacent borders would give a 2 px seam.
    //
    // Only the gaps are filled, not the whole matrix. Filling the matrix and
    // letting the strips overpaint it is the same picture (verified: 0 of
    // 936,000 pixels differ, both themes) and 158x the work — 419,520 px
    // touched to reveal 1,824, of which the strips immediately cover 417,696.
    // Measured 548.7 us against 3.46 us, on a 1,516 us full repaint; at 2x the
    // wasted fill alone is ~2.2 ms.
    g.setColour (lnf.token (theme::Token::line));

    // Every column of the matrix no strip covers, so this is the old whole-area
    // fill's result by construction and not by an argument about float
    // rounding: the strips span the full matrix height, and the edges walked
    // here are matrix.getX() -> strip[0], each gap, and strip[last] ->
    // matrix.getRight(). The two outer slivers are empty at every size tested
    // and fill nothing when they are; they exist so that a strip derivation
    // whose rounding does NOT reach the matrix edge still paints --line there
    // rather than leaving --bg showing through.
    auto edge = area.getX();

    for (int i = 0; i < ChassisLayout::kNumStrips; ++i)
    {
        const auto& strip = layout.strips[static_cast<size_t> (i)];

        g.fillRect (juce::Rectangle<int>::leftTopRightBottom (
            edge, area.getY(), strip.getX(), area.getBottom()));

        edge = strip.getRight();
    }

    g.fillRect (juce::Rectangle<int>::leftTopRightBottom (
        edge, area.getY(), area.getRight(), area.getBottom()));

    for (int i = 0; i < ChassisLayout::kNumStrips; ++i)
        paintStrip (g, layout.strips[static_cast<size_t> (i)], i);
}

void Chassis::paintStrip (juce::Graphics& g, juce::Rectangle<int> area, int channelIndex) const
{
    const auto& interior = layout.stripLayouts[static_cast<size_t> (channelIndex)];
    const auto& info  = ids::channelInfos[static_cast<size_t> (channelIndex)];
    const auto  which = static_cast<theme::Accent> (channelIndex);
    const auto  colour = theme::accent (which);

    // The zabumba strip carries `.anchor`: 88% panel, 12% accent. Barely
    // perceptible on purpose — the brief forbids decoration, and this is the one
    // tint that survives that rule because it marks the anchor instrument.
    const auto panel = lnf.token (theme::Token::panel);
    const auto isAnchor = (which == theme::Accent::zabumba);

    g.setColour (isAnchor ? theme::mix (panel, colour, ChassisLayout::kAnchorAccentWeight) : panel);
    g.fillRect (area);

    // Head row: instrument name left, index right. The names come from
    // ids::channelInfos, never re-typed here.
    const auto headRow = interior.headRow.toFloat();

    g.setColour (lnf.token (theme::Token::fg));
    type::drawTracked (g, type::Style::stripInstrumentName, info.displayName,
                       headRow, juce::Justification::centredLeft);

    g.setColour (lnf.token (theme::Token::fgDim));
    type::drawTracked (g, type::Style::stripIndex,
                       juce::String (channelIndex + 1).paddedLeft ('0', 2),
                       headRow, juce::Justification::centredRight);

    // Accent bar: 4 px, radius 1, with the glow at accent-intensity x 35%.
    const auto bar = interior.accentBar.toFloat();

    // `box-shadow: 0 0 10px <colour at accent-intensity x 35%>`.
    //
    // juce::DropShadow, not a stack of expanded rectangles at low alpha. That
    // was the first attempt and it renders a HARD-EDGED rectangular frame
    // around each bar rather than a glow — clearly visible in the light theme,
    // where it read as a grey box. DropShadow does a real blur, and with a zero
    // offset it is exactly the CSS's centred glow.
    const auto glow = colour.withAlpha (lnf.accentIntensity() * ChassisLayout::kAccentGlowOpacity);

    juce::DropShadow (glow, ChassisLayout::kAccentGlowRadius, {}).drawForRectangle (g, bar.toNearestInt());

    // `--accent-i` applies to this bar TWICE — the glow alpha above and the
    // fill's saturation here (css:285). Both are the identity at the default
    // intensity of 1.0; only one of them used to exist.
    g.setColour (theme::accentFill (colour, lnf.accentIntensity()));
    g.fillRoundedRectangle (bar, 1.0f);

    // The two 1 px dividers that bracket the knob grid (css:321). 04-02 paints
    // these and the knob grid; every other reserved box stays empty until the
    // plan that owns it arrives.
    g.setColour (lnf.token (theme::Token::line));
    g.fillRect (interior.dividerTop);
    g.fillRect (interior.dividerBottom);

    // The four boxes whose content is TEXT or plain shapes. The six that are
    // components — LOAD, both arrows, M, S and the fader — paint themselves as
    // children, which is what gives them hover and press without this method
    // knowing anything about either.
    paintSampleSlot (g, interior, channelIndex);
    paintPatternCycler (g, interior);
    paintGhostLabel (g, interior, channelIndex);

    if (! interior.subDots.isEmpty())
        paintSubDots (g, interior);

    // interior.hitVisualiser stays empty: it is Phase 5's activity meter and
    // has nothing to show until there are triggers to show. Reserved in
    // ChassisLayout and asserted there, so it is reachable without being drawn
    // — the same rule that kept `controls` undrawn through 04-01 and 04-02.
}

void Chassis::paintSampleSlot (juce::Graphics& g, const ChassisLayout::StripLayout& interior,
                               int channelIndex) const
{
    const auto& controls = stripControls[static_cast<size_t> (channelIndex)];

    // `flex: 1; text-overflow: ellipsis` — whatever the LOAD button leaves,
    // minus the 7 px gap. When the button has not been built the name gets the
    // whole box, which is what a chassis with no processor should show.
    auto available = interior.sampleSlot;

    if (controls.load != nullptr)
        available = available.withTrimmedRight (controls.load->getWidth()
                                                + ChassisLayout::kSampleSlotGap);

    const auto name = juce::String::fromUTF8 (
        ChassisLayout::sampleNames[static_cast<size_t> (channelIndex)]);

    g.setColour (lnf.token (theme::Token::fg));
    type::drawTracked (g, type::Style::sampleName,
                       type::ellipsised (type::Style::sampleName, name,
                                         static_cast<float> (available.getWidth())),
                       available.toFloat(), juce::Justification::centredLeft);
}

void Chassis::paintPatternCycler (juce::Graphics& g,
                                  const ChassisLayout::StripLayout& interior) const
{
    // The screen, between the two arrow buttons. `flex: 1`, so it is the row
    // minus both arrows and both gaps — derived from the button's own width,
    // not from a third copy of 22.
    const auto inset = Button::kArrowWidth + ChassisLayout::kPatternRowGap;

    const auto screen = interior.patternCycler.reduced (inset, 0)
                            .withSizeKeepingCentre (interior.patternCycler.getWidth() - inset * 2,
                                                    ChassisLayout::kPatternScreenHeight);

    const auto radius = lnf.cornerRadius();

    g.setColour (lnf.token (theme::Token::screen));
    g.fillRoundedRectangle (screen.toFloat(), radius);

    g.setColour (lnf.token (theme::Token::line));
    g.drawRoundedRectangle (screen.toFloat().reduced (0.5f), radius, 1.0f);

    g.setColour (lnf.token (theme::Token::screenFg));
    type::drawTracked (g, type::Style::patternScreen, ChassisLayout::kPatternScreenText,
                       screen.toFloat(), juce::Justification::centred);
}

void Chassis::paintGhostLabel (juce::Graphics& g, const ChassisLayout::StripLayout& interior,
                               int channelIndex) const
{
    // `justify-content: space-between` — the caption left, the readout right.
    const auto row = interior.ghostLabel.toFloat();

    g.setColour (lnf.token (theme::Token::fgFaint));
    type::drawTracked (g, type::Style::stripMicroLabel, "Ghost Prob", row,
                       juce::Justification::centredLeft);

    // Whatever the ghost attachment last wrote. Empty on a chassis with no
    // processor, which is honest: there is no value to show.
    g.setColour (lnf.token (theme::Token::fgDim));
    type::drawTracked (g, type::Style::ghostValue,
                       stripControls[static_cast<size_t> (channelIndex)].ghostText, row,
                       juce::Justification::centredRight);
}

void Chassis::paintSubDots (juce::Graphics& g, const ChassisLayout::StripLayout& interior) const
{
    // Four 8 px circles in the bateria piece colours, then the label, inset by
    // its own margin — css:351-354. A STUB: nothing opens the kit.
    auto row = interior.subDots;

    for (int i = 0; i < ChassisLayout::kNumSubDots; ++i)
    {
        const auto dot = row.removeFromLeft (ChassisLayout::kSubDotSize);

        g.setColour (theme::subColour (i).withMultipliedAlpha (ChassisLayout::kSubDotOpacity));
        g.fillEllipse (dot.toFloat());

        row.removeFromLeft (ChassisLayout::kSubDotGap);
    }

    g.setColour (lnf.token (theme::Token::fgFaint));
    type::drawTracked (g, type::Style::stripMicroLabel,
                       juce::String::fromUTF8 (ChassisLayout::kSubDotsLabel),
                       row.withTrimmedLeft (ChassisLayout::kSubDotsLabelInset).toFloat(),
                       juce::Justification::centredLeft);
}

void Chassis::paintSidePanel (juce::Graphics& g, juce::Rectangle<int> area) const
{
    g.setColour (lnf.token (theme::Token::raised));
    g.fillRect (area);

    g.setColour (lnf.token (theme::Token::line));
    g.fillRect (area.getX(), area.getY(), 1, area.getHeight());

    // `border-left: 1px solid var(--line)` AND `inset 0 1px 0 <highlight>`. An
    // inset box-shadow is drawn inside the border box, so the highlight starts
    // one column right of the border rather than running over it.
    paintRaisedHighlight (g, area.withTrimmedLeft (1), lnf.shadows().raisedHighlight);
}

void Chassis::paintSequencer (juce::Graphics& g, juce::Rectangle<int> area) const
{
    g.setColour (lnf.token (theme::Token::sunken));
    g.fillRect (area);

    const auto shadows = lnf.shadows();
    paintWellShadow (g, area, shadows.wellShadow, shadows.wellRadius);

    g.setColour (lnf.token (theme::Token::line));
    g.fillRect (area.getX(), area.getY(), area.getWidth(), 1);
}

void Chassis::paintFooter (juce::Graphics& g, juce::Rectangle<int> area) const
{
    g.setColour (lnf.token (theme::Token::raised));
    g.fillRect (area);

    g.setColour (lnf.token (theme::Token::line));
    g.fillRect (area.getX(), area.getY(), area.getWidth(), 1);

    // `border-top: 1px solid var(--line)` AND `inset 0 1px 0 <highlight>` —
    // two different rows. Painting the highlight first and the border over it
    // put both on row 0, so the footer's highlight never rendered at all.
    paintRaisedHighlight (g, area.withTrimmedTop (1), lnf.shadows().raisedHighlight);
}

} // namespace forrobox
