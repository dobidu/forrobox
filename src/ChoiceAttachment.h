/* ============================================================================
   FORRÓ BOX — ChoiceAttachment

   Binds one Segmented's lit index to one choice parameter.

   BOTH DIRECTIONS, as of 04-06. It shipped one-directional at 04-05 because
   `OUTPUT` was read-only until the multi-out routing existed behind it, and a
   write path with no caller is a guarantee with no caller — 02-04's ruling.
   04-06 built the routing, so the other half arrives with the thing that needed
   it.

   The display half is NOT optional even for a read-only control, which is why
   it came first: `output_mode` is real, automatable and persisted, so a host
   automating it, a project reopening or a host-side undo all move the parameter
   — and a control that read it once at build time would light the wrong segment
   with no way for anyone to notice. Found by /code-review on 04-05, against a
   comment of mine claiming the opposite.

   A click is ONE COMPLETE GESTURE, which is `ToggleAttachment`'s law and for its
   reason: a segmented control has no drag to bracket, so a begin/end pair around
   a single value would tell the host a gesture is in progress that already
   ended.
============================================================================ */
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Segmented.h"

namespace forrobox
{

class ChoiceAttachment final
{
public:
    ChoiceAttachment (juce::RangedAudioParameter&, Segmented&);
    ~ChoiceAttachment();

private:

    /** A weak reference, for the reason ToggleAttachment and
        ProportionAttachment record: no declaration order is safe on both the
        destruction and the assignment path, so the callback must not assume the
        control is still there. */
    juce::Component::SafePointer<Segmented> segmented;

    /** Declared LAST: its constructor takes a callback that touches the
        reference above, and it sends its initial update immediately. */
    juce::ParameterAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChoiceAttachment)
};

} // namespace forrobox
