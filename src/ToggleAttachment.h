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
    juce::RangedAudioParameter& parameter;
    Button&                     button;

    /** Declared LAST: its constructor takes a callback that touches the two
        references above, and it sends its initial update immediately. */
    juce::ParameterAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToggleAttachment)
};

} // namespace forrobox
