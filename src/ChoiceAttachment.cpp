#include "ChoiceAttachment.h"

namespace forrobox
{

ChoiceAttachment::ChoiceAttachment (juce::RangedAudioParameter& parameterToUse,
                                    Segmented& segmentedToUse)
    : parameter (parameterToUse),
      segmented (&segmentedToUse),
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
    segmentedToUse.onSegmentClicked = [this] (int index)
    {
        // DENORMALISED, and that is the whole care here.
        //
        // `setValueAsCompleteGesture` takes a denormalised value while
        // `getValue()` returns a normalised one, and for a two-choice parameter
        // the range is 0..1 — so writing the normalised index would APPEAR to
        // work and would break silently the day a third output mode is added.
        // ToggleAttachment.cpp:33 records /code-review finding exactly that trap
        // at 04-03.
        //
        // Clamped to the parameter's own range rather than trusted: the index
        // comes from a control that was constructed from a label list, and the
        // two are one table today but nothing in the type system says so.
        const auto& range = parameter.getNormalisableRange();

        attachment.setValueAsCompleteGesture (
            juce::jlimit (range.start, range.end, static_cast<float> (index)));
    };

    attachment.sendInitialUpdate();
}

ChoiceAttachment::~ChoiceAttachment()
{
    // The callback captures `this`, so leaving it installed on a control that
    // outlives the attachment turns the next click into a use-after-free — and
    // through the SafePointer, so a control that died FIRST is gone rather than
    // written to. ToggleAttachment's destructor, for its reason.
    if (auto* s = segmented.getComponent())
        s->onSegmentClicked = nullptr;
}

} // namespace forrobox
