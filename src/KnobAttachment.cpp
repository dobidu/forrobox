#include "KnobAttachment.h"

#include <cmath>

namespace forrobox
{

KnobAttachment::KnobAttachment (juce::RangedAudioParameter& parameterToUse, Knob& knobToUse)
    : parameter (parameterToUse),
      // The four callbacks THIS class installs. onDragTo, onGestureStart and
      // onGestureEnd belong to `shared` and are cleared by its own guard.
      //
      // onProportionChanged is NOT here, though the hand-written destructor
      // this replaced cleared it. Nobody installs it in this file: the readout
      // seam at HeaderBar.cpp:192 does, and its lambda captures the parameter
      // and the screen rather than `this`, so it does not dangle when the
      // attachment dies. Clearing a callback this class never installed is what
      // ProportionAttachment<Fader> already declines to do for Chassis.cpp:551's
      // ghost readout — the two seams now agree. Found by /code-review on 05-01.
      knob (knobToUse,
            [] (Knob& k)
            {
                k.onNudge = nullptr;
                k.onReset = nullptr;
                k.getDisplayText = nullptr;
                k.onTextEntered = nullptr;
            }),
      // The parameter -> knob update, the drag and the gesture bracket. Shared
      // with the fader, which needs those three and nothing below them.
      shared (parameterToUse, knobToUse)
{
    knobToUse.onNudge = [this] (int direction, bool fine) { nudge (direction, fine); };

    knobToUse.onReset = [this]
    {
        // The PARAMETER's default, not the value the knob was built with.
        shared.getAttachment().setValueAsCompleteGesture (
            parameter.convertFrom0to1 (parameter.getDefaultValue()));
    };

    knobToUse.getDisplayText = [this] { return parameter.getCurrentValueAsText(); };

    knobToUse.onTextEntered = [this] (const juce::String& text)
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

        shared.getAttachment().setValueAsCompleteGesture (parameter.convertFrom0to1 (normalised));
        return true;
    };

    // LAST, so the knob's full seam is installed before the parameter's current
    // value arrives through it.
    shared.sendInitialUpdate();
}

double KnobAttachment::intervalSize() const noexcept
{
    const auto& range = parameter.getNormalisableRange();

    // A PRECONDITION, not a runtime fallback. Every parameter in this plugin's
    // layout has interval > 0 — percentRange() is {0,100,1}, the ints are
    // {-12,12,1} and {-kPanExtent,kPanExtent,1}, bool is {0,1,1}, choice is
    // {0,n-1,1} — and the IDs are frozen by a project boundary.
    //
    // There was a span/100 else-branch here, justified by a comment naming "a
    // 40..300 BPM knob" as the case it defended. That parameter already exists
    // and is an AudioParameterInt (PluginProcessor.cpp:683), so the branch was
    // dead on arrival, and the test guarding it ran on VOL where both
    // definitions agree — it could not fail for the regression it named.
    jassert (range.interval > 0.0f);

    return static_cast<double> (range.interval);
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

    const auto current = parameter.convertFrom0to1 (parameter.getValue());

    // snapToLegalValue clamps AND quantises to the interval. A hand-rolled
    // jlimit did only the clamp, which is a second expression of the range's
    // own law and lands off-grid for any parameter whose coarse step is not a
    // whole number of intervals.
    shared.getAttachment().setValueAsCompleteGesture (
        range.snapToLegalValue (current + static_cast<float> (delta)));
}

} // namespace forrobox
