#include "ValueTooltip.h"

namespace forrobox
{

ValueTooltip::ValueTooltip (ForroBoxLookAndFeel& lookAndFeelToUse)
    : lnf (lookAndFeelToUse)
{
    // It must never swallow a gesture aimed at the control underneath it.
    setInterceptsMouseClicks (false, false);
    setVisible (false);
}

void ValueTooltip::showFor (const juce::String& text, juce::Rectangle<int> anchorInParent)
{
    content = text;

    const auto width = juce::roundToInt (type::trackedWidth (type::Style::tooltip, text))
                     + kPaddingX * 2;
    const auto height = juce::roundToInt (type::styleFor (type::Style::tooltip).heightPx)
                      + kPaddingY * 2;

    auto bounds = juce::Rectangle<int> (width, height)
                      .withCentre ({ anchorInParent.getCentreX(), 0 })
                      .withY (anchorInParent.getY() - height - 6);

    // Clamped into the parent: a knob on the top row would otherwise place its
    // tooltip off the window, where the checkpoint could not see it.
    if (auto* parent = getParentComponent())
        bounds = bounds.constrainedWithin (parent->getLocalBounds());

    setBounds (bounds);
    toFront (false);

    if (! isVisible())
    {
        opacity = 0.0f;
        setVisible (true);
    }

    startTimerHz (60);
    repaint();
}

void ValueTooltip::hide()
{
    stopTimer();
    setVisible (false);
    opacity = 0.0f;
}

void ValueTooltip::timerCallback()
{
    // 120 ms from nothing to solid, at the 60 Hz the UI already runs at.
    //
    // Fade IN only. There was a `fadingIn` flag selecting a -perTick arm, but
    // nothing ever set it false — hide() stops the timer and drops opacity to
    // zero — so the fade-out branch was unreachable and the member implied a
    // two-way fade the class does not have.
    const auto perTick = 1000.0f / 60.0f / static_cast<float> (kFadeMs);

    opacity = juce::jmin (1.0f, opacity + perTick);

    if (opacity >= 1.0f)
        stopTimer();

    repaint();
}

void ValueTooltip::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const auto radius = lnf.cornerRadius();

    // The alpha is folded into each COLOUR, not set once through setOpacity.
    // Graphics::setColour calls setFill, which replaces the whole FillType
    // including its opacity, while setOpacity only mutates the current fill's
    // — so `setOpacity` followed by `setColour` discarded the fade entirely and
    // the tooltip popped in fully opaque on the first tick. PLANNING.md:375's
    // 120 ms fade was dead state until this.
    g.setColour (lnf.token (theme::Token::screen).withMultipliedAlpha (opacity));
    g.fillRoundedRectangle (area, radius);

    g.setColour (lnf.token (theme::Token::lineStrong).withMultipliedAlpha (opacity));
    g.drawRoundedRectangle (area.reduced (0.5f), radius, 1.0f);

    g.setColour (lnf.token (theme::Token::screenFg).withMultipliedAlpha (opacity));
    type::drawTracked (g, type::Style::tooltip, content, area, juce::Justification::centred);
}

} // namespace forrobox
