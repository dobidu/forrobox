#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace forrobox
{

/** ONE ASYNC CHOOSER, held in `slot` so a second click while it is up is
    ignored. Every file dialog in the plugin opens through here (20-01).

    launchAsync, NEVER the modal browseFor… calls: `JUCE_MODAL_LOOPS_PERMITTED=1`
    is set on the TEST target only and deliberately — CMakeLists.txt says so
    beside it: "modal loops in a plugin are exactly what the default forbids". A
    modal call compiles and deadlocks a host (07-02).

    Never REPLACED while one is open: the platform Pimpl holds a `FileChooser&`
    back-reference and `~FileChooser` only nulls its callback, so overwriting a
    live chooser leaves a native dialog pointing at freed memory — JUCE asserts
    on it (`juce_FileChooser.cpp:196`).

    The results are read BEFORE `slot` is reset: the callback's `fc` IS the
    chooser in `slot`, and a read after the reset came from freed memory — the
    empty result that made Export groove… save nothing (19-02). `onChosen` runs
    only for a non-empty choice, while `owner` is alive. */
void launchChooser (std::unique_ptr<juce::FileChooser>& slot, juce::Component& owner,
                    const juce::String& title, const juce::File& start, const juce::String& pattern,
                    int flags, std::function<void (const juce::Array<juce::File>&)> onChosen);

/** The plugin's one warning box: async, a warning icon, an OK button. Components
    hold a `showWarning` hook defaulting to this, so tests can capture what a
    user would be told. */
void showWarningBox (const juce::String& title, const juce::String& message);

/** An async OK / Cancel box; `onConfirm` runs only on OK. The fallback for a
    component that has no preset screen to ask on. */
void confirmBox (const juce::String& title, const juce::String& message, std::function<void()> onConfirm);

} // namespace forrobox
