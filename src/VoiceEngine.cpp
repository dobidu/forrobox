#include "VoiceEngine.h"

#include "Atomics.h"

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

    // 32 ms at the prepared rate: 1536 samples at 48 kHz, 1411 at 44.1 kHz.
    lookaheadSamples = lookaheadSamplesFor (sampleRate);

    for (auto& voice : synthVoices)
        voice.prepare (sampleRate);

    sampler.prepare (sampleRate);

    prepared = true;

    reset();
}

void VoiceEngine::reset() noexcept
{
    for (auto& voice : synthVoices)
        voice.clear();

    for (auto& voice : sampleVoices)
        voice = {};

    // Reseeding both is what makes "render the same bars twice and compare" a
    // meaningful assertion rather than a coincidence.
    noiseRng.setSeed (kNoiseSeed);
    stepCounter = 0;

    nextStartOrder = 1;
    activeVoices.store (0, std::memory_order_relaxed);
    peakActiveVoices.store (0, std::memory_order_relaxed);
    voicesStolen.store (0, std::memory_order_relaxed);
    voicesDropped.store (0, std::memory_order_relaxed);
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
    voicesStolen.fetch_add (1, std::memory_order_relaxed);

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

    voicesStolen.fetch_add (1, std::memory_order_relaxed);

    return oldest;
}

void VoiceEngine::scheduleStep (const StepVelocities& velocities, int sampleOffset) noexcept
{
    if (! prepared)
        return;

    // This step's key, taken before anything can fail, so the humanisation
    // sequence advances once per step whatever the lanes turn out to hold.
    const auto step = stepCounter++;
    const auto seed = kHumanisationSeed + seedOffset;

    // Delayed by the lookahead FIRST, and once — not once per lane. That is
    // what makes a bipolar jitter representable: `lookahead + jitter` cannot go
    // negative, so nothing is ever clamped and the placement stays independent
    // of where the block boundary happened to fall.
    const auto origin = sampleOffset + lookaheadSamples;

    // ── the ONE timing jitter, keyed on the step alone ──────────────────────
    //
    //  Per STEP, not per hit. app.js computes it outside scheduleStep and
    //  passes one time in, so every lane of the step moves together. A per-lane
    //  value is not a compile error and not a failing spectral test — it is the
    //  whole step breathing against the lanes flamming apart.
    //
    //  Lane 0 is the key's lane component for a step-wide value: the purpose
    //  distinguishes it from lane 0's own per-hit values, so there is no
    //  collision.
    const auto cachaca01 = blockSettings.cachaca;

    const auto jitter = static_cast<int> (
        std::lround (static_cast<double> (cachaca01) * kMaxJitterSeconds * sampleRate
                       * bipolarHumanisedValue (seed, step, 0, Purpose::jitter)));

    const auto stepOffset = origin + jitter;

    for (size_t laneIndex = 0; laneIndex < velocities.size(); ++laneIndex)
    {
        const auto lane = static_cast<int> (laneIndex);

        // Channel resolution and the audibility gate happen ONCE, for both a
        // hit and a ghost.
        //
        // maybeGhost used to re-do all of this — the bounds check, the channel
        // lookup, the mute/solo gate — and then call the same playVelocity. A
        // ghost is a hit whose velocity comes from a roll instead of the grid,
        // and saying so collapses two paths into one.
        const auto channel = channelForLane (lane);

        if (! juce::isPositiveAndBelow (lane, static_cast<int> (voiceSpecs.size()))
            || ! juce::isPositiveAndBelow (channel, kNumChannels))
            continue;

        const auto& channelSettings = blockSettings.channels[static_cast<size_t> (channel)];

        // Gated at SCHEDULE, not render: a muted channel must not consume
        // voices an audible one needs. A note already sounding when its channel
        // is muted is left to finish, which is what the prototype's graph does
        // — the mute is a gain, applied downstream of notes already in flight.
        if (! channelSettings.audible)
            continue;

        auto velocity = 0.0f;
        auto offset = stepOffset;

        if (velocities[laneIndex] > 0)
        {
            velocity = static_cast<float> (velocities[laneIndex])
                         / static_cast<float> (State::kMaxVelocity);

            // Velocity variation, per HIT — the opposite of the timing jitter,
            // and the sketch's own asymmetry: `v *= (1 - cach * 0.25 *
            // Math.random())` sits inside the per-hit play(). The roll is
            // [0, 1), so the multiplier is (0.75, 1.0] and a hit can never be
            // made LOUDER.
            velocity *= 1.0f - cachaca01 * kVelocityHumaniseDepth
                                 * humanisedValue (seed, step, lane, Purpose::velocity);
        }
        else if (detail::laneCanGhost (lane))
        {
            // "Ghost notes add unwritten in-between hits, which is much of what
            // makes the groove feel human" — PLANNING.md. Performance, not
            // data: never written into the pattern, never exported to MIDI, and
            // deliberately absent from lastStepVelocities, which carries the
            // step's PROGRAMMED content for Phase 5's pads.
            //
            // Only where the pattern is SILENT: `if (v > 0) play(...) else
            // ghost(...)`. Never both on one lane on one step.
            const auto ghostAmount = ids::normalisedPercent (channelSettings.ghost);

            if (ghostAmount <= 0.0f)
                continue;

            // chance = (ghost/100) x (0.22 + (cachaca/100) x 0.6).
            //
            // Note the base term: CACHAÇA RAISES the rate, it does not gate it.
            // At CACHAÇA 0 ghosts still fire at (ghost/100) x 0.22.
            const auto chance = ghostAmount * (kGhostBaseChance
                                                 + cachaca01 * kGhostCachacaSpan);

            if (humanisedValue (seed, step, lane, Purpose::ghostRoll) >= chance)
                continue;

            // +/-10 ms from the step's ALREADY-JITTERED offset, not from its
            // grid position — `t2 = t + (random - 0.5) * 0.02`, where `t`
            // carries the jitter. The two compound, which is why the lookahead
            // is 32 ms and not 22.
            offset += static_cast<int> (
                std::lround (kGhostJitterSeconds * sampleRate
                               * bipolarHumanisedValue (seed, step, lane, Purpose::ghostOffset)));

            // Already normalised, and NOT put through the velocity humanisation
            // above: it is random already.
            velocity = kGhostVelocityMin
                     + humanisedValue (seed, step, lane, Purpose::ghostVelocity)
                         * kGhostVelocitySpan;
        }

        // The one post-condition covering both sources: a rest never sounds.
        if (velocity > 0.0f)
            playVelocity (lane, velocity, offset, channelSettings);
    }
}

void VoiceEngine::playVelocity (int lane, float velocity, int sampleOffset,
                                const ChannelSettings& channelSettings) noexcept
{
    // The one place a normalised velocity becomes a voice, so ghost notes —
    // whose velocity is already normalised and must NOT be humanised again —
    // can reach it without round-tripping through the uint8 grid velocity and
    // being quantised on the way.
    if (voiceSpecs[static_cast<size_t> (lane)].usesSample)
        scheduleSample (lane, velocity, sampleOffset, channelSettings);
    else
        scheduleSynth (lane, velocity, sampleOffset, channelSettings);
}

void VoiceEngine::scheduleSynth (int lane, float velocity, int sampleOffset,
                                 const ChannelSettings& channelSettings) noexcept
{
    auto* voice = claimSynthVoice (lane);

    // Structurally non-null while the pool is non-empty — claimSynthVoice
    // steals rather than fails — but checked rather than assumed.
    if (voice == nullptr)
    {
        voicesDropped.fetch_add (1, std::memory_order_relaxed);
        return;
    }

    // Only stamp a voice that actually took the note. Stamping one that did not
    // left it sounding its previous note behind the newest stealing order,
    // pinning the slot and losing the new note without recording anything.
    if (! voice->trigger (lane, velocity, channelSettings.pitch, channelSettings.decay,
                          kHumanisationSeed + seedOffset, stepCounter - 1))
    {
        voicesDropped.fetch_add (1, std::memory_order_relaxed);
        return;
    }

    // NOT jmax(0, ...). 03-02's plan said "avoid jmax(0, …) anywhere on an
    // offset — the clamp is what this task exists to remove", and two clamps
    // survived it. While one stands, any future insufficiency — 03-03 adding a
    // further offset, a lookahead that no longer covers the excursion — is
    // silently absorbed instead of failing. lookaheadSamplesFor is built from
    // the same expressions as the excursions, so this holds by construction.
    jassert (sampleOffset >= 0);
    voice->setSamplesUntilStart (sampleOffset);
    voice->setStartOrder (nextStartOrder++);
}

void VoiceEngine::scheduleSample (int lane, float velocity, int sampleOffset,
                                  const ChannelSettings& channelSettings) noexcept
{
    if (! sampler.isReady())
        return;

    const auto blend = sampler.blendForVelocity (velocity);
    const auto pitchFactor = pitchFactorForSemitones (static_cast<double> (channelSettings.pitch));

    // PLANNING.md line ~730: for a sampled channel DECAY becomes an amplitude
    // envelope that may truncate the tail and must never extend it past the
    // file. decayScale spans 0.4-1.8, so it is normalised by its own maximum:
    // DECAY at 100 plays the file whole, and lower values shorten it
    // proportionally rather than scaling it past its own end.
    // kDecayScaleMax rather than a hand-written 1.8: the maximum belongs to the
    // formula, not to this call site.
    const auto decayScale = static_cast<double> (decayScaleFor (channelSettings.decay));
    const auto decayFraction = juce::jlimit (0.0, 1.0,
                                             decayScale / static_cast<double> (kDecayScaleMax));

    const std::array<std::pair<int, float>, 2> parts {{
        { blend.slotA, blend.gainA },
        { blend.slotB, blend.gainB },
    }};

    for (const auto& [slot, mix] : parts)
    {
        if (slot < 0 || mix <= 0.0f)
            continue;

        const auto lengthSamples = static_cast<double> (sampler.getLengthSamples (slot));

        // Per slot: each file's own rate against the host's, times PITCH.
        const auto readRate = sampler.getBaseReadRate (slot) * pitchFactor;

        // Counted, not silently skipped. These two are the only paths that can
        // actually reach the drop counter — and the comment on getVoicesDropped
        // claimed exactly that while the code `continue`d without incrementing,
        // so the counter remained unreachable and the claim was false. That is
        // the "measure the claim in the comment" rule from 02-04, applied to a
        // comment written in the same plan that recorded it.
        if (lengthSamples <= 0.0 || readRate <= 0.0)
        {
            voicesDropped.fetch_add (1, std::memory_order_relaxed);
            continue;
        }

        auto* voice = claimSampleVoice();

        if (voice == nullptr)
        {
            voicesDropped.fetch_add (1, std::memory_order_relaxed);
            continue;
        }

        // Output samples the file spans at this read rate.
        const auto playableSamples = lengthSamples / readRate;

        voice->active = true;
        voice->slot = slot;
        voice->channel = channelForLane (lane);
        voice->sourceIsStereo = sampler.getNumChannels (slot) > 1;
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
        jassert (sampleOffset >= 0);
        voice->samplesUntilStart = sampleOffset;
        voice->startOrder = nextStartOrder++;
    }
}

void VoiceEngine::render (juce::AudioBuffer<float>& buffer) noexcept
{
    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0 || ! prepared)
        return;

    // maxBlockSize was stored by prepare and read nowhere — a field with no
    // reader, which is the same category of thing as a guarantee with no
    // caller. It states a contract, so it now enforces one.
    jassert (maxBlockSize <= 0 || numSamples <= maxBlockSize);

    auto* left  = buffer.getWritePointer (0);
    auto* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    // Aliases `left` on a mono output, so every write below can be
    // unconditional and both pan-matrix terms always land somewhere.
    auto* rightOut = right != nullptr ? right : left;

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
        const auto& cs = blockSettings.channels[static_cast<size_t> (juce::jlimit (0, kNumChannels - 1, channel))];

        const auto volume = ids::normalisedPercent (cs.vol);

        float gainLeft = 0.0f, gainRight = 0.0f;
        monoPanGains (ids::normalisedPan (cs.pan), gainLeft, gainRight);

        gainLeft  *= volume;
        gainRight *= volume;

        const auto start = juce::jmax (0, pending);

        // With no right channel, `rightOut` aliases `left` so BOTH matrix terms
        // land in the one output — which is the fold-down, without a second
        // copy of the loop or a per-sample branch.
        //
        // Writing only the left term made a hard-RIGHT channel vanish outright:
        // at PAN +50 monoPanGains returns gainLeft = cos(pi/2) = 0. Unreachable
        // while isBusesLayoutSupported accepts stereo only, but this aliasing
        // exists to support the MULTI-OUT mode whose parameter is already
        // declared, so it is a trap for whoever relaxes that check.
        for (int s = start; s < numSamples && voice.isActive(); ++s)
        {
            const auto sample = voice.nextSample (noiseRng);

            left[s]     += sample * gainLeft;
            rightOut[s] += sample * gainRight;
        }

        voice.setSamplesUntilStart (0);
    }

    // ── sampled voices: stereo into the stereo pan law ──────────────────────
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

        // The voice's OWN channel, not lane 0's. Read per voice so a second
        // sampled lane cannot silently inherit ZABUMBA's VOL and PAN.
        const auto& cs = blockSettings.channels[static_cast<size_t> (juce::jlimit (0, kNumChannels - 1,
                                                                                    voice.channel))];

        // The pan law follows the SOURCE, not the pool. A stereo file keeps its
        // own image through Web Audio's stereo law; a mono one is placed by the
        // equal-power law, exactly as the seven synthesised voices are. Picking
        // by pool gave a hard-panned mono file 2x amplitude.
        float leftToLeft = 1.0f, rightToLeft = 0.0f, leftToRight = 0.0f, rightToRight = 1.0f;

        if (voice.sourceIsStereo)
        {
            stereoPanGains (ids::normalisedPan (cs.pan),
                            leftToLeft, rightToLeft, leftToRight, rightToRight);
        }
        else
        {
            float gainLeft = 0.0f, gainRight = 0.0f;
            monoPanGains (ids::normalisedPan (cs.pan), gainLeft, gainRight);

            // Only channel 0 is read for a mono source, so the right-hand
            // column stays zero rather than double-counting it.
            leftToLeft   = gainLeft;
            leftToRight  = gainRight;
            rightToLeft  = 0.0f;
            rightToRight = 0.0f;
        }

        const auto start = juce::jmax (0, voice.samplesUntilStart);
        const auto gain = voice.gain
                            * (ids::normalisedPercent (cs.vol));

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

            left[s]     += outLeft;
            rightOut[s] += outRight;

            voice.position += voice.readRate;
            voice.pos += 1.0;
        }

        voice.samplesUntilStart = 0;
    }

    // Published once per block rather than counted on demand from the message
    // thread: the `active` flags are plain bools written here, so reading them
    // from another thread is a data race. The peak is what makes the pool
    // arithmetic in the header checkable.
    auto sounding = 0;

    for (const auto& voice : synthVoices)
        if (voice.isActive())
            ++sounding;

    for (const auto& voice : sampleVoices)
        if (voice.active)
            ++sounding;

    activeVoices.store (sounding, std::memory_order_relaxed);

    // The SECOND instance of the publish-a-running-max shape, and it had the
    // same defect MixBus's did: a load and a store are not a read-modify-write.
    // Less severe here — this cell is cleared by `prepare` rather than by a
    // reader, so a lost update drops a peak instead of resurrecting a consumed
    // one — but it is the same law, and it is now written in one place.
    // Found by /simplify on 04-05, one file over from where /code-review found
    // the first.
    atomicMax (peakActiveVoices, sounding);
}

} // namespace forrobox
