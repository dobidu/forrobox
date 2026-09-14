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

   The parameter -> knob direction, the drag and the gesture bracket are
   ProportionAttachment's: the fader needs exactly those three and nothing more,
   so they were extracted when it arrived rather than copied into it. What is
   left here is everything the FADER has no way to fire, because
   `controls.js:231-247` gives it no wheel, no reset and no typed entry.
============================================================================ */
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ScopedControlCallbacks.h"

#include "Knob.h"
#include "ProportionAttachment.h"

namespace forrobox
{

class KnobAttachment final
{
public:
    KnobAttachment (juce::RangedAudioParameter& parameterToUse, Knob& knobToUse);

    /** `step * max(1, range/50)` per wheel notch — controls.js:161, expressed
        in intervals of the parameter's own range. */
    double coarseIntervals() const noexcept;

    /** One interval, for a snapped parameter or a continuous one. ONE
        definition, because two of them disagreed by a factor of the span. */
    double intervalSize() const noexcept;

private:
    void nudge (int direction, bool fine);

    juce::RangedAudioParameter& parameter;

    /** A weak reference, for the reason ProportionAttachment and
        ToggleAttachment record: no declaration order is safe on both the
        destruction and the assignment path, and 04-04's header struct is
        cleared by assignment. `stripKnobs` was safe only because PlacedKnob is
        DESTROYED, in reverse order, rather than assigned — so this class was
        the last one still trusting an ordering. Found by /code-review. */
    ScopedControlCallbacks<Knob> knob;

    /** Declared LAST: its constructor installs callbacks that touch the two
        references above. */
    ProportionAttachment<Knob> shared;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnobAttachment)
};

} // namespace forrobox
