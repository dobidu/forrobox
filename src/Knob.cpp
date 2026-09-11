#include "Knob.h"

namespace forrobox
{

Knob::Knob (ForroBoxLookAndFeel& lookAndFeelToUse, int dialSizePx, Polarity polarityToUse,
            juce::Colour arcColour, juce::String microLabel)
    : lnf (lookAndFeelToUse),
      dialSize (dialSizePx),
      polarity (polarityToUse),
      accentColour (arcColour),
      label (std::move (microLabel))
{
    setWantsKeyboardFocus (true);

    // `cursor: ns-resize` on the dial (css:358). Vertical drag is the gesture.
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

int Knob::preferredHeight (int dialSizePx, bool hasLabel) noexcept
{
    if (! hasLabel)
        return dialSizePx;

    return dialSizePx + knob::kLabelGap + knob::kLabelHeight;
}

float Knob::unitScale() const noexcept
{
    return static_cast<float> (dialSize) / knob::kViewBox;
}

juce::Rectangle<float> Knob::dialBounds() const noexcept
{
    // Centred horizontally, top-aligned: `.fb-knob` is a column flex with
    // align-items center (css:357), and the label sits below.
    const auto size = static_cast<float> (dialSize);

    return { (static_cast<float> (getWidth()) - size) * 0.5f, 0.0f, size, size };
}

juce::Rectangle<int> Knob::labelBounds() const noexcept
{
    if (label.isEmpty())
        return {};

    const auto top = dialSize + knob::kLabelGap;

    return { 0, top, getWidth(), getHeight() - top };
}

void Knob::paintArc (juce::Graphics& g, juce::Rectangle<float> dial,
                     float fromDeg, float toDeg, juce::Colour colour) const
{
    if (std::abs (toDeg - fromDeg) < 1.0e-4f)
        return;   // a zero-extent arc: a bipolar knob sitting exactly at centre

    const auto scale  = unitScale();
    const auto centre = dial.getCentre();
    const auto radius = knob::kArcRadius * scale;

    juce::Path path;

    // JUCE's addCentredArc takes radians with 0 at twelve o'clock, increasing
    // clockwise — the same convention controls.js uses once its own -90 deg
    // offset is applied, so the spec's degrees map straight across with no
    // second transform to get wrong.
    path.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                        juce::degreesToRadians (juce::jmin (fromDeg, toDeg)),
                        juce::degreesToRadians (juce::jmax (fromDeg, toDeg)),
                        true);

    g.setColour (colour);
    g.strokePath (path, juce::PathStrokeType (knob::kArcStroke * scale,
                                              juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
}

void Knob::paint (juce::Graphics& g)
{
    const auto dial   = dialBounds();
    const auto scale  = unitScale();
    const auto centre = dial.getCentre();

    // Painted in the order controls.js appends the SVG children (:70):
    // track, hub, value arc, line. The value arc must come after the track so
    // it covers it, and the line after the hub so it sits on top.

    // ── track arc: the full sweep, always visible (css:360) ────────────────
    paintArc (g, dial, knob::kSweepStartDeg, knob::kSweepEndDeg,
              lnf.token (theme::Token::lineStrong));

    // ── hub (css:362) ──────────────────────────────────────────────────────
    {
        const auto hubRadius = knob::kHubRadius * scale;
        const auto hub = juce::Rectangle<float> (hubRadius * 2.0f, hubRadius * 2.0f)
                             .withCentre (centre);

        g.setColour (lnf.token (theme::Token::hub));
        g.fillEllipse (hub);

        // The focus ring IS the hub stroke turning --active (css:359) — not an
        // extra ring drawn around the knob.
        g.setColour (showFocusRing ? lnf.token (theme::Token::active)
                                   : lnf.token (theme::Token::line));
        g.drawEllipse (hub, knob::kHubStroke * scale);
    }

    // ── value arc ──────────────────────────────────────────────────────────
    {
        const auto valueDeg = knob::angleForProportion (proportion);

        // Unipolar grows from the sweep's start; bipolar from its centre, in
        // either direction. Polarity changes the ARC only — the indicator line
        // below points to the same angle in both.
        const auto fromDeg = (polarity == Polarity::bipolar) ? knob::kCentreDeg
                                                             : knob::kSweepStartDeg;

        paintArc (g, dial, fromDeg, valueDeg,
                  theme::saturated (accentColour, lnf.accentIntensity(),
                                    knob::kSaturationFloor, knob::kSaturationRange));
    }

    // ── indicator line: the knob's identity (css:363) ──────────────────────
    {
        const auto tipLength = (knob::kViewBox * 0.5f - knob::kIndicatorTipY) * scale;
        const auto radians   = juce::degreesToRadians (knob::angleForProportion (proportion));

        // Rotated about the centre, exactly as controls.js rotates the line
        // element (`rotate(ang 50 50)`, :112).
        const juce::Point<float> tip { centre.x + std::sin (radians) * tipLength,
                                       centre.y - std::cos (radians) * tipLength };

        g.setColour (lnf.token (theme::Token::fg));
        g.drawLine ({ centre, tip }, knob::kIndicatorStroke * scale);
    }

    // ── micro-label (css:364) ──────────────────────────────────────────────
    if (label.isNotEmpty())
    {
        g.setColour (lnf.token (theme::Token::fgFaint));
        type::drawTracked (g, type::Style::knobMicroLabel, label,
                           labelBounds().toFloat(), juce::Justification::centred);
    }
}

void Knob::setProportion (float newProportion)
{
    const auto clamped = juce::jlimit (0.0f, 1.0f, newProportion);

    if (! juce::approximatelyEqual (clamped, proportion))
    {
        proportion = clamped;
        repaint();
    }
}

void Knob::setShowingFocusRing (bool shouldShow)
{
    if (showFocusRing != shouldShow)
    {
        showFocusRing = shouldShow;
        repaint();
    }
}

} // namespace forrobox
