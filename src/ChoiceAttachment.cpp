#include "ChoiceAttachment.h"

namespace forrobox
{

ChoiceAttachment::ChoiceAttachment (juce::RangedAudioParameter& parameterToUse,
                                    Segmented& segmentedToUse)
    : segmented (&segmentedToUse),
      attachment (parameterToUse,
                  [this] (float newDenormalisedValue)
                  {
                      // A choice parameter's DENORMALISED value is its index —
                      // AudioParameterChoice's range is 0 .. numChoices-1 with
                      // an interval of 1. Rounded rather than truncated, because
                      // a host is free to hand over 0.9999999 for index 1.
                      if (auto* s = segmented.getComponent())
                          s->setSelectedIndex (juce::roundToInt (newDenormalisedValue));
                  })
{
    attachment.sendInitialUpdate();
}

// No destructor. Unlike ToggleAttachment there is no callback installed ON the
// control to tear down — this binding runs one way, so the only thing to undo is
// the parameter listener, and ~juce::ParameterAttachment already does that.

} // namespace forrobox
