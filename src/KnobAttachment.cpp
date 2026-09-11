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
        // The TEXT is validated, not the resulting float. getValueForText
        // bottoms out in String::getFloatValue/getIntValue, which return 0 for
        // anything unparseable rather than NaN — so an isfinite() guard passes
        // for "hello" and slams the parameter to its minimum while reporting
        // success. Requiring a digit is what actually rejects junk.
        if (! text.containsAnyOf ("0123456789"))
            return false;

        // The parameter parses its own text, so "L20", "+3" and "82" all work
        // without the knob inventing a second formatter to disagree with.
        const auto normalised = parameter.getValueForText (text.trim());

        if (! std::isfinite (normalised))
            return false;

        attachment.setValueAsCompleteGesture (parameter.convertFrom0to1 (normalised));
        return true;
    };

    attachment.sendInitialUpdate();
}

KnobAttachment::~KnobAttachment()
{
    // Every callback above captures `this`, so leaving them installed on a knob
    // that outlives the attachment turns the next mouse event into a
    // use-after-free. Today that cannot happen only because Chassis declares
    // its knobs before its attachments and destruction runs in reverse — an
    // ordering nothing states and a future detach/re-attach would not respect.
    // Clearing them here makes the class answer for itself.
    knob.onDragTo = nullptr;
    knob.onNudge = nullptr;
    knob.onReset = nullptr;
    knob.onGestureStart = nullptr;
    knob.onGestureEnd = nullptr;
    knob.getDisplayText = nullptr;
    knob.onTextEntered = nullptr;
}

double KnobAttachment::intervalSize() const noexcept
{
    const auto& range = parameter.getNormalisableRange();

    // ONE definition of "one interval", shared by both callers. They used to
    // disagree: coarseIntervals() treated a continuous parameter's interval as
    // 1 unit while nudge() treated it as span/100, so the coarse wheel step
    // came out span^2/5000 instead of span/50. Unreachable with today's four
    // knob parameters, all of which have interval 1 — and wrong by 2.6x for the
    // first continuous one, such as a 40..300 BPM knob.
    if (range.interval > 0.0f)
        return static_cast<double> (range.interval);

    return static_cast<double> (range.end - range.start) / 100.0;
}

double KnobAttachment::coarseIntervals() const noexcept
{
    const auto& range = parameter.getNormalisableRange();
    const auto span = static_cast<double> (range.end - range.start);

    // `Math.max(1, range / 50)` — controls.js:161. The prototype multiplies
    // that by `step` to get a VALUE delta; here it is already a count of
    // intervals, so a parameter with a coarse interval does not get a coarse
    // multiplier on top of it.
    return juce::jmax (1.0, span / 50.0 / intervalSize());
}

void KnobAttachment::nudge (int direction, bool fine)
{
    const auto& range = parameter.getNormalisableRange();

    // One interval is the finest move a snapped parameter has. controls.js uses
    // `step * 0.2` for the fine case, which on a step of 1 quantises straight
    // back to where it started — so shift-wheel is a no-op for every integer
    // control in the prototype. PLANNING.md:368 asks for "fine steps"; one
    // interval is what that means here.
    const auto interval = intervalSize();
    const auto steps = fine ? 1.0 : coarseIntervals();
    const auto delta = static_cast<double> (direction) * steps * interval;

    const auto current = static_cast<double> (parameter.convertFrom0to1 (parameter.getValue()));
    const auto target = juce::jlimit (static_cast<double> (range.start),
                                      static_cast<double> (range.end),
                                      current + delta);

    attachment.setValueAsCompleteGesture (static_cast<float> (target));
}

} // namespace forrobox
