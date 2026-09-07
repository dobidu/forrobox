#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <array>
#include <cstdlib>
#include <type_traits>

ForroBoxAudioProcessor::ForroBoxAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
        // Instrument: declaring an input bus makes some hosts present this as
        // an effect, so there is deliberately none.
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

void ForroBoxAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // The only place allocation is permitted. Later phases size their voice
    // pools, pattern buffers and FIFOs here.
    currentSampleRate.store (sampleRate,      std::memory_order_relaxed);
    currentBlockSize .store (samplesPerBlock, std::memory_order_relaxed);
}

void ForroBoxAudioProcessor::releaseResources()
{
    // Deliberately does NOT reset the cached rate/block size. A host may close
    // its audio device with the editor still open and querying; zeroing here
    // would hand out a 0.0 sample rate indefinitely.
}

bool ForroBoxAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Stereo main output, no input. Multi-out is a post-v0.1 concern.
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet()  == juce::AudioChannelSet::disabled();
}

void ForroBoxAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                           juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // AUDIO-THREAD CONTRACT — no allocation, no locks, no I/O, no logging,
    // no juce::String construction. Every later phase inherits this rule.
    buffer.clear();
    midi.clear();
}

// ── parameter layout ────────────────────────────────────────────────────────
namespace
{
    using namespace juce;

    /** 0-100 with unit steps. Integer steps keep text<->value strictly
        invertible, which host-typed automation values and the Phase 4
        type-to-set knob both depend on. */
    NormalisableRange<float> percentRange() { return { 0.0f, 100.0f, 1.0f }; }

    /** "12%" <-> 12. */
    AudioParameterFloatAttributes percentAttributes()
    {
        return AudioParameterFloatAttributes()
            // No withValueFromStringFunction: AudioParameterFloat's default is
            // String::getFloatValue(), which is strtod-based and already stops at
            // the '%'. A custom parser here would only restate it.
            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + "%"; });
    }

    /** "+5" / "0" / "-5" — the explicit plus is how the prototype shows pitch. */
    AudioParameterIntAttributes pitchAttributes()
    {
        return AudioParameterIntAttributes()
            // Only the display half is custom; AudioParameterInt's default parser
            // is already String::getIntValue().
            .withStringFromValueFunction ([] (int v, int) { return v > 0 ? "+" + String (v) : String (v); });
    }

    /** "C" at centre, else "L##" / "R##". */
    AudioParameterIntAttributes panAttributes()
    {
        return AudioParameterIntAttributes()
            .withStringFromValueFunction ([] (int v, int)
            {
                if (v == 0) return String ("C");
                return (v < 0 ? String ("L") : String ("R")) + String (std::abs (v));
            })
            .withValueFromStringFunction ([] (const String& t) -> int
            {
                const auto s = t.trim();
                if (s.startsWithIgnoreCase ("C")) return 0;

                const auto magnitude = s.retainCharacters ("0123456789").getIntValue();
                if (s.startsWithIgnoreCase ("L")) return -magnitude;
                if (s.startsWithIgnoreCase ("R")) return  magnitude;

                // Plain numeric entry keeps whatever sign it was typed with.
                // retainCharacters must keep the '-' here, or a host's typed
                // "-20" would come back as R20 — the opposite channel.
                return s.retainCharacters ("-0123456789").getIntValue();
            });
    }

    std::unique_ptr<AudioProcessorParameterGroup> makeGlobalGroup()
    {
        namespace ids = forrobox::ids;

        auto group = std::make_unique<AudioProcessorParameterGroup> (ids::groupGlobal, "GLOBAL", "|");

        group->addChild (
            std::make_unique<AudioParameterInt>    (ParameterID { ids::bpm, 1 },        "BPM", 40, 300, 132),
            std::make_unique<AudioParameterBool>   (ParameterID { ids::sync, 1 },       "SYNC", false),
            std::make_unique<AudioParameterFloat>  (ParameterID { ids::swing, 1 },      "SWING",   percentRange(), 38.0f, percentAttributes()),
            std::make_unique<AudioParameterFloat>  (ParameterID { ids::cachaca, 1 },    String::fromUTF8 ("CACHA\xc3\x87" "A")   /* CACHAÇA — split so \x87 does not swallow the A */, percentRange(), 22.0f, percentAttributes()),
            std::make_unique<AudioParameterChoice> (ParameterID { ids::steps, 1 },      "STEPS",  StringArray { "16", "32" }, 0),
            std::make_unique<AudioParameterChoice> (ParameterID { ids::timbre, 1 },     "TIMBRE", StringArray { "HI-FI", "LO-FI", "CICLOTRON" }, 0),
            std::make_unique<AudioParameterFloat>  (ParameterID { ids::charMix, 1 },    "MIX",    percentRange(), 40.0f, percentAttributes()),
            std::make_unique<AudioParameterBool>   (ParameterID { ids::limiterOn, 1 },  "LIMITER", true),
            std::make_unique<AudioParameterFloat>  (ParameterID { ids::master, 1 },     "MASTER", percentRange(), 82.0f, percentAttributes()),
            std::make_unique<AudioParameterChoice> (ParameterID { ids::outputMode, 1 }, "OUTPUT", StringArray { "STEREO", "MULTI-OUT" }, 0));

        return group;
    }

    std::unique_ptr<AudioProcessorParameterGroup> makeChannelGroup (size_t index)
    {
        namespace ids = forrobox::ids;

        const auto& info    = ids::channelInfos[index];
        const auto* channel = info.id;

        auto group = std::make_unique<AudioProcessorParameterGroup> (
                         channel, String::fromUTF8 (info.displayName), "|");

        group->addChild (
            std::make_unique<AudioParameterFloat> (ParameterID { ids::channelParam (channel, ids::vol),   1 }, "VOL",   percentRange(), info.vol,   percentAttributes()),
            std::make_unique<AudioParameterInt>   (ParameterID { ids::channelParam (channel, ids::pitch), 1 }, "PITCH", -12, 12, 0,              pitchAttributes()),
            std::make_unique<AudioParameterFloat> (ParameterID { ids::channelParam (channel, ids::decay), 1 }, "DECAY", percentRange(), info.decay, percentAttributes()),
            std::make_unique<AudioParameterInt>   (ParameterID { ids::channelParam (channel, ids::pan),   1 }, "PAN",   -50, 50, info.pan,          panAttributes()),
            std::make_unique<AudioParameterFloat> (ParameterID { ids::channelParam (channel, ids::ghost), 1 }, "GHOST", percentRange(), info.ghost, percentAttributes()),
            std::make_unique<AudioParameterBool>  (ParameterID { ids::channelParam (channel, ids::mute),  1 }, "MUTE",  false),
            std::make_unique<AudioParameterBool>  (ParameterID { ids::channelParam (channel, ids::solo),  1 }, "SOLO",  false));

        return group;
    }
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout ForroBoxAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (makeGlobalGroup());

    for (size_t i = 0; i < forrobox::ids::channelInfos.size(); ++i)
        layout.add (makeChannelGroup (i));

    return layout;
}

juce::AudioProcessorEditor* ForroBoxAudioProcessor::createEditor()
{
    return new ForroBoxAudioProcessorEditor (*this);
}

void ForroBoxAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Serialise from a copy so the live parameter tree is never disturbed.
    // Takes stateLock directly rather than via lockPatternState() because the
    // lock must also cover apvts.copyState(); the effect is the same lock.
    const juce::ScopedLock lock (stateLock);

    auto tree = apvts.copyState();
    patternState.writeTo (tree);

    if (const auto xml = tree.createXml())
        copyXmlToBinary (*xml, destData);
}

void ForroBoxAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    // Absent, unparseable, or belonging to some other plugin — leave every
    // parameter at whatever it currently holds rather than half-applying.
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    const auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.isValid())
        return;

    const juce::ScopedLock lock (stateLock);

    patternState = forrobox::State::readFrom (tree);

    // Strip the grid child from a copy BEFORE handing the tree over, so the live
    // APVTS tree never holds a node it does not own. Stripping afterwards would
    // mean assigning an invalid tree and then repairing it.
    auto strippedTree = tree.createCopy();
    strippedTree.removeChild (strippedTree.getChildWithName (forrobox::ids::stateNode), nullptr);
    apvts.replaceState (strippedTree);
}

// Entry point the plugin wrappers call.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ForroBoxAudioProcessor();
}
