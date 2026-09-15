#include "Playhead.h"

#include <cmath>

namespace forrobox
{

Playhead::Playhead()
{
    // It is decoration over the pads. A click belongs to whatever pad is under
    // it, and a 49 px-wide component swallowing clicks across the whole
    // sequencer would make one column of every row un-editable.
    setInterceptsMouseClicks (false, false);

    // Two DropShadows and a gradient, repainted 60 times a second at an
    // unchanging SIZE — `boundsForLineAt` always returns 49 x rowsHeight, and
    // only the x moves. JUCE invalidates a cached image on resize or while not
    // showing, so a move is a pure blit. /simplify measured 32.9 us of painting
    // against 7.2 us of blitting.
    setBufferedToImage (true);
}

juce::Rectangle<int> Playhead::boundsForLineAt (int centreX, juce::Rectangle<int> rowsArea) noexcept
{
    namespace ph = playhead;

    const auto left  = centreX - ph::kLineWidth / 2 - ph::kTrailWidth - ph::kGlowRadius;
    const auto right = centreX + ph::kLineWidth / 2 + 1 + ph::kGlowRadius;

    return juce::Rectangle<int>::leftTopRightBottom (
        left,
        rowsArea.getY() - ph::kOverhang - ph::kGlowRadius,
        right,
        rowsArea.getBottom() + ph::kOverhang + ph::kGlowRadius);
}

int Playhead::lineCentreFor (double position, juce::Rectangle<int> pads, int stepCount) noexcept
{
    if (stepCount <= 0 || pads.isEmpty())
        return pads.getX();

    // Wrapped the way the clock wraps it. `std::fmod (-0.2, 16.0)` is -0.2, not
    // 15.8, so a raw fmod puts the line off the left edge for the first
    // `outputDelaySamples()` after Play — which is exactly when the user is
    // looking at it.
    const auto window = static_cast<double> (stepCount);
    const auto wrapped = std::fmod (std::fmod (position, window) + window, window);

    const auto index = juce::jlimit (0, stepCount - 1, static_cast<int> (std::floor (wrapped)));
    const auto frac  = wrapped - std::floor (wrapped);

    // Between the CENTRES the pads actually have, so the line cannot disagree
    // with the pad it is over.
    const auto here = SequencerLayout::padBounds (pads, index, stepCount).getCentreX();

    // The last step sweeps to the strip's right EDGE and no further, then the
    // wrap puts the line back on step 0's centre. Two alternatives were tried
    // and both are worse: continuing past the edge draws the line over the
    // region's padding, which reads as a rendering fault rather than as a wrap
    // (a test caught it); and the prototype's own behaviour is to animate
    // BACKWARDS across the whole strip over one step duration, because CSS
    // interpolates to step 0's centre from wherever it was. A hardware
    // sequencer runs off the end and starts again, so that is what this does.
    const auto next = index + 1 < stepCount
                        ? SequencerLayout::padBounds (pads, index + 1, stepCount).getCentreX()
                        : pads.getRight();

    return juce::roundToInt (static_cast<double> (here)
                             + frac * static_cast<double> (next - here));
}

void Playhead::paint (juce::Graphics& g)
{
    namespace ph = playhead;

    const auto accent = theme::accent (theme::Accent::pandeiro);

    // The line's own box inside this component: everything left of it is trail
    // and glow margin.
    const auto body = getLocalBounds()
                          .reduced (ph::kGlowRadius, ph::kGlowRadius)
                          .withTrimmedLeft (ph::kTrailWidth);

    if (body.isEmpty())
        return;

    // ── the trailing column (css:494-498) ───────────────────────────────────
    //
    // BEHIND the line, fading away from it: `linear-gradient(90deg,
    // transparent, <pandeiro 16%>)` runs left-to-right INTO the line, so the
    // strong end is the one touching it.
    {
        const auto trail = juce::Rectangle<int> (getLocalBounds().getX() + ph::kGlowRadius,
                                                 body.getY(),
                                                 ph::kTrailWidth,
                                                 body.getHeight());

        g.setGradientFill (juce::ColourGradient (
            accent.withAlpha (0.0f), (float) trail.getX(), 0.0f,
            accent.withAlpha (ph::kTrailAlpha), (float) trail.getRight(), 0.0f, false));

        g.fillRect (trail);
    }

    // ── the glow: 10 px of accent, then a 3 px white core ───────────────────
    //
    // juce::DropShadow for the same reason the accent bar uses it (Chassis.cpp):
    // a stack of expanded rectangles at low alpha renders a hard-edged frame
    // rather than a blur, which is clearly visible in the light theme.
    juce::DropShadow (accent, ph::kGlowRadius, {}).drawForRectangle (g, body);
    juce::DropShadow (juce::Colours::white.withAlpha (0.55f), ph::kCoreGlowRadius, {})
        .drawForRectangle (g, body);

    // ── the line (css:488) ──────────────────────────────────────────────────
    //
    // `linear-gradient(180deg, <pandeiro mixed 90% with white>, <pandeiro>)`.
    g.setGradientFill (juce::ColourGradient (
        accent.interpolatedWith (juce::Colours::white, ph::kTopWhiteMix),
        0.0f, (float) body.getY(),
        accent, 0.0f, (float) body.getBottom(), false));

    g.fillRoundedRectangle (body.toFloat(), ph::kCornerRadius);
}

} // namespace forrobox
