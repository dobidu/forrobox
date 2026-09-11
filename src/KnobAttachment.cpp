#include "KnobAttachment.h"

#include <cmath>

namespace forrobox
{

KnobAttachment::KnobAttachment (juce::RangedAudioParameter& parameterToUse, Knob& knobToUse)
    : parameter (parameterToUse),
      knob (knobToUse),
      attachment (parameterToUse,
                  [this] (float newDenormalisedValue)
                  {
                      // Parameter -> knob. The ONLY writer of the knob's
                      // displayed position, and it never writes back: a
                      // repaint must not become a parameter change, or host
                      // automation would fight itself.
                      knob.setProportion (parameter.convertTo0to1 (newDenormalisedValue));
                  })
{
    knob.onDragTo = [this] (float targetProportion)
    {
        attachment.setValueAsPartOfGesture (parameter.convertFrom0to1 (targetProportion));
    };

    knob.onNudge = [this] (int direction, bool fine) { nudge (direction, fine); };

    knob.onReset = [this]
    {
        // The PARAMETER's default, not the value the knob was built with.
        attachment.setValueAsCompleteGesture (
            parameter.convertFrom0to1 (parameter.getDefaultValue()));
    };

    knob.onGestureStart = [this] { attachment.beginGesture(); };
    knob.onGestureEnd   = [this] { attachment.endGesture(); };

    knob.getDisplayText = [this] { return parameter.getCurrentValueAsText(); };

    knob.onTextEntered = [this] (const juce::String& text)
    {
        // The parameter parses its own text, so "L20", "+3" and "82" all work
        // without the knob inventing a second formatter to disagree with.
        const auto normalised = parameter.getValueForText (text);

        if (! std::isfinite (normalised))
            return false;

        attachment.setValueAsCompleteGesture (parameter.convertFrom0to1 (normalised));
        return true;
    };

    attachment.sendInitialUpdate();
}

double KnobAttachment::coarseIntervals() const noexcept
{
    const auto& range = parameter.getNormalisableRange();
    const auto span = static_cast<double> (range.end - range.start);

    // `Math.max(1, range / 50)` — controls.js:161. The prototype multiplies
    // that by `step` to get a VALUE delta; here it is already a count of
    // intervals, so a parameter with a coarse interval does not get a coarse
    // multiplier on top of it.
    if (range.interval <= 0.0f)
        return juce::jmax (1.0, span / 50.0);   // continuous: one "interval" is 1 unit

    return juce::jmax (1.0, span / 50.0 / static_cast<double> (range.interval));
}

void KnobAttachment::nudge (int direction, bool fine)
{
    const auto& range = parameter.getNormalisableRange();

    // One interval is the finest move a snapped parameter has. controls.js uses
    // `step * 0.2` for the fine case, which on a step of 1 quantises straight
    // back to where it started — so shift-wheel is a no-op for every integer
    // control in the prototype. PLANNING.md:368 asks for "fine steps"; one
    // interval is what that means here.
    const auto interval = range.interval > 0.0f ? static_cast<double> (range.interval)
                                                : static_cast<double> (range.end - range.start) / 100.0;

    const auto steps = fine ? 1.0 : coarseIntervals();
    const auto delta = static_cast<double> (direction) * steps * interval;

    const auto current = static_cast<double> (parameter.convertFrom0to1 (parameter.getValue()));
    const auto target = juce::jlimit (static_cast<double> (range.start),
                                      static_cast<double> (range.end),
                                      current + delta);

    attachment.setValueAsCompleteGesture (static_cast<float> (target));
}

} // namespace forrobox
