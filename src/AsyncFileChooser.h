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

/** Asks whether to replace `existing`, and runs `replace` only on yes. */
using ConfirmReplace = std::function<void (const juce::File& existing, std::function<void()> replace)>;

/** What a save dialog does with its file: `extension` is FORCED onto `chosen`,
    since a save dialog does not add one, and `write` gets the final file.

    THE DIALOG WARNED ABOUT THE NAME TYPED, not the one the extension made: "Meu
    Xote" over an earlier "Meu Xote.forrogroove" would be replaced unasked. So a
    forced name that lands on an existing file goes through `confirm` first
    (19-02, then 20-01 for MIDI). A null `confirm` is never a yes: nothing is
    written. */
void saveWithForcedExtension (const juce::File& chosen, const juce::String& extension,
                              const ConfirmReplace& confirm, std::function<void (const juce::File&)> write);

} // namespace forrobox
