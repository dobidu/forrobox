#include "Voices.h"

namespace forrobox
{
namespace
{
    inline constexpr double kFourOverPi = 4.0 / juce::MathConstants<double>::pi;

    /** How many odd harmonics of `freq` fit below `nyquist`. */
    int oddHarmonicsBelowNyquist (double freq, double nyquist) noexcept
    {
        if (freq <= 0.0)
            return 0;

        auto count = 0;

        for (int k = 1; static_cast<double> (k) * freq < nyquist && k < 64; k += 2)
            ++count;

        return count;
    }

    /** A band-limited square from a partial's CARRIED sin and cos.

        A naive square would fold every harmonic above Nyquist back into the
        audible band, and the triângulo's partials run to 11.2 kHz where the
        third harmonic is already out of range at 48 kHz.

        Takes sin(x) and cos(x) as arguments rather than computing them: the
        caller advances them by a rotation, so no sample pays for a
        transcendental. Odd harmonics are summed by the Chebyshev recurrence
        sin((k+1)x) = 2cos(x)sin(kx) - sin((k-1)x), which needs only those two
        values however many harmonics survive.

        `harmonics` is the odd-harmonic count from trigger time. One harmonic —
        three of the five partials at 48 kHz — is exactly a sine, and skipping
        the recurrence for it is free. */
    float bandlimitedSquare (double sinX, double cosX, int harmonics) noexcept
    {
        if (harmonics <= 1)
            return static_cast<float> (sinX * kFourOverPi);

        auto previous = 0.0;    // sin(0x)
        auto current  = sinX;   // sin(1x)
        auto sum      = 0.0;

        // k runs over ODD harmonics; the recurrence steps one at a time, so the
        // loop advances twice per accumulated term.
        for (int k = 1, taken = 0; taken < harmonics; k += 2, ++taken)
        {
            sum += current / static_cast<double> (k);

            for (int step = 0; step < 2; ++step)
            {
                const auto next = 2.0 * cosX * current - previous;
                previous = current;
                current  = next;
            }
        }

        return static_cast<float> (sum * kFourOverPi);
    }

    /** Naive triangle, phase-aligned to start at zero and RISE, matching Web
        Audio's `triangle` oscillator.

        The offset is 0.75, not 0.25. At 0.25 this started at zero and then
        FELL — inverted against both Web Audio and the comment above it — so
        CX's 190 Hz body was phase-flipped relative to the sketch it was
        transcribed from and to its own noise layer. Checked: at phase 0,
        turns = 0.75, frac = 0.75, output 4·|0.25| − 1 = 0; a quarter cycle
        later frac = 0, output 4·0.5 − 1 = +1.

        Naive is defensible here where it is not for the square: a triangle's
        harmonics fall as 1/k², so the first one that folds is already 40 dB
        down, and the only voice using it sits at 190 Hz. */
    float triangleWave (double phase) noexcept
    {
        const auto turns = phase / juce::MathConstants<double>::twoPi + 0.75;
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
    // Zeroed here rather than by the caller: reset() used to need
    // `voice.clear(); voice.setSamplesUntilStart (0);` because clear() did not
    // quite clear, while the sample pool next door did the whole job with
    // `voice = {}`.
    samplesUntilStart = 0;

    for (auto& layer : layers)
    {
        layer.generator = Generator::none;
        layer.env = 0.0f;
        layer.pos = 0.0;
        layer.durationSamples = 0.0;
        layer.filter.reset();
    }
}

bool SynthVoice::trigger (int laneToPlay, float velocity, float pitchSemitones,
                          float decayPercent, juce::Random& detuneRandom) noexcept
{
    // Reports failure rather than returning silently.
    //
    // It used to just return, and the caller stamped the voice regardless — so
    // a STOLEN voice would keep sounding its previous note while carrying the
    // newest startOrder, making it the last candidate for stealing. The slot
    // would be held for the old note's full remaining duration, the new note
    // would never be heard, and no counter would record any of it. Unreachable
    // today because schedule() checks the lane first, which is exactly the
    // problem: the guard lived only in the caller.
    if (! juce::isPositiveAndBelow (laneToPlay, static_cast<int> (voiceSpecs.size())))
    {
        clear();
        return false;
    }

    const auto& spec = voiceSpecs[static_cast<size_t> (laneToPlay)];

    lane = laneToPlay;

    const auto v = juce::jlimit (0.0f, 1.0f, velocity);

    // Both from PLANNING.md's preamble to the Voice Specifications table, and
    // both defined once in Voices.h.
    const auto pitchFactor = static_cast<float> (pitchFactorForSemitones (pitchSemitones));
    const auto decayScale  = decayScaleFor (decayPercent);

    // The velocity split, where a lane has one. `>` not `>=`, matching the
    // sketch's `v > 0.55` — the boundary velocity is the CLOSED articulation.
    const auto base = (spec.openThreshold > 0.0f && v > spec.openThreshold)
                        ? spec.openDurBase
                        : spec.durBase;

    const auto durationSeconds = (base + spec.durPerVelocity * v) * decayScale;

    auto longestLayerSamples = 0.0;

    for (size_t i = 0; i < layers.size(); ++i)
    {
        auto& layer     = layers[i];
        const auto& ls  = spec.layers[i];

        layer.generator = ls.generator;
        layer.pos       = 0.0;
        layer.env       = 0.0f;
        layer.phase     = 0.0;
        layer.frequency = 0.0;
        layer.sweepRatio = 1.0;
        layer.numPartials = 0;
        layer.sinPhase = {};
        layer.cosPhase = {};
        layer.sinStep  = {};
        layer.cosStep  = {};
        layer.weight   = {};
        layer.numHarmonics = {};

        if (ls.generator == Generator::none)
        {
            layer.durationSamples = 0.0;
            continue;
        }

        const auto layerSeconds = durationSeconds * ls.durationFactor;

        layer.durationSamples = static_cast<double> (layerSeconds) * sampleRate;
        layer.attackSamples   = juce::jmin (static_cast<double> (ls.attackSeconds) * sampleRate,
                                            layer.durationSamples * 0.5);
        // Set before sweepRatio below, which is derived from it.
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

        if (ls.generator == Generator::detunedSquares)
        {
            layer.numPartials = static_cast<int> (trianguloPartials.size());

            for (size_t p = 0; p < trianguloPartials.size(); ++p)
            {
                // +/-0.5% per partial per hit. Drawn from the engine's seeded
                // generator so a render is reproducible.
                const auto detune = 1.0 + (static_cast<double> (detuneRandom.nextFloat()) - 0.5) * 0.01;
                const auto f = static_cast<double> (trianguloPartials[p] * pitchFactor) * detune;

                // The rotation step for this partial, and its harmonic count.
                // These are the only transcendentals the partial will ever
                // cost — the per-sample path advances sin and cos by rotation.
                const auto delta = juce::MathConstants<double>::twoPi * f / sampleRate;

                layer.sinPhase[p] = 0.0;
                layer.cosPhase[p] = 1.0;
                layer.sinStep[p]  = std::sin (delta);
                layer.cosStep[p]  = std::cos (delta);
                layer.numHarmonics[p] = oddHarmonicsBelowNyquist (f, nyquist);
                layer.weight[p] = static_cast<double> (trianguloPartialWeight (static_cast<int> (p)));
            }
        }
        else if (ls.generator == Generator::sweptSine || ls.generator == Generator::fixedTriangle)
        {
            layer.numPartials = 1;
            layer.frequency = static_cast<double> (ls.f0 * pitchFactor);

            const auto target = ls.f1 > 0.0f ? static_cast<double> (ls.f1 * pitchFactor)
                                             : layer.frequency;

            // A per-sample multiplier instead of a per-sample std::pow. The
            // sweep is exponential, so r^pos reproduces (f1/f0)^(pos/span)
            // exactly.
            layer.sweepRatio = (layer.sweepSamples > 0.0 && layer.frequency > 0.0
                                && std::abs (target - layer.frequency) > 1.0e-9)
                                 ? std::pow (target / layer.frequency, 1.0 / layer.sweepSamples)
                                 : 1.0;
        }

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

        longestLayerSamples = juce::jmax (longestLayerSamples, layer.durationSamples);
    }

    // A local, not a member. `durationSamples` was stored on the voice and
    // duplicated max(layer.durationSamples); its accessor had no callers and
    // claimed to exist "for the pool's sizing assertions", which used it
    // nowhere.
    active = longestLayerSamples > 0.0;

    return active;
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

float SynthVoice::Layer::nextSample (double sampleRateToUse, juce::Random& noiseSource) noexcept
{
    if (! isRunning())
        return 0.0f;

    auto raw = 0.0f;

    switch (generator)
    {
        case Generator::filteredNoise:
            raw = whiteNoise (noiseSource);
            break;

        case Generator::detunedSquares:
        {
            // No transcendental on this path at all: each partial's sin and cos
            // are advanced by a fixed rotation. Legal because these partials do
            // not sweep.
            for (int p = 0; p < numPartials; ++p)
            {
                const auto i = static_cast<size_t> (p);

                raw += bandlimitedSquare (sinPhase[i], cosPhase[i], numHarmonics[i])
                         * static_cast<float> (weight[i]);

                const auto nextSin = sinPhase[i] * cosStep[i] + cosPhase[i] * sinStep[i];
                const auto nextCos = cosPhase[i] * cosStep[i] - sinPhase[i] * sinStep[i];

                sinPhase[i] = nextSin;
                cosPhase[i] = nextCos;
            }
            break;
        }

        case Generator::sweptSine:
        case Generator::fixedTriangle:
        {
            raw = generator == Generator::sweptSine
                    ? static_cast<float> (std::sin (phase))
                    : triangleWave (phase);

            phase += juce::MathConstants<double>::twoPi * frequency / sampleRateToUse;

            if (phase > juce::MathConstants<double>::twoPi)
                phase -= juce::MathConstants<double>::twoPi;

            // Exponential in frequency over `sweepSamples`, then HOLDS — Web
            // Audio's exponentialRampToValueAtTime does not continue past its
            // target time. One multiply where this used to call std::pow.
            //
            // The sine itself cannot be rotated away like the squares': the
            // rotation increment changes as the frequency sweeps, so obtaining
            // it would cost the sine it saved.
            if (pos < sweepSamples)
                frequency *= sweepRatio;

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
        if (! layer.isRunning())
            continue;

        sum += layer.nextSample (sampleRate, noiseSource);
        anyRunning = true;
    }

    if (! anyRunning)
        active = false;

    return sum;
}

} // namespace forrobox
