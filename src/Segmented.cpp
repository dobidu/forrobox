#include "Segmented.h"

namespace forrobox
{

Segmented::Segmented (ForroBoxLookAndFeel& lookAndFeelToUse, juce::StringArray labelsToUse,
                      type::Style styleToUse)
    : lnf (lookAndFeelToUse), labels (std::move (labelsToUse)), style (styleToUse)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

int Segmented::preferredHeight() const
{
    return static_cast<int> (type::styleFor (style).heightPx + 0.5f)
         + segmented::kPadY * 2 + segmented::kBorderWidth * 2;
}

int Segmented::preferredWidth() const
{
    auto total = segmented::kBorderWidth * 2;

    for (const auto& label : labels)
        total += juce::roundToInt (type::trackedWidth (style, label)) + segmented::kPadX * 2;

    // N-1 dividers, NOT N. `border-right` with `:last-child { border-right: 0 }`
    // — css:247 and :250.
    total += juce::jmax (0, labels.size() - 1) * segmented::kDividerWidth;

    return total;
}

juce::Rectangle<int> Segmented::segmentBounds (int index) const
{
    if (! juce::isPositiveAndBelow (index, labels.size()))
        return {};

    auto x = segmented::kBorderWidth;

    for (int i = 0; i < index; ++i)
        x += juce::roundToInt (type::trackedWidth (style, labels[i])) + segmented::kPadX * 2
           + segmented::kDividerWidth;

    const auto width = juce::roundToInt (type::trackedWidth (style, labels[index]))
                     + segmented::kPadX * 2;

    return { x, segmented::kBorderWidth, width, getHeight() - segmented::kBorderWidth * 2 };
}

int Segmented::indexAt (juce::Point<int> position) const
{
    for (int i = 0; i < labels.size(); ++i)
        if (segmentBounds (i).contains (position))
            return i;

    return -1;
}

void Segmented::setSelectedIndex (int index)
{
    const auto clamped = juce::jlimit (0, juce::jmax (0, labels.size() - 1), index);

    if (selectedIndex != clamped)
    {
        selectedIndex = clamped;
        repaint();
    }
}

void Segmented::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced (segmented::kBorderWidth * 0.5f);
    const auto radius = lnf.cornerRadius (segmented::kRadiusExtra);

    g.setColour (lnf.token (theme::Token::sunken));
    g.fillRoundedRectangle (area, radius);

    for (int i = 0; i < labels.size(); ++i)
    {
        const auto bounds = segmentBounds (i).toFloat();

        if (i == selectedIndex)
        {
            g.setColour (lnf.token (theme::Token::active));
            g.fillRect (bounds);
        }
        else if (i == hoveredIndex)
        {
            // `color-mix(in srgb, var(--fg) 8%, transparent)` — a translucent
            // --fg, not a mix with the ground, because the second colour is
            // `transparent`: the weight becomes the alpha.
            g.setColour (lnf.token (theme::Token::fg)
                             .withAlpha (segmented::kHoverGroundPct / 100.0f));
            g.fillRect (bounds);
        }

        g.setColour (i == selectedIndex ? lnf.token (theme::Token::bg)
                                        : (i == hoveredIndex ? lnf.token (theme::Token::fg)
                                                             : lnf.token (theme::Token::fgDim)));

        type::drawTracked (g, style, labels[i], bounds, juce::Justification::centred);

        // `border-right: 1px solid var(--line)`, and `:last-child` has NONE.
        if (i < labels.size() - 1)
        {
            g.setColour (lnf.token (theme::Token::line));
            g.fillRect (bounds.getRight(), bounds.getY(),
                        static_cast<float> (segmented::kDividerWidth), bounds.getHeight());
        }
    }

    // `inset 0 1px 2px rgba(0,0,0,0.3)`, drawn over the segments because an
    // inset shadow sits above the background and below the border.
    g.setColour (juce::Colour::fromFloatRGBA (0.0f, 0.0f, 0.0f, segmented::kInsetAlpha));
    g.fillRect (area.withHeight (1.0f).reduced (radius * 0.5f, 0.0f));

    g.setColour (lnf.token (theme::Token::lineStrong));
    g.drawRoundedRectangle (area, radius, static_cast<float> (segmented::kBorderWidth));
}

void Segmented::mouseMove (const juce::MouseEvent& e)
{
    const auto index = indexAt (e.getPosition());

    if (hoveredIndex != index)
    {
        hoveredIndex = index;
        repaint();
    }
}

void Segmented::mouseExit (const juce::MouseEvent&)
{
    if (hoveredIndex >= 0 || pressedIndex >= 0)
    {
        hoveredIndex = -1;
        pressedIndex = -1;
        repaint();
    }
}

void Segmented::mouseDown (const juce::MouseEvent& e)
{
    // Right-click belongs to the host, as it does on every other control here.
    if (e.mods.isPopupMenu())
        return;

    pressedIndex = indexAt (e.getPosition());
}

void Segmented::mouseUp (const juce::MouseEvent& e)
{
    const auto index = indexAt (e.getPosition());
    const auto wasPressed = pressedIndex;

    pressedIndex = -1;

    // Fires only when released on the segment it was pressed on.
    if (index >= 0 && index == wasPressed && onSegmentClicked != nullptr)
        onSegmentClicked (index);
}

} // namespace forrobox
