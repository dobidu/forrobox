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

void saveWithForcedExtension (const juce::File& chosen, const juce::String& extension,
                              const ConfirmReplace& confirm, std::function<void (const juce::File&)> write)
{
    const auto forced = ! chosen.hasFileExtension (extension);
    const auto target = forced ? chosen.withFileExtension (extension) : chosen;

    if (! (forced && target.exists()))
    {
        write (target);
        return;
    }

    jassert (confirm != nullptr);   // a replace nobody can be asked about is not written

    if (confirm != nullptr)
        confirm (target, [target, then = std::move (write)] { then (target); });
}

} // namespace forrobox
