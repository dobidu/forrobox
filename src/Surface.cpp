#include "Surface.h"

namespace forrobox
{

double cubicBezierEase (double t, double x1, double y1, double x2, double y2) noexcept
{
    // A cubic-bezier easing is a PARAMETRIC curve: x and y are both cubics in a
    // parameter s, and the easing is y at the s where x == t. `smoothstep` looks
    // like it and is a different function.
    const auto clamped = juce::jlimit (0.0, 1.0, t);

    const auto bezier = [] (double a, double b, double s)
    {
        const auto u = 1.0 - s;
        return 3.0 * u * u * s * a + 3.0 * u * s * s * b + s * s * s;
    };

    // Newton would need the derivative and can stall where the curve is flat;
    // bisection over a monotonic x is short, exact enough for an animation, and
    // has no failure mode to reason about.
    auto low = 0.0, high = 1.0;

    for (int i = 0; i < 24; ++i)
    {
        const auto mid = (low + high) * 0.5;

        if (bezier (x1, x2, mid) < clamped)
            low = mid;
        else
            high = mid;
    }

    return bezier (y1, y2, (low + high) * 0.5);
}

} // namespace forrobox

namespace forrobox::surface
{

void raisedHighlight (juce::Graphics& g, juce::Rectangle<int> area, juce::Colour highlight)
{
    g.setColour (highlight);
    g.fillRect (area.getX(), area.getY(), area.getWidth(), 1);
}

void wellShadow (juce::Graphics& g, juce::Rectangle<int> area, juce::Colour shadow, float depth)
{
    const auto height = juce::jmin (static_cast<float> (area.getHeight()), depth * 3.0f);

    g.setGradientFill (juce::ColourGradient::vertical (shadow, static_cast<float> (area.getY()),
                                                       shadow.withAlpha (0.0f),
                                                       static_cast<float> (area.getY()) + height));
    g.fillRect (area.withHeight (juce::roundToInt (height)));
}

void glowDot (juce::Graphics& g, juce::Rectangle<int> box, juce::Colour colour,
              int glowRadius)
{
    juce::DropShadow (colour, glowRadius, {}).drawForRectangle (g, box);

    g.setColour (colour);
    g.fillEllipse (box.toFloat());
}

} // namespace forrobox::surface
