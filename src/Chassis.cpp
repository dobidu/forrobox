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

    return out;
}

Chassis::Chassis (ForroBoxLookAndFeel& lookAndFeel)
    : lnf (lookAndFeel)
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

void Chassis::paintRaisedHighlight (juce::Graphics& g, juce::Rectangle<int> area) const
{
    g.setColour (lnf.shadows().raisedHighlight);
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
        theme::mix (raised, juce::Colours::white, 0.03f), static_cast<float> (area.getY()),
        raised, static_cast<float> (area.getBottom())));
    g.fillRect (area);

    paintRaisedHighlight (g, area);

    g.setColour (lnf.token (theme::Token::line));
    g.fillRect (area.getX(), area.getBottom() - 1, area.getWidth(), 1);
}

void Chassis::paintMatrix (juce::Graphics& g, juce::Rectangle<int> area) const
{
    // The matrix paints --line and the strips sit on top, so the 1 px gaps
    // between them ARE the dividers. Nothing draws a border.
    g.setColour (lnf.token (theme::Token::line));
    g.fillRect (area);

    for (int i = 0; i < ChassisLayout::kNumStrips; ++i)
        paintStrip (g, layout.strips[static_cast<size_t> (i)], i);
}

void Chassis::paintStrip (juce::Graphics& g, juce::Rectangle<int> area, int channelIndex) const
{
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

    auto content = area.reduced (ChassisLayout::kStripPadSide, 0)
                       .withTrimmedTop (ChassisLayout::kStripPadTop)
                       .withTrimmedBottom (ChassisLayout::kStripPadBottom);

    // Head row: instrument name left, index right. The names come from
    // ids::channelInfos, never re-typed here.
    const auto headRow = content.removeFromTop (ChassisLayout::kHeadRowHeight).toFloat();

    g.setColour (lnf.token (theme::Token::fg));
    type::drawTracked (g, type::Style::stripInstrumentName, info.displayName,
                       headRow, juce::Justification::centredLeft);

    g.setColour (lnf.token (theme::Token::fgDim));
    type::drawTracked (g, type::Style::stripIndex,
                       juce::String (channelIndex + 1).paddedLeft ('0', 2),
                       headRow, juce::Justification::centredRight);

    // Accent bar: 4 px, radius 1, with the glow at accent-intensity x 35%.
    content.removeFromTop (ChassisLayout::kAccentBarMarginTop);
    const auto bar = content.removeFromTop (ChassisLayout::kAccentBarHeight).toFloat();
    content.removeFromTop (ChassisLayout::kAccentBarMarginBottom);

    // `box-shadow: 0 0 10px <colour at accent-intensity x 35%>`.
    //
    // juce::DropShadow, not a stack of expanded rectangles at low alpha. That
    // was the first attempt and it renders a HARD-EDGED rectangular frame
    // around each bar rather than a glow — clearly visible in the light theme,
    // where it read as a grey box. DropShadow does a real blur, and with a zero
    // offset it is exactly the CSS's centred glow.
    const auto glow = colour.withAlpha (lnf.accentIntensity() * 0.35f);

    juce::DropShadow (glow, ChassisLayout::kAccentGlowRadius, {}).drawForRectangle (g, bar.toNearestInt());

    g.setColour (colour);
    g.fillRoundedRectangle (bar, 1.0f);

    // Everything below is 04-02/04-03/04-04's. The box is left reserved rather
    // than filled with placeholder chrome that would then have to be found and
    // removed — Phase 1's placeholder editor text is the precedent for how easy
    // that is to leave behind.
    juce::ignoreUnused (content);
}

void Chassis::paintSidePanel (juce::Graphics& g, juce::Rectangle<int> area) const
{
    g.setColour (lnf.token (theme::Token::raised));
    g.fillRect (area);

    paintRaisedHighlight (g, area);

    g.setColour (lnf.token (theme::Token::line));
    g.fillRect (area.getX(), area.getY(), 1, area.getHeight());
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

    paintRaisedHighlight (g, area);

    g.setColour (lnf.token (theme::Token::line));
    g.fillRect (area.getX(), area.getY(), area.getWidth(), 1);
}

} // namespace forrobox
