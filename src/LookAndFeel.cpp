#include "LookAndFeel.h"

namespace forrobox
{

ForroBoxLookAndFeel::ForroBoxLookAndFeel (theme::Mode initialMode)
    : mode (initialMode)
{
    applyColourScheme();
}

void ForroBoxLookAndFeel::setMode (theme::Mode newMode) noexcept
{
    mode = newMode;
    applyColourScheme();
}

juce::Colour ForroBoxLookAndFeel::token (theme::Token which) const noexcept
{
    return theme::colour (which, mode);
}

void ForroBoxLookAndFeel::applyColourScheme()
{
    // Only the IDs something consumes. A full ColourScheme would map dozens of
    // IDs for widgets this plugin never instantiates, and every unused mapping
    // is a value nothing verifies.
    setColour (juce::ResizableWindow::backgroundColourId, token (theme::Token::bg));
    setColour (juce::Label::textColourId,                 token (theme::Token::fg));
    setColour (juce::Label::backgroundColourId,           juce::Colours::transparentBlack);
    setColour (juce::TooltipWindow::backgroundColourId,   token (theme::Token::screen));
    setColour (juce::TooltipWindow::textColourId,         token (theme::Token::screenFg));
    setColour (juce::TooltipWindow::outlineColourId,      token (theme::Token::lineStrong));
}

juce::Font ForroBoxLookAndFeel::getLabelFont (juce::Label& label)
{
    // JUCE labels are not used for the design's own text — every string in the
    // chassis is drawn through type::drawTracked, because JUCE has no letter
    // spacing and the design leans on it. This override exists only so that a
    // label created by JUCE itself (a tooltip, an editor) is not left in the
    // platform default font.
    juce::ignoreUnused (label);

    return type::fontFor (type::Face::sansRegular, 12.0f);
}

} // namespace forrobox
