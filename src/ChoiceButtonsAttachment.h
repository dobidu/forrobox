/* ============================================================================
   FORRÓ BOX — ChoiceButtonsAttachment

   Binds N buttons to one choice parameter: button `i` is lit when the choice
   index is `i`, and clicking button `i` selects it.

   NOT ChoiceAttachment, which binds a `Segmented`. `STEPS` is two separate
   `.btn`s in the prototype (`app.js:321-322`) inside a plain flex row, not the
   `.qs-group` with dividers that `Segmented` draws — so the two are different
   controls answering the same kind of parameter, which is exactly the split the
   other five attachments already carry.

   NOT ToggleAttachment per button either. A toggle's law is "this value or the
   other"; a choice's is "this index among N", and expressing the second as two
   of the first gives two controls that can both be lit, or neither, with nothing
   saying which is right. The lit state must be a FUNCTION of the parameter, and
   there is one parameter.

   The buttons are held through ScopedControlCallbacks for the reason all five
   others are: no declaration or assignment order is load-bearing.
============================================================================ */
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ScopedControlCallbacks.h"

#include "Button.h"

#include <vector>

namespace forrobox
{

class ChoiceButtonsAttachment final
{
public:
    /** `buttons[i]` selects choice `i`. The order IS the mapping, so the caller
        passes them in the parameter's own choice order — `ids::stepWindows` for
        STEPS — rather than in visual order, and a reordered layout cannot
        silently re-map the parameter. */
    ChoiceButtonsAttachment (juce::RangedAudioParameter&, std::vector<Button*> buttons);

private:
    /** One guard per button. A vector of guards rather than one guard over the
        group, because ScopedControlCallbacks holds a SafePointer to ONE control
        — and a button that died first must be skipped individually rather than
        taking the whole teardown with it. */
    std::vector<ScopedControlCallbacks<Button>> guards;

    /** Declared LAST: its constructor installs a callback that touches the
        guards above, and it sends its initial update immediately. */
    juce::ParameterAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChoiceButtonsAttachment)
};

} // namespace forrobox
