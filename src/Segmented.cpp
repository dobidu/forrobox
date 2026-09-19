#include "Segmented.h"

namespace forrobox
{

Segmented::Segmented (ForroBoxLookAndFeel& lookAndFeelToUse, juce::StringArray labelsToUse,
                      type::Style styleToUse, Variant variantToUse)
    : lnf (lookAndFeelToUse), labels (std::move (labelsToUse)), variant (variantToUse),
      style (styleToUse)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);

    const auto& spec = specFor (variant);

    auto x = segmented::kBorderWidth;

    for (const auto& label : labels)
    {
        const auto width = juce::roundToInt (type::trackedWidth (style, label)) + spec.padX * 2;

        spans.push_back (juce::Range<int>::withStartAndLength (x, width));

        // The out-toggle is `gap: 0` with no `border-right`, so its segments
        // ABUT — its divider width is 0 and this adds nothing.
        x += width + spec.dividerWidth;
    }
}

int Segmented::heightOf (type::Style style, Variant variant) noexcept
{
    return type::boxHeight (style, specFor (variant).padY, segmented::kBorderWidth);
}

int Segmented::widthOf (const juce::StringArray& labels, type::Style style,
                        Variant variant) noexcept
{
    const auto& spec = specFor (variant);

    auto total = segmented::kBorderWidth * 2;

    for (const auto& label : labels)
        total += juce::roundToInt (type::trackedWidth (style, label)) + spec.padX * 2;

    // N-1 dividers, NOT N. `border-right` with `:last-child { border-right: 0 }`
    // — css:247 and :250. The out-toggle's width is 0, so it adds nothing.
    total += juce::jmax (0, labels.size() - 1) * spec.dividerWidth;

    return total;
}

void Segmented::setReadOnly (bool shouldBeReadOnly)
{
    if (readOnly == shouldBeReadOnly)
        return;

    readOnly = shouldBeReadOnly;

    setMouseCursor (readOnly ? juce::MouseCursor::NormalCursor
                             : juce::MouseCursor::PointingHandCursor);
    setAlpha (readOnly ? theme::kReadOnlyAlpha : 1.0f);

    if (readOnly)
        hoveredIndex = -1;

    repaint();
}

int Segmented::preferredHeight() const { return heightOf (style, variant); }
int Segmented::preferredWidth() const  { return widthOf (labels, style, variant); }

juce::Rectangle<int> Segmented::segmentBounds (int index) const
{
    if (! juce::isPositiveAndBelow (index, static_cast<int> (spans.size())))
        return {};

    const auto& span = spans[static_cast<size_t> (index)];

    return { span.getStart(), segmented::kBorderWidth, span.getLength(),
             getHeight() - segmented::kBorderWidth * 2 };
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
    // -1 means NOTHING selected, and it has to be reachable: 06-03's STYLE
    // control shows the profile the state names, and an edited state names none
    // — `PLANNING.md:601-602`, where the highlight clears. Clamping to 0 turned
    // "no profile" into "the first profile", which is the same wrong answer
    // `ChassisLayout::indexOfProfile`'s `ifUnknown` exists to let callers choose
    // against. Anything else out of range still clamps.
    const auto resolved = index < 0 ? -1
                                    : juce::jlimit (0, juce::jmax (0, labels.size() - 1), index);

    if (selectedIndex != resolved)
    {
        selectedIndex = resolved;
        repaint();
    }
}

void Segmented::paint (juce::Graphics& g)
{
    const auto& spec = specFor (variant);

    const auto area = getLocalBounds().toFloat().reduced (segmented::kBorderWidth * 0.5f);
    const auto radius = lnf.cornerRadius (spec.radiusExtra);

    // `.out-toggle` declares no background at all, so its unlit segments show
    // whatever is behind the control — the footer's `--raised`. Filling
    // `--sunken` there would be a well the stylesheet does not ask for.
    if (spec.sunkenGround)
    {
        g.setColour (lnf.token (theme::Token::sunken));
        g.fillRoundedRectangle (area, radius);
    }

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
        // A zero-width fillRect is a no-op, which is what the out-toggle wants.
        if (i < labels.size() - 1)
        {
            g.setColour (lnf.token (theme::Token::line));
            g.fillRect (bounds.getRight(), bounds.getY(),
                        static_cast<float> (spec.dividerWidth), bounds.getHeight());
        }
    }

    // `inset 0 1px 2px rgba(0,0,0,0.3)`, drawn over the segments because an
    // inset shadow sits above the background and below the border. css:545
    // declares none for the out-toggle, whose alpha is 0 — a fully transparent
    // fill is a no-op.
    g.setColour (juce::Colour::fromFloatRGBA (0.0f, 0.0f, 0.0f, spec.insetAlpha));
    g.fillRect (area.withHeight (1.0f).reduced (radius * 0.5f, 0.0f));

    g.setColour (lnf.token (theme::Token::lineStrong));
    g.drawRoundedRectangle (area, radius, static_cast<float> (segmented::kBorderWidth));
}

void Segmented::mouseMove (const juce::MouseEvent& e)
{
    if (readOnly)
        return;

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
    if (readOnly)
        return;

    // Right-click belongs to the host, as it does on every other control here.
    if (e.mods.isPopupMenu())
        return;

    pressedIndex = indexAt (e.getPosition());
}

void Segmented::mouseUp (const juce::MouseEvent& e)
{
    if (readOnly)
        return;

    const auto index = indexAt (e.getPosition());
    const auto wasPressed = pressedIndex;

    pressedIndex = -1;

    // Fires only when released on the segment it was pressed on.
    if (index >= 0 && index == wasPressed && onSegmentClicked != nullptr)
        onSegmentClicked (index);
}

} // namespace forrobox
