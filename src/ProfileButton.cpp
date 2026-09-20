#include "ProfileButton.h"

#include "Chassis.h"
#include "SidePanel.h"
#include "Theme.h"
#include "Typography.h"

namespace forrobox
{

int ProfileButton::descriptionLineHeight() noexcept
{
    return juce::roundToInt (type::styleFor (type::Style::profileDescription).heightPx
                             * side::kDescriptionLineHeight);
}

int ProfileButton::heightOf (bool showsDescription) noexcept
{
    const auto name = textBox (type::Style::profileName, side::kProfilePadY, side::kBorder);

    if (! showsDescription)
        return name;

    return name + side::kDescriptionMarginTop + descriptionLineHeight() * 3;
}

ProfileButton::ProfileButton (ForroBoxLookAndFeel& lookAndFeelToUse, int indexToUse)
    : SelectableTile (lookAndFeelToUse, indexToUse)
{
}

void ProfileButton::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds();
    const auto radius = lnf.cornerRadius();
    const auto& info = ids::profileInfos[static_cast<size_t> (index)];

    g.setColour (lnf.token (isActive() ? theme::Token::active : theme::Token::panel));
    g.fillRoundedRectangle (area.toFloat(), radius);

    g.setColour (lnf.token (isActive() ? theme::Token::active : theme::Token::line));
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), radius, 1.0f);

    auto inner = area.reduced (side::kProfilePadX + side::kBorder,
                               side::kProfilePadY + side::kBorder);

    auto name = inner.removeFromTop (textBox (type::Style::profileName));

    if (isActive())
    {
        // `float: right` on the ● — css:405. Taken off the NAME's row, so the
        // name keeps the rest of it.
        //
        // TWO STATEMENTS. Writing it as one passes `name` to `centredInRow`
        // while `removeFromRight` mutates it in another argument, and argument
        // evaluation order is unspecified — benign only because `centredInRow`
        // reads the Y and height that `removeFromRight` does not touch, an
        // invariant living in another file. `KitOverlay` records /code-review
        // hoisting exactly this shape out of its own layout. /code-review.
        const auto dotBox = name.removeFromRight (side::kActiveDotSize)
                                .withHeight (side::kActiveDotSize);
        const auto dot = centredInRow (name, dotBox);

        g.setColour (theme::accent (theme::Accent::zabumba));
        g.fillEllipse (dot.toFloat());
    }

    // `--bg` on the active button, which is the ground it sits on — css:402.
    g.setColour (lnf.token (isActive() ? theme::Token::bg : theme::Token::fg));
    type::drawTracked (g, type::Style::profileName,
                       juce::String (juce::CharPointer_UTF8 (info.displayName)),
                       name.toFloat(), juce::Justification::centredLeft);

    if (! isActive())
        return;

    inner.removeFromTop (side::kDescriptionMarginTop);

    // BLACK at 0.6 in dark, WHITE at 0.7 in light — css:403 and css:404, two
    // rules rather than one colour at one alpha. `--active` inverts between the
    // themes, so `--bg` at a single alpha read correctly in dark and wrongly in
    // light.
    const auto dark = lnf.getMode() == theme::Mode::dark;

    g.setColour ((dark ? juce::Colours::black : juce::Colours::white)
                     .withAlpha (dark ? side::kDescriptionAlpha : side::kDescriptionAlphaLight));

    auto lineBox = inner.withHeight (descriptionLineHeight());

    for (const auto* line : info.description)
    {
        type::drawTracked (g, type::Style::profileDescription,
                           juce::String (juce::CharPointer_UTF8 (line)),
                           lineBox.toFloat(), juce::Justification::centredLeft);

        lineBox = lineBox.translated (0, lineBox.getHeight());
    }
}

} // namespace forrobox
