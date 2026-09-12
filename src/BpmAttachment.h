/* ============================================================================
   FORRÓ BOX — BpmAttachment

   Binds the BPM field to the `bpm` parameter, and owns every value decision the
   field deliberately does not: the range, the step, what a pixel is worth, and
   what typed text means.

   NOT ProportionAttachment. That template's shape is `onDragTo (proportion)` —
   an absolute position — and the BPM field's drag is a DELTA in BPM units from
   an anchor. Forcing it through would mean the field computing BPM, which means
   the field knowing the range, which is the thing this project keeps taking
   away from its controls. The plan says the same about not forcing the fader
   through the knob's seam, one control along.

   What IS shared with ToggleAttachment: the control is held through a
   juce::Component::SafePointer, so no declaration or assignment order is
   load-bearing. /simplify established that at 04-03 after AddressSanitizer
   named the alternative.
============================================================================ */
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "BpmField.h"

namespace forrobox
{

class BpmAttachment final
{
public:
    BpmAttachment (juce::RangedAudioParameter&, BpmField&);
    ~BpmAttachment();

    /** Under SYNC the field shows the HOST's tempo and refuses every gesture.
        Called by whatever polls the processor — the field itself knows nothing
        about a host. A hostBpm of 0 means the host reported none, and the
        parameter's own value is shown instead. */
    void setSyncedToHost (bool synced, float hostBpm);

private:
    void applyDrag (int pixelsUp);
    void applyNudge (int direction);

    juce::RangedAudioParameter&            parameter;
    juce::Component::SafePointer<BpmField> field;

    /** The value at mouse-down. The drag law is a delta FROM this, so it is
        captured once per gesture rather than re-read per move — re-reading
        would accumulate rounding and make a drag out and back drift. */
    float anchorValue { 0.0f };

    bool  synced { false };
    float hostTempo { 0.0f };

    void refreshText();

    /** Declared LAST: its constructor installs callbacks touching the above. */
    juce::ParameterAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BpmAttachment)
};

} // namespace forrobox
