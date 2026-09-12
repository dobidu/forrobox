#include "ToggleAttachment.h"

namespace forrobox
{

ToggleAttachment::ToggleAttachment (juce::RangedAudioParameter& parameterToUse, Button& buttonToUse)
    : parameter (parameterToUse),
      button (buttonToUse),
      attachment (parameterToUse,
                  [this] (float newDenormalisedValue)
                  {
                      // Parameter -> button. The ONLY writer of the lit state:
                      // the button keeps no bool of its own, so what it shows
                      // is what the host holds even when the host refuses the
                      // change the click asked for.
                      button.setOn (newDenormalisedValue > 0.5f);
                  })
{
    button.onClick = [this]
    {
        // The parameter's value, not the button's — reading `isOn()` would
        // toggle what is DISPLAYED, and the display is downstream of the
        // parameter. The two agree today and would stop agreeing the moment a
        // host filtered a change.
        const auto current = parameter.getValue() > 0.5f;

        // One complete gesture: begin, set, end. A toggle has no drag to
        // bracket, so a begin/end pair around a single value would tell the
        // host a gesture is in progress that already ended.
        attachment.setValueAsCompleteGesture (current ? 0.0f : 1.0f);
    };

    attachment.sendInitialUpdate();
}

ToggleAttachment::~ToggleAttachment()
{
    // The callback captures `this`, so leaving it installed on a button that
    // outlives the attachment turns the next click into a use-after-free.
    button.onClick = nullptr;
}

} // namespace forrobox
