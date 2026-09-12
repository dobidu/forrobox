/* ============================================================================
   FORRÓ BOX — ToggleAttachment

   Binds one Button's lit state to one boolean parameter, in both directions.

   Not ProportionAttachment with a different name: a toggle has no drag, so its
   change is one COMPLETE gesture rather than a begin / part / end bracket, and
   it has no proportion to show. Sharing the two would have meant a `setProportion`
   the button cannot answer and an `onDragTo` it can never fire — which is the
   argument the plan makes for not forcing the fader through the knob's seam,
   one control further along.

   juce::AudioProcessorValueTreeState::ButtonAttachment already does this job
   for a juce::Button; ours is a custom Component, for the reason every control
   in this plugin is. Checked before hand-rolling, which is 02-04's lesson.
============================================================================ */
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Button.h"

namespace forrobox
{

class ToggleAttachment final
{
public:
    ToggleAttachment (juce::RangedAudioParameter&, Button&);
    ~ToggleAttachment();

private:
    /** Is this denormalised value the ON state?

        ONE definition, used by both directions. Written out twice it was the
        `dry = 1 - 0.5 * wet` shape this project keeps finding: fix the
        threshold for the click and not for the display and you get a button
        whose click inverts correctly and whose lit state does not, with both
        looking plausible on their own. */
    bool isOnValue (float denormalised) const noexcept;

    /** The denormalised value for an ON or OFF state — `isOnValue`'s inverse. */
    float valueFor (bool on) const noexcept;

    juce::RangedAudioParameter&           parameter;

    /** A weak reference, for the reason ProportionAttachment records: no
        declaration order is safe on both the destruction and the assignment
        path, so the teardown must not assume the control is still there. */
    juce::Component::SafePointer<Button>  button;

    /** Declared LAST: its constructor takes a callback that touches the two
        references above, and it sends its initial update immediately. */
    juce::ParameterAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToggleAttachment)
};

} // namespace forrobox
