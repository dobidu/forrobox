#include "Voices.h"

namespace forrobox
{
namespace
{
    /** A band-limited square: the odd harmonics that fit below Nyquist.

        A naive square would fold every harmonic above Nyquist back into the
        audible band, and the triângulo's partials run to 11.2 kHz where the
        third harmonic is already out of range at 48 kHz. Harmonics are summed
        by the Chebyshev recurrence sin(k+1)x = 2cos(x)sin(kx) - sin(k-1)x, so a
        partial costs one sine and one cosine regardless of how many harmonics
        survive rather than one sine each. */
    float bandlimitedSquare (double phase, double freq, double nyquist) noexcept
    {
        const auto s1 = std::sin (phase);
        const auto c1 = std::cos (phase);

        auto previous = 0.0;    // sin(0x)
        auto current  = s1;     // sin(1x)
        auto sum      = 0.0;

        for (int k = 1; static_cast<double> (k) * freq < nyquist && k < 64; ++k)
        {
            if (k % 2 == 1)
                sum += current / static_cast<double> (k);

            const auto next = 2.0 * c1 * current - previous;
            previous = current;
            current  = next;
        }

        return static_cast<float> (sum * 4.0 / juce::MathConstants<double>::pi);
    }

    /** Naive triangle, phase-aligned to start at zero and rise, matching Web
        Audio's `triangle` oscillator.

        Naive is defensible here where it is not for the square: a triangle's
        harmonics fall as 1/k², so the first one that folds is already 40 dB
        down, and the only voice using it sits at 190 Hz. */
    float triangleWave (double phase) noexcept
    {
        const auto turns = phase / juce::MathConstants<double>::twoPi + 0.25;
        const auto frac  = turns - std::floor (turns);
        return static_cast<float> (4.0 * std::abs (frac - 0.5) - 1.0);
    }

    float whiteNoise (juce::Random& source) noexcept
    {
        return source.nextFloat() * 2.0f - 1.0f;
    }

    juce::dsp::StateVariableTPTFilterType toJuceFilterType (FilterKind kind) noexcept
    {
        switch (kind)
        {
            case FilterKind::lowpass:  return juce::dsp::StateVariableTPTFilterType::lowpass;
            case FilterKind::bandpass: return juce::dsp::StateVariableTPTFilterType::bandpass;
            case FilterKind::highpass: return juce::dsp::StateVariableTPTFilterType::highpass;
            case FilterKind::none:     break;
        }
        return juce::dsp::StateVariableTPTFilterType::lowpass;
    }
}

// ── SynthVoice ──────────────────────────────────────────────────────────────

void SynthVoice::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    nyquist    = sampleRate * 0.5;

    // The one allocating call in this class, and it happens with the audio
    // device stopped. processSample, setCutoffFrequency, setResonance and
    // reset() are all allocation-free once the state vectors exist.
    const juce::dsp::ProcessSpec spec { sampleRate, 1u, 1u };

    for (auto& layer : layers)
        layer.filter.prepare (spec);

    clear();
}

void SynthVoice::clear() noexcept
{
    active = false;
    durationSamples = 0.0;

    for (auto& layer : layers)
    {
        layer.generator = Generator::none;
        layer.env = 0.0f;
        layer.pos = 0.0;
        layer.durationSamples = 0.0;
        layer.filter.reset();
    }
}

void SynthVoice::trigger (int laneToPlay, float velocity, float pitchSemitones,
                          float decayPercent, juce::Random& detuneRandom) noexcept
{
    if (! juce::isPositiveAndBelow (laneToPlay, static_cast<int> (voiceSpecs.size())))
        return;

    const auto& spec = voiceSpecs[static_cast<size_t> (laneToPlay)];

    lane = laneToPlay;

    const auto v = juce::jlimit (0.0f, 1.0f, velocity);

    // Both from PLANNING.md's preamble to the Voice Specifications table.
    const auto pitchFactor = std::pow (2.0f, pitchSemitones / 12.0f);
    const auto decayScale  = 0.4f + juce::jlimit (0.0f, ids::kPercentMax, decayPercent)
                                      / ids::kPercentMax * 1.4f;

    // The velocity split, where a lane has one. `>` not `>=`, matching the
    // sketch's `v > 0.55` — the boundary velocity is the CLOSED articulation.
    const auto base = (spec.openThreshold > 0.0f && v > spec.openThreshold)
                        ? spec.openDurBase
                        : spec.durBase;

    const auto durationSeconds = (base + spec.durPerVelocity * v) * decayScale;

    durationSamples = 0.0;

    for (size_t i = 0; i < layers.size(); ++i)
    {
        auto& layer     = layers[i];
        const auto& ls  = spec.layers[i];

        layer.generator = ls.generator;
        layer.pos       = 0.0;
        layer.env       = 0.0f;
        layer.numPartials = 0;
        layer.f0 = {};
        layer.f1 = {};
        layer.phase = {};
        layer.weight = {};

        if (ls.generator == Generator::none)
        {
            layer.durationSamples = 0.0;
            continue;
        }

        const auto layerSeconds = ls.fixedDurationSeconds > 0.0f
                                    ? ls.fixedDurationSeconds
                                    : durationSeconds * ls.durationFactor;

        layer.durationSamples = static_cast<double> (layerSeconds) * sampleRate;
        layer.attackSamples   = juce::jmin (static_cast<double> (ls.attackSeconds) * sampleRate,
                                            layer.durationSamples * 0.5);
        layer.sweepSamples    = layer.durationSamples * static_cast<double> (ls.sweepFraction);
        layer.peak            = ls.peakPerVelocity * v;

        // The envelope is Web Audio's: linear to the peak over the attack, then
        // exponential to kEnvelopeFloor at the layer's end. Precomputing the
        // per-sample multiplier keeps it exact without an exp() per sample.
        //
        // A peak at or below the floor would make that ramp rise instead of
        // fall, so it is silent by construction — the same call Web Audio's
        // exponentialRamp makes when handed a target above the current value.
        if (layer.peak > kEnvelopeFloor && layer.durationSamples > layer.attackSamples)
        {
            const auto decaySamples = layer.durationSamples - layer.attackSamples;
            layer.envDecayMultiplier =
                static_cast<float> (std::pow (static_cast<double> (kEnvelopeFloor / layer.peak),
                                              1.0 / decaySamples));
        }
        else
        {
            layer.peak = 0.0f;
            layer.envDecayMultiplier = 0.0f;
        }

        const auto oscillatorFactor = ls.pitchTracksOscillator ? pitchFactor : 1.0f;

        if (ls.generator == Generator::detunedSquares)
        {
            layer.numPartials = static_cast<int> (trianguloPartials.size());

            for (size_t p = 0; p < trianguloPartials.size(); ++p)
            {
                // +/-0.5% per partial per hit. Drawn from the engine's seeded
                // generator so a render is reproducible.
                const auto detune = 1.0 + (static_cast<double> (detuneRandom.nextFloat()) - 0.5) * 0.01;
                const auto f = static_cast<double> (trianguloPartials[p] * oscillatorFactor) * detune;

                layer.f0[p] = f;
                layer.f1[p] = f;
                layer.weight[p] = static_cast<double> (trianguloPartialWeight (static_cast<int> (p)));
            }
        }
        else if (ls.generator == Generator::sweptSine || ls.generator == Generator::fixedTriangle)
        {
            layer.numPartials = 1;
            layer.f0[0] = static_cast<double> (ls.f0 * oscillatorFactor);
            layer.f1[0] = ls.f1 > 0.0f ? static_cast<double> (ls.f1 * oscillatorFactor)
                                       : layer.f0[0];
            layer.weight[0] = 1.0;
        }

        // `>` rather than `!=`: a sweep exists only when the spec gave a
        // non-zero, different target. Decided here so nextSample does not
        // compare two floats for equality on every sample.
        layer.sweeps = ls.f1 > 0.0f && std::abs (ls.f1 - ls.f0) > 1.0e-6f;

        layer.hasFilter = ls.filter != FilterKind::none;

        if (layer.hasFilter)
        {
            const auto filterFactor = ls.pitchTracksFilter ? pitchFactor : 1.0f;

            layer.filter.reset();
            layer.filter.setType (toJuceFilterType (ls.filter));
            // Nyquist-limited: setCutoffFrequency asserts below sampleRate/2,
            // and a pitched-up ganzá bandpass at 6.8 kHz reaches it at 44.1 kHz
            // by +7 semitones.
            layer.filter.setCutoffFrequency (
                juce::jlimit (20.0f, static_cast<float> (nyquist) * 0.99f, ls.filterFreq * filterFactor));
            // JUCE's SVF uses R2 = 1/resonance, so `resonance` IS Q — the same
            // convention as Web Audio's BiquadFilterNode, which these Q values
            // came from.
            layer.filter.setResonance (juce::jmax (0.05f, ls.filterQ));
        }

        durationSamples = juce::jmax (durationSamples, layer.durationSamples);
    }

    active = durationSamples > 0.0;
}

void SynthVoice::Layer::advanceEnvelope() noexcept
{
    if (pos < attackSamples)
        env = peak * static_cast<float> (pos / juce::jmax (1.0, attackSamples));
    else if (env <= 0.0f)
        env = peak;
    else
        env *= envDecayMultiplier;

    pos += 1.0;
}

float SynthVoice::Layer::nextSample (double sampleRateToUse, double nyquistToUse,
                                     juce::Random& noiseSource) noexcept
{
    if (generator == Generator::none || pos >= durationSamples)
        return 0.0f;

    auto raw = 0.0f;

    switch (generator)
    {
        case Generator::filteredNoise:
            raw = whiteNoise (noiseSource);
            break;

        case Generator::sweptSine:
        case Generator::fixedTriangle:
        case Generator::detunedSquares:
        {
            // The sweep is exponential in frequency over `sweepSamples`, then
            // holds — Web Audio's exponentialRampToValueAtTime, which does not
            // continue past its target time.
            const auto sweep = sweepSamples > 0.0 ? juce::jmin (1.0, pos / sweepSamples) : 1.0;

            for (int p = 0; p < numPartials; ++p)
            {
                const auto i = static_cast<size_t> (p);
                const auto freq = sweeps ? f0[i] * std::pow (f1[i] / f0[i], sweep)
                                         : f0[i];

                if (generator == Generator::sweptSine)
                    raw += static_cast<float> (std::sin (phase[i]) * weight[i]);
                else if (generator == Generator::fixedTriangle)
                    raw += triangleWave (phase[i]) * static_cast<float> (weight[i]);
                else
                    raw += bandlimitedSquare (phase[i], freq, nyquistToUse)
                             * static_cast<float> (weight[i]);

                phase[i] += juce::MathConstants<double>::twoPi * freq / sampleRateToUse;

                if (phase[i] > juce::MathConstants<double>::twoPi)
                    phase[i] -= juce::MathConstants<double>::twoPi;
            }
            break;
        }

        case Generator::none:
            break;
    }

    // Filter BEFORE the envelope, matching the sketch's graph: the source feeds
    // the filter and the filter feeds the envelope gain. The other order would
    // let the filter ring on past the note.
    if (hasFilter)
        raw = filter.processSample (0, raw);

    advanceEnvelope();

    return raw * env;
}

float SynthVoice::nextSample (juce::Random& noiseSource) noexcept
{
    if (! active)
        return 0.0f;

    auto sum = 0.0f;
    auto anyRunning = false;

    for (auto& layer : layers)
    {
        if (layer.generator == Generator::none || layer.pos >= layer.durationSamples)
            continue;

        sum += layer.nextSample (sampleRate, nyquist, noiseSource);
        anyRunning = true;
    }

    if (! anyRunning)
        active = false;

    return sum;
}

} // namespace forrobox
