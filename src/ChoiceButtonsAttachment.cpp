#include "ChoiceButtonsAttachment.h"

namespace forrobox
{

ChoiceButtonsAttachment::ChoiceButtonsAttachment (juce::RangedAudioParameter& parameterToUse,
                                                  std::vector<Button*> buttons)
    : attachment (parameterToUse,
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
    // ONE loop, in the body, installing BOTH halves of each binding.
    //
    // A `guardsFor` helper built the guards in the init list and a second loop
    // installed the clicks, so the index law — "button i selects choice i" — was
    // split across two passes in two places. Filling `guards` here is safe for
    // the reason the header's ordering note actually rests on: DESTRUCTION order
    // follows DECLARATION order regardless of when a member is populated, and
    // `juce::ParameterAttachment`'s constructor only adds a listener — it does
    // not invoke the callback, which is marshalled to this same thread. Found
    // by /simplify.
    guards.reserve (buttons.size());

    for (size_t i = 0; i < buttons.size(); ++i)
    {
        // Not null: every caller constructs its buttons first. A nullable
        // overload was added to ScopedControlCallbacks for a slot that cannot
        // occur, and 02-04's rule is that a guarantee with no caller is not a
        // guarantee.
        jassert (buttons[i] != nullptr);

        guards.emplace_back (*buttons[i], [] (Button& b) { b.onClick = nullptr; });

        buttons[i]->onClick = [this, index = static_cast<int> (i)]
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
    }

    attachment.sendInitialUpdate();
}

} // namespace forrobox
