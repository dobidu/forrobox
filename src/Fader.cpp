#include "Fader.h"

namespace forrobox
{

Fader::Fader (ForroBoxLookAndFeel& lookAndFeelToUse, juce::Colour fillColour)
    : lnf (lookAndFeelToUse), colour (fillColour)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

juce::Rectangle<int> Fader::trackRect() const noexcept
{
    return getLocalBounds()
        .reduced (fader::kThumbOverhang, 0)
        .withSizeKeepingCentre (getWidth() - fader::kThumbOverhang * 2, fader::kTrackHeight);
}

float Fader::proportionForX (int x) const noexcept
{
    const auto track = trackRect();

    if (track.getWidth() <= 0)
        return 0.0f;

    // `n = (e.clientX - r.left) / r.width`, clamped — controls.js:236-237,
    // measured against the TRACK and not the padded box the event arrives on.
    return juce::jlimit (0.0f, 1.0f,
                         static_cast<float> (x - track.getX())
                             / static_cast<float> (track.getWidth()));
}

void Fader::setProportion (float newProportion)
{
    const auto clamped = juce::jlimit (0.0f, 1.0f, newProportion);

    if (! juce::approximatelyEqual (proportion, clamped))
    {
        proportion = clamped;
        repaint();

        if (onProportionChanged != nullptr)
            onProportionChanged (proportion);
    }
}

void Fader::paint (juce::Graphics& g)
{
    const auto track = trackRect().toFloat();

    // `border-radius: 999px` on a 4 px bar is a capsule, so the radius is half
    // the height and not a token.
    const auto radius = track.getHeight() * 0.5f;

    g.setColour (lnf.token (theme::Token::lineStrong));
    g.fillRoundedRectangle (track, radius);

    // `.fb-fader-fill { width: <n>% }` — from the track's left edge, in the
    // instrument colour at the knob's saturation law.
    const auto fillWidth = track.getWidth() * proportion;

    if (fillWidth > 0.0f)
    {
        g.setColour (theme::saturated (colour, lnf.accentIntensity(),
                                       fader::kSaturationFloor, fader::kSaturationRange));
        // No minimum width. juce::Path::addRoundedRectangle already clamps the
        // corner size to half the box (juce_Path.cpp:356), so a fill narrower
        // than the track is tall draws as the sliver the browser draws rather
        // than as a degenerate shape — and a floor would have been an invented
        // law making every proportion under 3% render identically.
        g.fillRoundedRectangle (track.withWidth (fillWidth), radius);
    }

    // The thumb is CENTRED on the proportion point. The prototype offsets it by
    // -5px for a 12 px circle (controls.js:223), which is half of 10 rather
    // than half of 12 and shifts the whole travel one pixel right — a rounding
    // slip, not a design, and the one place here that does not follow it.
    const auto centre = juce::Point<float> (track.getX() + fillWidth, track.getCentreY());

    const auto thumb = juce::Rectangle<float> (fader::kThumbSize, fader::kThumbSize)
                           .withCentre (centre);

    // `0 1px 3px rgba(0,0,0,0.4)` — a real blur, for the reason Chassis records
    // about the accent bar: a stack of expanded ellipses at low alpha renders
    // as hard-edged rings rather than a shadow.
    juce::DropShadow (juce::Colour::fromFloatRGBA (0.0f, 0.0f, 0.0f, fader::kThumbShadowAlpha),
                      fader::kThumbShadowRadius, { 0, fader::kThumbShadowY })
        .drawForRectangle (g, thumb.toNearestInt());

    g.setColour (lnf.token (theme::Token::fg));
    g.fillEllipse (thumb);
}

void Fader::dragTo (const juce::MouseEvent& e)
{
    const auto target = proportionForX (e.getPosition().x);

    // The component's own position is written by the attachment, never here —
    // the same one-writer rule the knob follows, so a host that rejects or
    // quantises the value is what the fader ends up showing.
    if (onDragTo != nullptr)
        onDragTo (target);
}

void Fader::mouseDown (const juce::MouseEvent& e)
{
    // Right-click belongs to the host, exactly as the knob, the button and the
    // pad leave it (PLANNING.md:876-878).
    if (e.mods.isPopupMenu())
        return;

    // The gesture is opened BEFORE the first value, so the click-to-jump that
    // `fromEvent` performs on mousedown is inside the bracket rather than a
    // bare value change ahead of it.
    gestureActive = true;

    if (onGestureStart != nullptr)
        onGestureStart();

    dragTo (e);
}

void Fader::mouseDrag (const juce::MouseEvent& e)
{
    // Only inside a gesture this component opened. A drag that began with a
    // right-click never opened one, and closing an unopened gesture is the
    // fault 04-02's review found in the knob's Alt branch.
    if (gestureActive)
        dragTo (e);
}

void Fader::mouseUp (const juce::MouseEvent&)
{
    if (! gestureActive)
        return;

    gestureActive = false;

    if (onGestureEnd != nullptr)
        onGestureEnd();
}

} // namespace forrobox
