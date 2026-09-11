/* ============================================================================
   FORRÓ BOX — KnobAttachment

   Binds one Knob to one parameter, in both directions, and owns EVERY value
   decision the knob deliberately does not:

     * the range and the interval        — the parameter's NormalisableRange
     * the reset target                  — the parameter's own default
     * the displayed text                — the parameter's getText/getValueForText
     * begin/end gesture bracketing      — juce::ParameterAttachment

   That division is STATE's decision, and it comes from two things /graphify
   surfaced in controls.js:

     * `this.def` is frozen at construction (controls.js:35) while
       `loadProfile` pushes values in with `fire=false` (app.js:527), so the
       prototype's reset returns a knob to the value it had at PAGE LOAD rather
       than the loaded profile's. A plugin resets to the parameter's default.

     * `set()` quantises with a `step` the knob owns (controls.js:124) while
       JUCE's NormalisableRange already carries the interval. Two copies of one
       law is the shape that produced 03-03's `dry = 1 - 0.5*wet` and 04-01's
       tracking bug, so the knob asks instead of holding.

   juce::ParameterAttachment does the listening and the host gestures — checked
   before hand-rolling one, which is 02-04's lesson.
============================================================================ */
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Knob.h"

namespace forrobox
{

class KnobAttachment final
{
public:
    KnobAttachment (juce::RangedAudioParameter& parameterToUse, Knob& knobToUse);
    ~KnobAttachment();

    /** `step * max(1, range/50)` per wheel notch — controls.js:161, expressed
        in intervals of the parameter's own range. */
    double coarseIntervals() const noexcept;

    /** One interval, for a snapped parameter or a continuous one. ONE
        definition, because two of them disagreed by a factor of the span. */
    double intervalSize() const noexcept;

private:
    void nudge (int direction, bool fine);

    juce::RangedAudioParameter& parameter;
    Knob&                       knob;

    /** Declared LAST: its constructor takes a callback that touches the two
        references above, and it sends its initial update immediately. */
    juce::ParameterAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnobAttachment)
};

} // namespace forrobox
