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
    // `1 - wet * 0.5`, NOT `1 - wet`. The paths deliberately sum past unity.
    return 1.0f - wetGainFor (timbreIndex, charMixPercent) * 0.5f;
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

    // Snapped to the defaults rather than smoothed up from zero: a fresh
    // instance must render identically to another fresh instance, and a
    // 20 ms ramp-in would make the first block depend on history there is none
    // of.
    cutoffHz   = timbreSpecs[0].cutoffHz;
    wetGain    = wetGainFor (0, 40.0f);
    dryGain    = dryGainFor (0, 40.0f);
    masterGain = 0.82f * 0.82f;

    appliedCutoffHz = -1.0f;
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
    const auto targetDry = dryGainFor (settings.timbreIndex, settings.charMix);

    // `gain = (value/100)^2` — PLANNING.md's perceptual taper.
    const auto normalisedMaster = ids::normalisedPercent (settings.master);
    const auto targetMaster = normalisedMaster * normalisedMaster;

    // The drive is NOT smoothed. It is the waveshaper's own curve rather than a
    // gain, the sketch rebuilds the curve outright on a timbre change, and the
    // two gains either side of it are smoothed — so a step in the curve is
    // masked by the wet path fading through it.
    const auto drive = timbre.drive;

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
        dryGain    = targetDry;
        masterGain = targetMaster;
        needsSnap  = false;
    }

    auto peakBeforeLimiter = 0.0f;
    auto peakAfterLimiter = 0.0f;

    for (int s = 0; s < numSamples; ++s)
    {
        // ── smoothed per sample ─────────────────────────────────────────────
        cutoffHz   = smoothTowards (cutoffHz, targetCutoff);
        wetGain    = smoothTowards (wetGain, targetWet);
        dryGain    = smoothTowards (dryGain, targetDry);
        masterGain = smoothTowards (masterGain, targetMaster);

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

            peakBeforeLimiter = juce::jmax (peakBeforeLimiter, std::abs (shaped));

            const auto limited = limiter.processSample (c, shaped);

            peakAfterLimiter = juce::jmax (peakAfterLimiter, std::abs (limited));

            buffer.setSample (c, s, limited * masterGain);
        }
    }

    // Gain reduction as a per-block peak ratio, positive dB. Zero when nothing
    // was removed, which is also what "limiter off" gives, since the
    // transparent settings pass the signal through unchanged.
    const auto reduction = (peakBeforeLimiter > 1.0e-6f && peakAfterLimiter > 0.0f)
                             ? juce::jmax (0.0f, juce::Decibels::gainToDecibels (peakBeforeLimiter)
                                                   - juce::Decibels::gainToDecibels (peakAfterLimiter))
                             : 0.0f;

    gainReductionDb.store (reduction, std::memory_order_relaxed);
}

} // namespace forrobox
