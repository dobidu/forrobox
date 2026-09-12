#include "Button.h"

namespace forrobox
{

Button::Button (ForroBoxLookAndFeel& lookAndFeelToUse, Variant variantToUse,
                juce::String label, OnStyle onStyleToUse)
    : lnf (lookAndFeelToUse),
      variant (variantToUse),
      text (std::move (label)),
      onStyle (onStyleToUse)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

int Button::preferredWidth() const
{
    const auto& spec = specFor (variant);

    if (spec.fixedWidth > 0)
        return spec.fixedWidth;

    // The label's own tracked width — type::trackedWidth, not a guess, and the
    // same walk drawTracked performs so the two cannot disagree.
    return juce::roundToInt (type::trackedWidth (spec.labelStyle, text))
         + spec.padX * 2 + kBorderWidth * 2;
}

int Button::preferredHeight() const
{
    return heightOf (variant);
}

void Button::setIcon (Icon newIcon)
{
    icon = std::move (newIcon);
    hasIcon = true;
    repaint();
}

void Button::paint (juce::Graphics& g)
{
    const auto& spec = specFor (variant);
    const auto radius = lnf.cornerRadius();

    // The press scale is applied to the CONTENT, not to the bounds: a button
    // that shrank its own bounds would re-flow the row it sits in.
    if (pressed)
    {
        const auto centre = contentBox().toFloat().getCentre();
        g.addTransform (juce::AffineTransform::scale (spec.pressScale, spec.pressScale,
                                                      centre.x, centre.y));
    }

    const auto area = contentBox().toFloat().reduced (kBorderWidth * 0.5f);

    // ── ground ─────────────────────────────────────────────────────────────
    //
    // Three on-colours from three CSS rules. `--danger` and `--c-pandeiro` are
    // tokens; the two TEXT colours are literals the stylesheet writes out, so
    // they live in theme:: as named constants rather than here.
    const auto litGround = [this]
    {
        switch (onStyle)
        {
            case OnStyle::mute:   return lnf.token (theme::Token::danger);
            case OnStyle::solo:   return theme::accent (theme::Accent::pandeiro);
            case OnStyle::active: break;
        }

        return lnf.token (theme::Token::active);
    };

    const auto litText = [this]
    {
        switch (onStyle)
        {
            case OnStyle::mute:   return juce::Colour (theme::kMuteOnTextArgb);
            case OnStyle::solo:   return juce::Colour (theme::kSoloOnTextArgb);
            case OnStyle::active: break;
        }

        return lnf.token (theme::Token::bg);
    };

    if (on)
    {
        // `0 0 12px <ganza at 55%>` — the playing transport button's glow
        // (css:637). An OUTER shadow, so it is drawn before the ground; only
        // this variant has one, and only when lit.
        if (variant == Variant::transport)
            juce::DropShadow (litGround().withAlpha (kTransportGlowOpacity),
                              kTransportGlowRadius, {})
                .drawForRectangle (g, area.toNearestInt());

        g.setColour (litGround());
        g.fillRoundedRectangle (area, radius);
    }
    else if (spec.groundIsPanel)
    {
        // The arrow and the transport button have a ground of their own when
        // unlit (css:232, 191) — and the transport's DARKENS on hover, where
        // every other variant changes only its label.
        g.setColour (hovered && spec.hoverDarkensGround ? lnf.token (theme::Token::sunken)
                                                        : lnf.token (theme::Token::panel));
        g.fillRoundedRectangle (area, radius);
    }

    // ── border ─────────────────────────────────────────────────────────────
    //
    // `--line-strong` unlit; the on-state border matches its own ground, so a
    // lit button has no visible outline (css:144, 342, 343).
    g.setColour (on ? litGround()
                    : (hovered && spec.hoverLiftsBorder ? lnf.token (theme::Token::fgDim)
                                                        : lnf.token (theme::Token::lineStrong)));
    g.drawRoundedRectangle (area, radius, static_cast<float> (kBorderWidth));

    // ── label ──────────────────────────────────────────────────────────────
    //
    // Hover lifts --fg-dim to --fg on every variant (css:141, 300, 342, 235).
    // Whether it ALSO lifts the border is per-variant data: css:141 and css:300
    // move it, css:342 and css:235 do not.
    g.setColour (on ? litText()
                    : (hovered ? lnf.token (theme::Token::fg) : lnf.token (theme::Token::fgDim)));

    if (hasIcon)
    {
        // Scaled ONCE from its own viewBox, the knob's rule — the two transport
        // glyphs are authored in a 24x24 box and drawn at 14.
        const auto scale = static_cast<float> (kTransportIcon) / icon.viewBox;
        const auto centre = contentBox().toFloat().getCentre();

        g.fillPath (icon.path,
                    juce::AffineTransform::scale (scale)
                        .translated (centre.x - icon.viewBox * scale * 0.5f,
                                     centre.y - icon.viewBox * scale * 0.5f));
        return;
    }

    type::drawTracked (g, spec.labelStyle, text, contentBox().toFloat(),
                       juce::Justification::centred);
}

void Button::setOn (bool shouldBeOn)
{
    if (on != shouldBeOn)
    {
        on = shouldBeOn;
        repaint();
    }
}

void Button::mouseDown (const juce::MouseEvent& e)
{
    // Right-click belongs to the host, exactly as the knob leaves it
    // (PLANNING.md:876-878) — a button that swallowed it would hide the
    // automation menu for the parameter it drives.
    if (e.mods.isPopupMenu())
        return;

    pressed = true;
    repaint();
}

void Button::mouseUp (const juce::MouseEvent& e)
{
    if (! pressed)
        return;

    pressed = false;
    repaint();

    // Fires only when released INSIDE, which is the convention every other
    // button in every host follows.
    if (contentBox().contains (e.getPosition()) && onClick != nullptr)
        onClick();
}

void Button::mouseEnter (const juce::MouseEvent&)
{
    hovered = true;
    repaint();
}

void Button::mouseExit (const juce::MouseEvent&)
{
    hovered = false;
    pressed = false;
    repaint();
}

} // namespace forrobox
