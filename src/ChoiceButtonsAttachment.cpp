#include "ChoiceButtonsAttachment.h"

namespace forrobox
{

namespace
{
    std::vector<ScopedControlCallbacks<Button>> guardsFor (const std::vector<Button*>& buttons)
    {
        std::vector<ScopedControlCallbacks<Button>> out;
        out.reserve (buttons.size());

        for (auto* button : buttons)
            if (button != nullptr)
                out.emplace_back (*button, [] (Button& b) { b.onClick = nullptr; });

        return out;
    }
}

ChoiceButtonsAttachment::ChoiceButtonsAttachment (juce::RangedAudioParameter& parameterToUse,
                                                  std::vector<Button*> buttons)
    : guards (guardsFor (buttons)),
      attachment (parameterToUse,
                  [this] (float newDenormalisedValue)
                  {
                      // Parameter -> buttons. The ONLY writer of the lit state:
                      // exactly one button is on, and which one is a function of
                      // the parameter rather than of the last click. 04-04 found
                      // that failure twice in one plan and it is recorded as a
                      // project decision — a read-only control still needs the
                      // display half.
                      //
                      // A choice parameter's DENORMALISED value is its index.
                      // Rounded rather than truncated, because a host is free to
                      // hand over 0.9999999 for index 1 — ChoiceAttachment.cpp
                      // records the same care.
                      const auto selected = juce::roundToInt (newDenormalisedValue);

                      for (size_t i = 0; i < guards.size(); ++i)
                          if (auto* b = guards[i].get())
                              b->setOn (static_cast<int> (i) == selected);
                  })
{
    for (size_t i = 0; i < guards.size(); ++i)
        if (auto* b = guards[i].get())
            b->onClick = [this, index = static_cast<int> (i)]
            {
                // DENORMALISED, for the reason ToggleAttachment.cpp:33 records:
                // setValueAsCompleteGesture takes a denormalised value while
                // getValue() returns a normalised one, and for a two-choice
                // parameter the two agree — so writing the normalised index
                // would appear to work and break silently the day a third
                // choice is added.
                //
                // One COMPLETE gesture: a choice has no drag to bracket.
                attachment.setValueAsCompleteGesture (static_cast<float> (index));
            };

    attachment.sendInitialUpdate();
}

} // namespace forrobox
