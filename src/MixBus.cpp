#include "MixBus.h"

#include <cmath>

namespace forrobox
{

float MixBus::wetGainFor (int timbreIndex, float charMixPercent) noexcept
{
    const auto index = static_cast<size_t> (juce::jlimit (0, static_cast<int> (timbreSpecs.size()) - 1,
                                                          timbreIndex));
    const auto mix = ids::normalisedPercent (charMixPercent);

    // `const wet = id === "hifi" ? m * 0.5 : m;`
    return timbreSpecs[index].halvesWet ? mix * 0.5f : mix;
}

float MixBus::dryGainFor (int timbreIndex, float charMixPercent) noexcept
{
    return dryForWet (wetGainFor (timbreIndex, charMixPercent));
}

float MixBus::masterGainFor (float masterPercent) noexcept
{
    const auto normalised = ids::normalisedPercent (masterPercent);

    return normalised * normalised;
}

void MixBus::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;

    const juce::dsp::ProcessSpec spec { sampleRate,
                                        static_cast<juce::uint32> (juce::jmax (1, maxBlockSize)),
                                        2u };

    characterFilter.prepare (spec);
    characterFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    characterFilter.setResonance (kCharacterFilterQ);

    limiter.prepare (spec);
    limiter.setAttack (kLimiterAttackMs);
    limiter.setRelease (kLimiterReleaseMs);

    // One exponential step of setTargetAtTime: v += (target - v) * (1 - e^(-1/(tau*fs))).
    smoothingCoeff = static_cast<float> (1.0 - std::exp (-1.0 / (kBusSmoothingSeconds * sampleRate)));

    prepared = true;

    reset();
}

void MixBus::reset() noexcept
{
    characterFilter.reset();
    limiter.reset();

    // No values are assigned here. A first version set the four smoothed
    // members to hardcoded defaults — timbre 0, MIX 40, master 82, a third copy
    // of figures that live in createParameterLayout — and `needsSnap`
    // overwrites every one of them from the real targets before anything reads
    // them. They were dead, and they disagreed with the in-class initialisers,
    // so the file carried two different "defaults" for values that never
    // mattered.
    appliedCutoffHz = 0.0f;
    needsSnap = true;

    gainReductionDb.store (0.0f, std::memory_order_relaxed);
}

void MixBus::process (juce::AudioBuffer<float>& buffer, const Settings& settings) noexcept
{
    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = buffer.getNumChannels();

    if (! prepared || numSamples <= 0 || numChannels <= 0)
        return;

    const auto index = static_cast<size_t> (
        juce::jlimit (0, static_cast<int> (timbreSpecs.size()) - 1, settings.timbreIndex));
    const auto& timbre = timbreSpecs[index];

    const auto targetCutoff = juce::jlimit (20.0f, static_cast<float> (sampleRate) * 0.49f,
                                            timbre.cutoffHz);
    const auto targetWet = wetGainFor (settings.timbreIndex, settings.charMix);
    const auto targetMaster = masterGainFor (settings.master);
    const auto targetDrive = timbre.drive;

    // "Off" is threshold 0 dB and ratio 1:1 — the SAME object, made
    // transparent. PLANNING.md: "bypassed rather than removed, to avoid a
    // click." A branch around it would be exactly the click it warns about.
    limiter.setThreshold (settings.limiterOn ? kLimiterThresholdDb : 0.0f);
    limiter.setRatio (settings.limiterOn ? kLimiterRatio : 1.0f);

    // A fresh instance starts AT its settings rather than sliding to them over
    // 20 ms from the constructor's defaults.
    if (needsSnap)
    {
        cutoffHz   = targetCutoff;
        wetGain    = targetWet;
        drive      = targetDrive;
        masterGain = targetMaster;
        needsSnap  = false;
    }

    auto lowestGain = 1.0f;

    for (int s = 0; s < numSamples; ++s)
    {
        // ── smoothed per sample ─────────────────────────────────────────────
        cutoffHz   = smoothTowards (cutoffHz, targetCutoff);
        wetGain    = smoothTowards (wetGain, targetWet);
        drive      = smoothTowards (drive, targetDrive);
        masterGain = smoothTowards (masterGain, targetMaster);

        // Derived, not smoothed separately, and through the same function
        // `dryGainFor` uses so the law cannot differ between them.
        const auto dryGain = dryForWet (wetGain);

        // setCutoffFrequency calls std::tan, so it is skipped while the
        // smoothed value is not moving — every block except the ~20 ms after a
        // change. Driven by the smoothed value rather than by the block
        // boundary, so the trajectory stays block-size independent.
        if (std::abs (cutoffHz - appliedCutoffHz) > appliedCutoffHz * 1.0e-4f)
        {
            characterFilter.setCutoffFrequency (cutoffHz);
            appliedCutoffHz = cutoffHz;
        }

        for (int c = 0; c < numChannels; ++c)
        {
            const auto dry = buffer.getSample (c, s);

            // The wet path: lowpass into tanh.
            //
            // tanh applied DIRECTLY, not through Web Audio's 1024-point curve.
            // That curve spans [-1, 1] and CLAMPS beyond it, so at drive 1.2 an
            // input of 2.0 comes out at tanh(1.2) = 0.834 where this gives
            // 0.984 — and the grooves peak at 1.454, so inputs do exceed 1.
            // A hard ceiling at an arbitrary input level is a table artefact
            // rather than intent, and PROJECT.md's rule is that correct plugin
            // practice wins where the two conflict. Recorded as a deviation;
            // the A/B listen is where it gets judged.
            const auto wet = std::tanh (characterFilter.processSample (c, dry) * drive);

            const auto shaped = dry * dryGain + wet * wetGain;
            const auto limited = limiter.processSample (c, shaped);

            // The gain the limiter actually applied. processSample returns
            // `gain * input`, so the ratio IS the gain, and the minimum over the
            // block is the peak reduction — the same quantity as the
            // prototype's `-limiterNode.reduction`.
            //
            // The epsilon is the one the previous peak-ratio form already
            // needed; guarding on it is what makes the divide safe between
            // hits.
            if (const auto magnitude = std::abs (shaped); magnitude > 1.0e-6f)
                lowestGain = juce::jmin (lowestGain, std::abs (limited) / magnitude);

            buffer.setSample (c, s, limited * masterGain);
        }
    }

    // Accumulated as the MAXIMUM since the value was last taken, so a caller
    // polling slower than the audio thread cannot miss a peak. Zero when
    // nothing was removed, which is also what "limiter off" gives, since the
    // transparent settings pass the signal through unchanged.
    const auto reduction = juce::jmax (0.0f, -juce::Decibels::gainToDecibels (lowestGain));

    gainReductionDb.store (juce::jmax (gainReductionDb.load (std::memory_order_relaxed), reduction),
                           std::memory_order_relaxed);

    publishedWetGain.store (wetGain, std::memory_order_relaxed);
}

} // namespace forrobox
