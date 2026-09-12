#include "ToggleAttachment.h"

namespace forrobox
{

ToggleAttachment::ToggleAttachment (juce::RangedAudioParameter& parameterToUse, Button& buttonToUse)
    : parameter (parameterToUse),
      button (&buttonToUse),
      attachment (parameterToUse,
                  [this] (float newDenormalisedValue)
                  {
                      // Parameter -> button. The ONLY writer of the lit state:
                      // the button keeps no bool of its own, so what it shows
                      // is what the host holds even when the host refuses the
                      // change the click asked for.
                      if (auto* b = button.getComponent())
                          b->setOn (isOnValue (newDenormalisedValue));
                  })
{
    buttonToUse.onClick = [this]
    {
        // The parameter's value, not the button's — reading `isOn()` would
        // toggle what is DISPLAYED, and the display is downstream of the
        // parameter. The two agree today and would stop agreeing the moment a
        // host filtered a change.
        //
        // Read and written in ONE unit. `getValue()` is NORMALISED 0..1 while
        // `setValueAsCompleteGesture` takes a DENORMALISED value, and comparing
        // both against a bare 0.5 is correct only because an
        // AudioParameterBool's range happens to be 0..1. Bound to a two-value
        // choice, or a bool re-expressed over another range, the read would
        // invert while the write saturated. Found by /code-review on 04-03.
        const auto current = parameter.convertFrom0to1 (parameter.getValue());

        // One complete gesture: begin, set, end. A toggle has no drag to
        // bracket, so a begin/end pair around a single value would tell the
        // host a gesture is in progress that already ended.
        attachment.setValueAsCompleteGesture (valueFor (! isOnValue (current)));
    };

    attachment.sendInitialUpdate();
}

bool ToggleAttachment::isOnValue (float denormalised) const noexcept
{
    const auto& range = parameter.getNormalisableRange();

    return denormalised > (range.start + range.end) * 0.5f;
}

float ToggleAttachment::valueFor (bool on) const noexcept
{
    const auto& range = parameter.getNormalisableRange();

    return on ? range.end : range.start;
}

ToggleAttachment::~ToggleAttachment()
{
    // The callback captures `this`, so leaving it installed on a button that
    // outlives the attachment turns the next click into a use-after-free —
    // and through the SafePointer, so a button that died FIRST is gone rather
    // than written to.
    if (auto* b = button.getComponent())
        b->onClick = nullptr;
}

} // namespace forrobox
