#include "TimbreRow.h"

#include "Chassis.h"
#include "SidePanel.h"
#include "Surface.h"
#include "Theme.h"
#include "Typography.h"

namespace forrobox
{

TimbreRow::TimbreRow (ForroBoxLookAndFeel& lookAndFeelToUse, int indexToUse)
    : lnf (lookAndFeelToUse), index (indexToUse)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

int TimbreRow::heightOf() noexcept
{
    // A flex row of the two stacked labels against the LED — css:417's
    // `align-items: center`, so the row is as tall as its tallest child.
    const auto labels = textBox (type::Style::timbreName)
                      + textBox (type::Style::timbreSubLabel);

    return flexRow (labels, side::kTimbreLedSize)
         + side::kTimbrePadY * 2 + side::kBorder * 2;
}

juce::Rectangle<int> TimbreRow::ledBounds() const noexcept
{
    auto inner = getLocalBounds().reduced (side::kTimbrePadX + side::kBorder,
                                           side::kTimbrePadY + side::kBorder);

    return centredInRow (inner, inner.removeFromRight (side::kTimbreLedSize)
                                     .withHeight (side::kTimbreLedSize));
}

void TimbreRow::setSelected (bool shouldBeSelected)
{
    if (selected == shouldBeSelected)
        return;

    selected = shouldBeSelected;
    repaint();
}

void TimbreRow::mouseUp (const juce::MouseEvent& event)
{
    if (getLocalBounds().contains (event.getPosition()) && onClick != nullptr)
        onClick();
}

void TimbreRow::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds();
    const auto radius = lnf.cornerRadius();
    const auto& spec = timbreSpecs[static_cast<size_t> (index)];

    // `color-mix(in srgb, var(--panel) 70%, var(--active))` — css:422.
    g.setColour (selected ? theme::mix (lnf.token (theme::Token::panel),
                                        lnf.token (theme::Token::active),
                                        side::kTimbreActiveMix)
                          : lnf.token (theme::Token::panel));
    g.fillRoundedRectangle (area.toFloat(), radius);

    g.setColour (lnf.token (selected ? theme::Token::active : theme::Token::line));
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), radius, 1.0f);

    auto inner = area.reduced (side::kTimbrePadX + side::kBorder,
                               side::kTimbrePadY + side::kBorder);

    const auto led = ledBounds();

    inner.removeFromRight (side::kTimbreLedSize);

    const auto nameHeight = textBox (type::Style::timbreName);
    const auto subHeight  = textBox (type::Style::timbreSubLabel);

    auto labels = centredInRow (inner, inner.withHeight (nameHeight + subHeight));

    g.setColour (lnf.token (theme::Token::fg));
    type::drawTracked (g, type::Style::timbreName, spec.displayName,
                       labels.removeFromTop (nameHeight).toFloat(),
                       juce::Justification::centredLeft);

    g.setColour (lnf.token (theme::Token::fgFaint));
    type::drawTracked (g, type::Style::timbreSubLabel,
                       juce::String (juce::CharPointer_UTF8 (spec.subLabel)),
                       labels.toFloat(), juce::Justification::centredLeft);

    // `--line-strong` unlit; `--c-ganza` with a 6 px glow lit — css:428/429.
    if (selected)
        surface::glowDot (g, led, theme::accent (theme::Accent::ganza),
                          juce::roundToInt (side::kTimbreLedGlowRadius));
    else
    {
        g.setColour (lnf.token (theme::Token::lineStrong));
        g.fillEllipse (led.toFloat());
    }
}

} // namespace forrobox
