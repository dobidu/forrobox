/* ============================================================================
   FORRÓ BOX — what a test means by "a blank instrument"

   ONE LAW, ONE HOME. `ForroBoxAudioProcessor`'s constructor now loads
   `ids::defaultProfile` — CAMPINA's pattern AND its BATERIA mute — so that
   inserting the plugin and pressing play produces a groove, which is
   PROJECT.md's Success Metric. Every check written before that assumed a blank
   instrument, and a blank instrument is no longer what a fresh one is.

   So a test says which it wants, at the site, instead of inheriting a product
   default that has changed once and can change again.

   TWO VERBS, because the diff that introduced this header needed both and an
   enum could only express one. `blankInstrument` is what almost every check
   means. `clearGrid` is the narrower one: `testFactoryDefaults` measures the
   shipped ghost probabilities against the shipped groove, so it must clear the
   notes and leave every gate exactly as the product set it — releasing
   BATERIA's mute would put four kit lanes back into the thing being measured.

   NO `productDefault` VERB, and its absence is the point. A rig that wants the
   product's own starting state calls nothing at all; a two-valued parameter
   whose second value means "do nothing" is not a parameter, and it shipped here
   with zero callers, which is 02-04's "a guarantee with no caller is not a
   guarantee" stated and broken in one file. /simplify, all three angles.
============================================================================ */
#pragma once

#include "ParameterIDs.h"
#include "PluginProcessor.h"
#include "Settings.h"

namespace forrobox::test
{

/** Empties every lane, and touches nothing else.

    Under the handle, which publishes on release — the same path every
    production writer takes, so the audio thread cannot go on reading the
    profile's table. */
inline void clearGrid (ForroBoxAudioProcessor& processor)
{
    auto state = processor.lockPatternState();

    for (auto& lane : state->lanes)
        lane.fill (0);
}

/** No notes, nothing muted, nothing soloed — what a fresh instance used to be.

    The scalar defaults are deliberately NOT reset: bpm 132, swing 38, cachaça
    22 and TIMBRE HI-FI were chosen from CAMPINA and already equal it, so the
    constructor's load moves none of them and there is nothing here to undo.
    `activeProfile` and `dirty` are left alone as well — they are what the
    instrument CLAIMS, and a test that cares about the claim sets it. */
inline void blankInstrument (ForroBoxAudioProcessor& processor)
{
    // Hoisted out of the loop: it captures only `processor`, so re-forming the
    // closure per channel bought nothing.
    const auto write = [&processor] (const juce::String& id, float value)
    {
        if (auto* parameter = processor.getAPVTS().getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    };

    for (const auto& info : forrobox::ids::channelInfos)
    {
        write (forrobox::ids::channelParam (info.id, forrobox::ids::mute), 0.0f);
        write (forrobox::ids::channelParam (info.id, forrobox::ids::solo), 0.0f);
    }

    clearGrid (processor);
}

/** A temp preferences file that deletes itself.

    THE OTHER HALF OF THIS HEADER'S JOB. `blankInstrument` isolates a test from
    the product's default PATTERN; this isolates it from the user's stored
    SETTINGS, which 08-02 made a thing a test could accidentally read. Three
    hand-rolled copies existed before this — two in `UiTest.cpp` and one in
    `StateRoundTripTest.cpp` — and both `UiTest` copies did the thing the third
    one's comment forbids. /simplify.

    ORDER IS LOAD-BEARING. Members are destroyed in REVERSE declaration order,
    so `redirect` — declared last — is torn down first and the store is pointed
    away from this file BEFORE the file is deleted. Declaring the deleter after
    `redirect`, or calling `deleteFile` in a test body, removes the file while a
    handle is still open on it — and a `deleteFile` at the end of a function
    body also does not run when a check returns early. */
struct ScopedSettingsFile
{
    ScopedSettingsFile()
        : deleter { juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getChildFile ("forrobox-test-settings-"
                                       + juce::String (juce::Random::getSystemRandom().nextInt64())
                                       + ".settings") },
          path (deleter.path),
          redirect (path)
    {
    }

    struct Deleter
    {
        juce::File path;
        ~Deleter() { path.deleteFile(); }
    };

    Deleter                            deleter;
    juce::File                         path;
    forrobox::Settings::ScopedTestFile redirect;
};

/** The store pointed at a DIRECTORY, where its file should be, so every write
    fails. Same ordering rule as `ScopedSettingsFile`: the redirect goes first,
    then the directory, however the scope ends. 13-02, /simplify. */
struct ScopedUnwritableSettingsTarget
{
    struct Dir
    {
        juce::File path;

        Dir() : path (juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("forrobox-settings-dir-"
                                         + juce::String (juce::Random::getSystemRandom().nextInt64())))
        {
            path.createDirectory();
        }

        ~Dir() { path.deleteRecursively(); }
    };

    Dir                                dir;
    forrobox::Settings::ScopedTestFile redirect { dir.path };
};

/** A REAL `juce::PropertiesFile` on `path`, the reference the store's format is
    held to: XML, written through on every set. 13-01. */
inline std::unique_ptr<juce::PropertiesFile> referencePropertiesFile (const juce::File& path)
{
    juce::PropertiesFile::Options options;
    options.storageFormat            = juce::PropertiesFile::storeAsXML;
    options.millisecondsBeforeSaving = 0;
    return std::make_unique<juce::PropertiesFile> (path, options);
}

/** ANOTHER PROCESS writes one key: no `set` in this process sees it, which is
    what a host running a second plugin process does. 14-01, /simplify. */
inline void writeAsOtherProcess (const juce::File& path, juce::StringRef key, const juce::var& value)
{
    const auto otherProcess = referencePropertiesFile (path);
    otherProcess->setValue (key, value);
    otherProcess->saveIfNeeded();
}

/** The `Setting` a row of `settings::infos` describes — its index is the enum. */
inline forrobox::Setting settingOf (const forrobox::settings::SettingInfo& info) noexcept
{
    return static_cast<forrobox::Setting> (&info - forrobox::settings::infos.data());
}

} // namespace forrobox::test
