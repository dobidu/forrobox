#include "GearButton.h"

#include "Surface.h"
#include "Theme.h"

namespace forrobox
{

GearButton::GearButton (ForroBoxLookAndFeel& lookAndFeelToUse)
    : lnf (lookAndFeelToUse)
{
    setSize (gear::kSize, gear::kSize);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);

    // The host reads this; a control with no name is a control a screen reader
    // announces as nothing. PLANNING.md:868-872's accessibility notes give the
    // knobs and STYLE explicit roles, and this follows that rather than being
    // the one silent affordance.
    setTitle ("Settings");
    setDescription ("Open the settings menu");
}

juce::Path GearButton::shapeFor (juce::Rectangle<float> box) noexcept
{
    juce::Path path;

    const auto centre = box.getCentre();
    const auto extent = juce::jmin (box.getWidth(), box.getHeight());

    const auto outer = extent * gear::kOuterRatio;
    const auto inner = extent * gear::kInnerRatio;
    const auto bore  = extent * gear::kBoreRatio;

    // Alternating outer and inner radii, two vertices each, so the teeth have
    // flat tops rather than points — a star reads as a star, not as a gear.
    constexpr auto segments = gear::kTeeth * 4;

    for (int i = 0; i < segments; ++i)
    {
        const auto radius = ((i / 2) % 2 == 0) ? outer : inner;
        const auto angle  = juce::MathConstants<float>::twoPi
                          * (static_cast<float> (i) / static_cast<float> (segments));

        const auto x = centre.x + radius * std::cos (angle);
        const auto y = centre.y + radius * std::sin (angle);

        if (i == 0)
            path.startNewSubPath (x, y);
        else
            path.lineTo (x, y);
    }

    path.closeSubPath();

    // The bore, cut by EVEN-ODD winding. `setUsingNonZeroWinding (false)` below
    // is what makes it a hole: under even-odd a sub-path inside another is
    // unfilled whatever direction it was drawn in, so `addEllipse`'s own
    // direction is irrelevant. An earlier version of this comment claimed the
    // hole came from drawing it the opposite way round, which is not how the
    // rule works and would have sent the next reader looking at `addEllipse`.
    // /code-review.
    juce::Path hole;
    hole.addEllipse (centre.x - bore, centre.y - bore, bore * 2.0f, bore * 2.0f);

    path.addPath (hole);
    path.setUsingNonZeroWinding (false);

    return path;
}

void GearButton::paint (juce::Graphics& g)
{
    // --fg-dim at rest, --fg on hover. The same two tokens every other quiet
    // affordance in this chassis moves between, so the gear does not introduce
    // a third hover language.
    g.setColour (lnf.token (hovered ? theme::Token::fg : theme::Token::fgDim));
    g.fillPath (shapeFor (getLocalBounds().toFloat()));
}

void GearButton::mouseEnter (const juce::MouseEvent&)
{
    hovered = true;
    repaint();
}

void GearButton::mouseExit (const juce::MouseEvent&)
{
    hovered = false;
    repaint();
}

void GearButton::mouseUp (const juce::MouseEvent& event)
{
    // `isPlainClickInside`, which carries TWO clauses where this had one.
    //
    // A release inside, so a press dragged away is cancelled — that half was
    // here. The half that was MISSING is that a right-click belongs to the
    // HOST: this opened the settings menu on right-click AND swallowed the
    // DAW's own parameter/automation context menu. `HitZone.h`'s header
    // records that /simplify has found this exact clause dropped from "every
    // container that had hand-rolled its own hit test, because each one
    // restated the dispatch and each one forgot the same clause" — and this is
    // the next one. 04-02's Alt+click decision is the same law from the other
    // side. /simplify.
    if (onClick != nullptr && isPlainClickInside (event, getLocalBounds()))
        onClick();
}

} // namespace forrobox
