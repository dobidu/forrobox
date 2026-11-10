#include "AsyncFileChooser.h"

namespace forrobox
{

void launchChooser (std::unique_ptr<juce::FileChooser>& slot, juce::Component& owner,
                    const juce::String& title, const juce::File& start, const juce::String& pattern,
                    int flags, std::function<void (const juce::Array<juce::File>&)> onChosen)
{
    if (slot != nullptr)
        return;

    slot = std::make_unique<juce::FileChooser> (title, start, pattern);
    slot->launchAsync (flags,
        [safe = juce::Component::SafePointer<juce::Component> (&owner), &slot,
         then = std::move (onChosen)] (const juce::FileChooser& fc)
        {
            if (safe == nullptr)
                return;

            const auto results = fc.getResults();
            slot.reset();

            if (! results.isEmpty())
                then (results);
        });
}

void showWarningBox (const juce::String& title, const juce::String& message)
{
    juce::NativeMessageBox::showAsync (juce::MessageBoxOptions()
                                           .withIconType (juce::MessageBoxIconType::WarningIcon)
                                           .withTitle (title)
                                           .withMessage (message)
                                           .withButton ("OK"),
                                       nullptr);
}

void confirmBox (const juce::String& title, const juce::String& message, std::function<void()> onConfirm)
{
    juce::NativeMessageBox::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, title, message, nullptr,
                                             juce::ModalCallbackFunction::create (
                                                 [then = std::move (onConfirm)] (int result)
                                                 {
                                                     if (result != 0 && then != nullptr)
                                                         then();
                                                 }));
}

} // namespace forrobox
