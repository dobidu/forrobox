/* ============================================================================
   FORRÓ BOX — ChoiceAttachment

   Binds one Segmented's lit index to one choice parameter.

   ONE DIRECTION, parameter -> control, and that is not an oversight. `OUTPUT`
   is the only choice-driven control in the plugin today and it is deliberately
   read-only until 04-06 implements the multi-out routing behind it, so a write
   path here would be a guarantee with no caller — 02-04's ruling. 04-06 adds
   the other direction when it makes the control live, and the split is honest
   in the meantime: the segment always shows what the host holds.

   It exists at all because the display half is NOT optional for a read-only
   control. `output_mode` is real, automatable and persisted, so a host
   automating it, a project reopening, or a host-side undo all move the
   parameter — and a control that read it once at build time would then light
   the wrong segment with no way for anyone to notice. Found by /code-review on
   04-05, against a comment of mine claiming the opposite.
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
