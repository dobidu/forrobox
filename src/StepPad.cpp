#include "StepPad.h"

namespace forrobox
{

StepPad::StepPad (ForroBoxLookAndFeel& lookAndFeelToUse, juce::Colour instrumentColour)
    : lnf (lookAndFeelToUse), colour (instrumentColour)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void StepPad::paintUnlit (juce::Graphics& g, juce::Rectangle<float> area, float radius) const
{
    const auto dark = lnf.getMode() == theme::Mode::dark;

    const auto black = [] (float a) { return juce::Colour::fromFloatRGBA (0.0f, 0.0f, 0.0f, a); };
    const auto white = [] (float a) { return juce::Colour::fromFloatRGBA (1.0f, 1.0f, 1.0f, a); };

    // Two ROWS, not one row at a different alpha — the light theme's gradient
    // runs black-to-black where the dark theme's runs white-to-black.
    g.setGradientFill (juce::ColourGradient::vertical (
        dark ? white (pad::kOffTopWhite) : black (pad::kOffTopBlackLight), area.getY(),
        dark ? black (pad::kOffBottomBlack) : black (pad::kOffBottomBlackLight), area.getBottom()));
    g.fillRoundedRectangle (area, radius);

    // `inset 0 1px 1px rgba(0,0,0,<a>)` — the recessed top edge.
    //
    // A beat pad takes the DARK alpha in both themes: css:632 re-declares the
    // whole box-shadow at the same specificity as css:621's light override and
    // comes later, so it replaces it outright. box-shadow is one property, not
    // an additive list across rules.
    g.setColour (black (dark || beat ? pad::kInsetTopAlphaDark : pad::kInsetTopAlphaLight));
    g.fillRect (area.withHeight (1.0f).reduced (radius * 0.5f, 0.0f));

    // `inset 0 0 0 1px rgba(0,0,0,0.25)` — css:619, dark theme only, and gone
    // on a beat pad for the same replacement reason: css:632 lists the
    // `--line-strong` ring in this one's place rather than alongside it.
    if (dark && ! beat)
    {
        g.setColour (black (pad::kInsetRingAlpha));
        g.drawRoundedRectangle (area.reduced (0.5f), radius, 1.0f);
    }
}

void StepPad::paintLit (juce::Graphics& g, juce::Rectangle<float> area, float radius) const
{
    // `0 0 9px <c at accent-i x 45%>` — the pad's own glow law, and an OUTER
    // shadow, so it is painted first and the ground covers the part of it that
    // falls inside the pad. The component reserves room for the rest; see
    // StepPad::boundsForPadRect.
    const auto glow = colour.withAlpha (lnf.accentIntensity() * pad::kLitGlowOpacity);
    juce::DropShadow (glow, pad::kLitGlowRadius, {}).drawForRectangle (g, area.toNearestInt());

    // ── the ellipse ────────────────────────────────────────────────────────
    //
    // A CIRCULAR gradient of radius ry, stretched in x by rx/ry through the
    // FillType's own transform. See pad::kLitRadiusX for why this shape and not
    // Graphics::addTransform.
    const auto rx = pad::kLitRadiusX * area.getWidth();
    const auto ry = pad::kLitRadiusY * area.getHeight();

    const juce::Point<float> origin { area.getX() + pad::kLitOriginX * area.getWidth(),
                                      area.getY() + pad::kLitOriginY * area.getHeight() };

    const auto centreColour = theme::mix (colour, juce::Colours::white,
                                          theme::mixWeight (pad::kLitBasePct,
                                                            pad::kLitCentreWhitePct));

    juce::ColourGradient gradient (centreColour, origin,
                                   colour, origin.translated (0.0f, ry), true);

    // `var(--c) 70%` — constant from the 70% stop out to the edge.
    gradient.addColour (pad::kLitOuterStop, colour);

    juce::FillType fill (gradient);
    fill.transform = juce::AffineTransform::scale (rx / ry, 1.0f, origin.x, origin.y);

    g.setFillType (fill);
    g.fillRoundedRectangle (area, radius);
    g.setFillType (juce::FillType());

    // `inset 0 1px 0 <c + 35% white>` — the top sheen.
    g.setColour (theme::mix (colour, juce::Colours::white,
                             theme::mixWeight (pad::kLitBasePct, pad::kLitSheenWhitePct)));
    g.fillRect (area.withHeight (1.0f).reduced (radius * 0.5f, 0.0f));
}

void StepPad::paint (juce::Graphics& g)
{

    const auto area = padRect().toFloat();
    const auto radius = lnf.cornerRadius();

    if (pressed)
    {
        const auto centre = area.getCentre();
        g.addTransform (juce::AffineTransform::scale (pad::kPressScale, pad::kPressScale,
                                                      centre.x, centre.y));
    }

    // ── velocity ───────────────────────────────────────────────────────────
    //
    // The prototype sets `pad.style.opacity`, which is an ELEMENT opacity: the
    // whole pad — ground, sheen, glow, dot and border — is composited as one
    // group and then blended once. Painting each layer at that alpha instead
    // would blend the sheen against an already-faded ground and give a
    // different result, so this is a real transparency layer and not an alpha
    // threaded through every setColour.
    //
    // Full velocity lands on exactly 1.0, so the common case takes no layer.
    const auto opacity = isLit() ? pad::opacityForVelocity (velocity) : 1.0f;
    const auto grouped = opacity < 1.0f;

    if (grouped)
        g.beginTransparencyLayer (opacity);

    if (isLit())
        paintLit (g, area, radius);
    else
        paintUnlit (g, area, radius);

    // ── the ghost dot ──────────────────────────────────────────────────────
    //
    // `.pad.ghost::after` — a 3 px circle at 90%, on top of whatever ground the
    // pad already has. It is drawn inside the transparency layer because the
    // prototype's opacity is on the parent element and `::after` is its child.
    if (isGhost())
    {
        const auto dot = juce::Rectangle<float> (pad::kGhostDotSize, pad::kGhostDotSize)
                             .withCentre (area.getCentre());

        g.setColour (colour.withMultipliedAlpha (pad::kGhostDotOpacity));
        g.fillEllipse (dot);
    }

    // ── the beat ring ──────────────────────────────────────────────────────
    //
    // `--line-strong`, NOT `--line`. `.pad.beat` is declared twice — css:481
    // with `--line` and css:632 with `--line-strong` — at equal specificity, so
    // the later rule is what renders. `PLANNING.md:455` says `--line` and is the
    // losing copy; the stylesheet wins, this project's standing rule for
    // spec/reference conflicts.
    //
    // And ONLY when unlit: css:633 gives `.pad.on.beat` the lit shadow with no
    // ring at all, so the beat marker is invisible on a lit pad. That is the
    // stylesheet's intent and is asserted, so a later "fix" that draws the ring
    // on lit pads fails rather than passing as an improvement.
    // `.pad:hover { border-color: --line-strong }` (css:471) is the same ring in
    // the same colour, and it is a BORDER, so it sits inside the opacity group
    // too. ONE draw: written as two blocks, a hovered beat pad composited
    // `--line-strong` over itself at double alpha — and the pairwise test could
    // not see it, because it renders beat and hover as separate states and
    // never combines them. Found by /simplify.
    if (hovered || (beat && ! isLit()))
    {
        g.setColour (lnf.token (theme::Token::lineStrong));
        g.drawRoundedRectangle (area.reduced (0.5f), radius, 1.0f);
    }

    if (grouped)
        g.endTransparencyLayer();
}

void StepPad::setVelocity (int newVelocity)
{
    const auto clamped = juce::jlimit (0, 127, newVelocity);

    if (velocity != clamped)
    {
        velocity = clamped;
        repaint();
    }
}

void StepPad::setDimmed (bool shouldDim)
{
    if (dimmed == shouldDim)
        return;

    dimmed = shouldDim;

    // Component alpha, NOT g.setOpacity at the top of paint: Graphics::setColour
    // and setGradientFill both call ContextType::setFill, which REPLACES the
    // fill alpha, so an opacity set before the first fill is discarded. This
    // composites the finished pad — ground, glow, ring and ghost dot together —
    // and leaves the playhead crossing the row alone, which a scrim would not.
    setAlpha (dimmed ? pad::kDimmedAlpha : 1.0f);
}

void StepPad::setBeat (bool isBeatStep)
{
    if (beat != isBeatStep)
    {
        beat = isBeatStep;
        repaint();
    }
}

void StepPad::mouseEnter (const juce::MouseEvent&)
{
    hovered = true;
    repaint();
}

void StepPad::mouseExit (const juce::MouseEvent&)
{
    hovered = false;
    pressed = false;
    repaint();
}

void StepPad::mouseDown (const juce::MouseEvent& e)
{
    // Right-click belongs to the host, exactly as the knob and the button leave
    // it (PLANNING.md:876-878).
    if (e.mods.isPopupMenu())
        return;

    pressed = true;
    repaint();
}

void StepPad::mouseUp (const juce::MouseEvent& e)
{
    if (! pressed)
        return;

    pressed = false;
    repaint();

    // Fires only when released INSIDE the pad — hitTest already excludes the
    // glow margin, so this is the pad's own rect and not the bounds.
    if (padRect().contains (e.getPosition()) && onClick != nullptr)
        onClick();
}

} // namespace forrobox
