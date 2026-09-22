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

namespace
{
float keyframeValueOver (double phaseSeconds, double periodSeconds,
                         const KeyframeStop* first, const KeyframeStop* last) noexcept
{
    const auto count = static_cast<size_t> (last - first);

    if (count < 2 || periodSeconds <= 0.0)
        return count > 0 ? first->value : 0.0f;

    // fmod twice: a negative phase must wrap forwards rather than run the curve
    // in reverse, and one fmod of a negative number is negative.
    const auto cycle = std::fmod (std::fmod (phaseSeconds, periodSeconds) + periodSeconds,
                                  periodSeconds) / periodSeconds;

    const auto* previous = first;

    for (const auto* stop = first + 1; stop != last; ++stop)
    {
        if (cycle < stop->position || stop + 1 == last)
        {
            const auto span = stop->position - previous->position;

            if (span <= 0.0)
                return previous->value;

            const auto progress = easeInOut ((cycle - previous->position) / span);

            return previous->value
                 + static_cast<float> (progress) * (stop->value - previous->value);
        }

        previous = stop;
    }

    return previous->value;
}
} // namespace

float keyframeValueAt (double phaseSeconds, double periodSeconds,
                       std::initializer_list<KeyframeStop> stops) noexcept
{
    return keyframeValueOver (phaseSeconds, periodSeconds, stops.begin(), stops.end());
}

float keyframeValueAt (double phaseSeconds, double periodSeconds,
                       const std::vector<KeyframeStop>& stops) noexcept
{
    return keyframeValueOver (phaseSeconds, periodSeconds,
                              stops.data(), stops.data() + stops.size());
}

KeyframeLoop::KeyframeLoop (double periodSecondsToUse,
                            std::initializer_list<KeyframeStop> stopsToUse,
                            std::function<void()> onChangedToUse)
    : periodSeconds (periodSecondsToUse),
      stops (stopsToUse),
      onChanged (std::move (onChangedToUse))
{
    // COPIED, not referenced. A `std::initializer_list` does not own its array,
    // so storing the list itself would dangle the moment the constructor's
    // caller returned — and it would do so silently, since the memory usually
    // survives long enough to look correct.
    poll.tick = [this] { advance (poll.secondsSinceLastTick (periodSeconds)); };
}

void KeyframeLoop::setRunning (bool shouldRun)
{
    if (shouldRun == running)
        return;

    running = shouldRun;

    if (running)
    {
        // Re-based, so an animation that has been off for ten minutes gets one
        // frame on its first tick rather than ten minutes of motion at once.
        poll.restart();
        poll.startTimerHz (kUiPollHz);
    }
    else
    {
        poll.stopTimer();
        phase = 0.0;
    }

    if (onChanged != nullptr)
        onChanged();
}

void KeyframeLoop::advance (double seconds)
{
    if (! running)
        return;

    const auto before = value();

    phase += seconds;

    if (onChanged != nullptr && ! juce::approximatelyEqual (before, value()))
        onChanged();
}

float KeyframeLoop::value() const noexcept
{
    // At rest the phase is 0, so this is the track's own 0% stop rather than a
    // separate rest value that can disagree with the curve — which is exactly
    // how the two hand-rolled copies diverged.
    return keyframeValueAt (phase, periodSeconds, stops);
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
