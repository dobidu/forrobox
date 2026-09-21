#include "GrooveExport.h"

#include "MidiExport.h"
#include "ParameterIDs.h"
#include "PluginProcessor.h"

namespace forrobox
{
namespace
{

/** Whether a channel is MUTED — not whether it is audible.

    THE DISTINCTION IS THE WHOLE POINT, and `ChannelGate`'s own header comment
    predicted this call site: `exportMIDI` reads `chans[id].mute` and never looks
    at solo. `ForroBoxAudioProcessor::resolveChannelSettings()` is right there on
    the processor this takes, and returns `VoiceEngine::Settings` whose channels
    carry `audible` — which folds SOLO in. Using it would make soloing one channel
    silently strip every other channel from the exported file, while the plugin
    kept playing them. Nothing about the resulting file looks wrong.

    So this reads the raw `mute` parameter per channel, and the suite asserts
    that a soloed channel changes nothing about the export. */
ChannelGate mutedChannels (ForroBoxAudioProcessor& processor)
{
    ChannelGate muted {};

    for (size_t channel = 0; channel < ids::channelInfos.size(); ++channel)
        if (auto* parameter = processor.getAPVTS().getRawParameterValue (
                ids::channelParam (ids::channelInfos[channel].id, ids::mute)))
            muted[channel] = parameter->load (std::memory_order_relaxed) > 0.5f;

    return muted;
}

int currentBpm (ForroBoxAudioProcessor& processor)
{
    if (auto* parameter = processor.getAPVTS().getRawParameterValue (ids::bpm))
        return juce::roundToInt (parameter->load (std::memory_order_relaxed));

    return ids::kMinBpm;
}

/** The profile id, made safe to put in a filename — or `"custom"`.

    THE PROTOTYPE'S FALLBACK IS NOT DEAD CODE, and 07-02's plan said it was. I
    wrote that `State::activeProfile` "only ever holds one of the four profile
    ids"; `State::readFrom` (src/ForroBoxState.cpp:106-111) says otherwise, and
    deliberately — an unrecognised profile string is PRESERVED VERBATIM so that
    "a project saved by a newer build must not lose its profile". A host project
    or preset blob can therefore carry anything here.

    That is a path, not just a name: `forrobox_../../x_132bpm.mid` is resolved by
    `File::getChildFile`, so an unsanitised id writes outside the temp folder and
    seeds the save dialog outside Documents. A `jassert` does not catch it —
    asserts compile out of the Release build the user actually runs.

    So an id that is not plain lowercase ASCII falls back to `"custom"`, which is
    the same word `app.js:454` reaches for when it has no profile.

    A CHARACTER RULE, not `Profiles.h`'s `findProfile`. The two answer different
    questions: `findProfile` asks "do I know this groove", and this asks "is this
    safe in a path". They come apart exactly where `State::readFrom`'s comment
    says they should — a profile from a NEWER build is unknown to `findProfile`
    but is a perfectly good filename, and mapping it to "custom" would throw away
    the name the newer build deliberately preserved. */
juce::String filenameSafeProfile (const juce::String& profileId)
{
    const auto safe = profileId.isNotEmpty()
                   && profileId.containsOnly ("abcdefghijklmnopqrstuvwxyz0123456789");

    return safe ? profileId : juce::String ("custom");
}

/** `forrobox_<profile>_<bpm>bpm.mid`, from `app.js:454`.

    The DIRTY flag does not appear. `markCustom()` sets `dirty` and never clears
    `activeProfile`, so an edited CAMPINA exports as
    `forrobox_campina_<bpm>bpm.mid` in the prototype too. Writing "custom" for a
    dirty pattern would be inventing behaviour the design source does not have —
    "custom" here means "no id we can safely use", not "edited". */
juce::String filenameFor (const juce::String& profileId, int bpm)
{
    return "forrobox_" + filenameSafeProfile (profileId) + "_" + juce::String (bpm) + "bpm.mid";
}

} // namespace

GrooveExport renderCurrentGroove (ForroBoxAudioProcessor& processor)
{
    const auto bpm   = currentBpm (processor);
    const auto steps = processor.currentStepWindow();
    const auto muted = mutedChannels (processor);

    auto state = processor.lockPatternState();

    return { renderStandardMidiFile (*state, bpm, steps, muted),
             filenameFor (state->activeProfile, bpm) };
}

} // namespace forrobox
