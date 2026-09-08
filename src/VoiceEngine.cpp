#include "VoiceEngine.h"

#include <cmath>

namespace forrobox
{
namespace
{
    /** Web Audio's StereoPannerNode law for a MONO source: equal power, so a
        centred voice is not 3 dB louder than a hard-panned one.

        The seven synthesised voices are mono and take this path. */
    void monoPanGains (float pan01, float& gainLeft, float& gainRight) noexcept
    {
        const auto x = juce::jlimit (0.0f, 1.0f, (pan01 + 1.0f) * 0.5f)
                         * juce::MathConstants<float>::halfPi;
        gainLeft  = std::cos (x);
        gainRight = std::sin (x);
    }

    /** Web Audio's StereoPannerNode law for a STEREO source: panning
        ATTENUATES the opposite side and folds it inwards, rather than placing a
        point source.

        The sampled zabumba takes this path because its files are true stereo,
        not dual-mono. Feeding a stereo image through the mono law would collapse
        it to the middle and then re-place it, throwing away the recording's own
        width. */
    void stereoPanGains (float pan01,
                         float& leftToLeft, float& rightToLeft,
                         float& leftToRight, float& rightToRight) noexcept
    {
        const auto pan = juce::jlimit (-1.0f, 1.0f, pan01);

        if (pan <= 0.0f)
        {
            const auto x = (pan + 1.0f) * juce::MathConstants<float>::halfPi;
            leftToLeft   = 1.0f;
            rightToLeft  = std::cos (x);
            leftToRight  = 0.0f;
            rightToRight = std::sin (x);
        }
        else
        {
            const auto x = pan * juce::MathConstants<float>::halfPi;
            leftToLeft   = std::cos (x);
            rightToLeft  = 0.0f;
            leftToRight  = std::sin (x);
            rightToRight = 1.0f;
        }
    }

    /** PAN as -1..+1. The parameter's extent is +/-50, not +/-100 — normalising
        by the wrong one halves every pan silently, which is what this did
        before the pan tests caught it. */
    float normalisedPan (float panParameter) noexcept
    {
        const auto extent = static_cast<float> (ids::kPanExtent);

        return juce::jlimit (-1.0f, 1.0f, juce::jlimit (-extent, extent, panParameter) / extent);
    }

    /** Fade applied over the last `releaseSamples` of a truncated sample, so
        cutting the tail short cannot click. */
    float truncationGain (double pos, double envSamples, double releaseSamples) noexcept
    {
        if (releaseSamples <= 0.0 || pos < envSamples - releaseSamples)
            return 1.0f;

        const auto remaining = envSamples - pos;

        return static_cast<float> (juce::jlimit (0.0, 1.0, remaining / releaseSamples));
    }
}

void VoiceEngine::prepare (double newSampleRate, int newMaxBlockSize)
{
    sampleRate   = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    maxBlockSize = juce::jmax (0, newMaxBlockSize);

    for (auto& voice : synthVoices)
        voice.prepare (sampleRate);

    sampler.prepare (sampleRate);

    prepared = true;

    reset();
}

void VoiceEngine::reset() noexcept
{
    for (auto& voice : synthVoices)
    {
        voice.clear();
        voice.setSamplesUntilStart (0);
    }

    for (auto& voice : sampleVoices)
        voice = {};

    // Reseeding is what makes "render the same bars twice and compare" a
    // meaningful assertion rather than a coincidence.
    rng.setSeed (kRngSeed);

    nextStartOrder = 1;
    voicesStolen = 0;
    voicesDropped = 0;
}

SynthVoice* VoiceEngine::claimSynthVoice (int lane) noexcept
{
    SynthVoice* oldestOnLane = nullptr;
    SynthVoice* oldestAnywhere = nullptr;

    for (auto& voice : synthVoices)
    {
        if (! voice.isActive())
            return &voice;

        if (voice.getLane() == lane
            && (oldestOnLane == nullptr || voice.getStartOrder() < oldestOnLane->getStartOrder()))
            oldestOnLane = &voice;

        if (oldestAnywhere == nullptr || voice.getStartOrder() < oldestAnywhere->getStartOrder())
            oldestAnywhere = &voice;
    }

    // Steal from the SAME lane first. Stealing across lanes would let a busy
    // ganzá — sixteen hits a bar — cut the zabumba, which is the one voice the
    // groove is built around.
    ++voicesStolen;

    return oldestOnLane != nullptr ? oldestOnLane : oldestAnywhere;
}

VoiceEngine::SampleVoice* VoiceEngine::claimSampleVoice() noexcept
{
    SampleVoice* oldest = nullptr;

    for (auto& voice : sampleVoices)
    {
        if (! voice.active)
            return &voice;

        if (oldest == nullptr || voice.startOrder < oldest->startOrder)
            oldest = &voice;
    }

    ++voicesStolen;

    return oldest;
}

void VoiceEngine::schedule (int lane, std::uint8_t velocity, int sampleOffset,
                            const Settings& settings) noexcept
{
    if (velocity == 0 || ! prepared)
        return;

    if (! juce::isPositiveAndBelow (lane, static_cast<int> (voiceSpecs.size())))
        return;

    const auto channel = channelForLane (lane);

    if (! juce::isPositiveAndBelow (channel, kNumChannels))
        return;

    const auto& channelSettings = settings[static_cast<size_t> (channel)];

    // Gated at SCHEDULE, not render: a muted channel must not consume voices
    // that an audible one needs. A note already sounding when its channel is
    // muted is left to finish, which is what the prototype's graph does — the
    // mute is a gain, applied downstream of notes already in flight.
    if (! channelSettings.audible)
        return;

    const auto v = static_cast<float> (velocity) / static_cast<float> (State::kMaxVelocity);

    if (voiceSpecs[static_cast<size_t> (lane)].usesSample)
        scheduleSample (v, sampleOffset, channelSettings);
    else
        scheduleSynth (lane, v, sampleOffset, channelSettings);
}

void VoiceEngine::scheduleSynth (int lane, float velocity, int sampleOffset,
                                 const ChannelSettings& channelSettings) noexcept
{
    auto* voice = claimSynthVoice (lane);

    if (voice == nullptr)
    {
        ++voicesDropped;
        return;
    }

    voice->trigger (lane, velocity, channelSettings.pitch, channelSettings.decay, rng);
    voice->setSamplesUntilStart (juce::jmax (0, sampleOffset));
    voice->setStartOrder (nextStartOrder++);
}

void VoiceEngine::scheduleSample (float velocity, int sampleOffset,
                                  const ChannelSettings& channelSettings) noexcept
{
    if (! sampler.isReady())
        return;

    const auto blend = sampler.blendForVelocity (velocity);
    const auto pitchFactor = std::pow (2.0, static_cast<double> (channelSettings.pitch) / 12.0);
    const auto readRate = sampler.getBaseReadRate() * pitchFactor;

    // PLANNING.md line ~730: for a sampled channel DECAY becomes an amplitude
    // envelope that may truncate the tail and must never extend it past the
    // file. decayScale spans 0.4-1.8, so it is normalised by its own maximum:
    // DECAY at 100 plays the file whole, and lower values shorten it
    // proportionally rather than scaling it past its own end.
    const auto decayScale = 0.4 + juce::jlimit (0.0f, ids::kPercentMax, channelSettings.decay)
                                    / static_cast<double> (ids::kPercentMax) * 1.4;
    const auto decayFraction = juce::jlimit (0.0, 1.0, decayScale / 1.8);

    const std::array<std::pair<int, float>, 2> parts {{
        { blend.slotA, blend.gainA },
        { blend.slotB, blend.gainB },
    }};

    for (const auto& [slot, mix] : parts)
    {
        if (slot < 0 || mix <= 0.0f)
            continue;

        const auto lengthSamples = static_cast<double> (sampler.getLengthSamples (slot));

        if (lengthSamples <= 0.0 || readRate <= 0.0)
            continue;

        auto* voice = claimSampleVoice();

        if (voice == nullptr)
        {
            ++voicesDropped;
            continue;
        }

        // Output samples the file spans at this read rate.
        const auto playableSamples = lengthSamples / readRate;

        voice->active = true;
        voice->slot = slot;
        voice->position = 0.0;
        voice->readRate = readRate;
        voice->lengthSamples = lengthSamples;
        voice->envSamples = playableSamples * decayFraction;
        voice->releaseSamples = juce::jmin (voice->envSamples * 0.5, 0.005 * sampleRate);
        voice->pos = 0.0;
        // RMS-normalised, then velocity. Normalising by PEAK looked obvious and
        // is wrong on this material: the soft layer's peak/RMS is 3.8 against
        // the slap's 21.3, so matching peaks would make the softest hit the
        // loudest thing in the set.
        voice->gain = mix * velocity * sampler.getNormalisationGain (slot);
        voice->samplesUntilStart = juce::jmax (0, sampleOffset);
        voice->startOrder = nextStartOrder++;
    }
}

void VoiceEngine::render (juce::AudioBuffer<float>& buffer, const Settings& settings) noexcept
{
    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0 || ! prepared)
        return;

    auto* left  = buffer.getWritePointer (0);
    auto* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    // ── synthesised voices: mono into the equal-power pan law ───────────────
    for (auto& voice : synthVoices)
    {
        const auto pending = voice.getSamplesUntilStart();

        if (! voice.isActive())
        {
            // Still carry an unstarted voice's countdown forward, so a trigger
            // scheduled past this block is not lost.
            if (pending > 0)
                voice.advanceStart (numSamples);

            continue;
        }

        if (pending >= numSamples)
        {
            voice.advanceStart (numSamples);
            continue;
        }

        const auto channel = channelForLane (voice.getLane());
        const auto& cs = settings[static_cast<size_t> (juce::jlimit (0, kNumChannels - 1, channel))];

        const auto volume = juce::jlimit (0.0f, ids::kPercentMax, cs.vol) / ids::kPercentMax;

        float gainLeft = 0.0f, gainRight = 0.0f;
        monoPanGains (normalisedPan (cs.pan), gainLeft, gainRight);

        gainLeft  *= volume;
        gainRight *= volume;

        const auto start = juce::jmax (0, pending);

        for (int s = start; s < numSamples && voice.isActive(); ++s)
        {
            const auto sample = voice.nextSample (rng);

            left[s] += sample * gainLeft;

            if (right != nullptr)
                right[s] += sample * gainRight;
        }

        voice.setSamplesUntilStart (0);
    }

    // ── sampled zabumba: stereo into the stereo pan law ─────────────────────
    const auto zabumbaChannel = juce::jlimit (0, kNumChannels - 1, channelForLane (0));
    const auto& zabumbaSettings = settings[static_cast<size_t> (zabumbaChannel)];
    const auto zabumbaVolume = juce::jlimit (0.0f, ids::kPercentMax, zabumbaSettings.vol)
                                 / ids::kPercentMax;

    float leftToLeft = 1.0f, rightToLeft = 0.0f, leftToRight = 0.0f, rightToRight = 1.0f;
    stereoPanGains (normalisedPan (zabumbaSettings.pan),
                    leftToLeft, rightToLeft, leftToRight, rightToRight);

    for (auto& voice : sampleVoices)
    {
        if (! voice.active)
        {
            if (voice.samplesUntilStart > 0)
                voice.samplesUntilStart -= numSamples;

            continue;
        }

        if (voice.samplesUntilStart >= numSamples)
        {
            voice.samplesUntilStart -= numSamples;
            continue;
        }

        const auto start = juce::jmax (0, voice.samplesUntilStart);
        const auto gain = voice.gain * zabumbaVolume;

        for (int s = start; s < numSamples; ++s)
        {
            if (voice.pos >= voice.envSamples || voice.position >= voice.lengthSamples)
            {
                voice.active = false;
                break;
            }

            const auto envelope = truncationGain (voice.pos, voice.envSamples, voice.releaseSamples);

            const auto sourceLeft  = sampler.readSample (voice.slot, 0, voice.position);
            const auto sourceRight = sampler.readSample (voice.slot, 1, voice.position);

            const auto outLeft  = (sourceLeft * leftToLeft + sourceRight * rightToLeft) * gain * envelope;
            const auto outRight = (sourceLeft * leftToRight + sourceRight * rightToRight) * gain * envelope;

            left[s] += outLeft;

            if (right != nullptr)
                right[s] += outRight;

            voice.position += voice.readRate;
            voice.pos += 1.0;
        }

        voice.samplesUntilStart = 0;
    }
}

int VoiceEngine::getActiveVoiceCount() const noexcept
{
    auto count = 0;

    for (const auto& voice : synthVoices)
        if (voice.isActive())
            ++count;

    for (const auto& voice : sampleVoices)
        if (voice.active)
            ++count;

    return count;
}

} // namespace forrobox
