#include "Chassis.h"

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
            stripInteriorOf (out.strips[static_cast<size_t> (i)]);

    return out;
}

ChassisLayout::StripLayout ChassisLayout::stripInteriorOf (juce::Rectangle<int> strip) noexcept
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

    // Whatever is left is 04-02/04-03/04-04's, reserved and reachable.
    out.controls = content;

    return out;
}

Chassis::Chassis (ForroBoxLookAndFeel& lookAndFeelToUse)
    : lnf (lookAndFeelToUse)
{
    setOpaque (true);
    setSize (ChassisLayout::kWidth, ChassisLayout::kHeight);
}

void Chassis::resized()
{
    layout = ChassisLayout::forBounds (getLocalBounds());
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

    // interior.controls is 04-02/04-03/04-04's box. Deliberately not filled
    // with placeholder chrome that would then have to be found and removed —
    // Phase 1's placeholder editor text is the precedent for how easy that is
    // to leave behind. It is reserved in ChassisLayout and asserted there, so
    // it is reachable without being drawn.
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
