/* ============================================================================
   FORRÓ BOX — the synthesised percussion voices

   One declarative recipe per lane, transcribed from PLANNING.md's Voice
   Specifications table and cross-checked against `audio.js`'s `trigger()`.
   Declarative on purpose: a reviewer can diff `voiceSpecs` against the spec
   line by line, which is not true of eight hand-written render functions.

   Where the spec table and the sketch disagree, the sketch wins and the
   difference is noted at the recipe — the table is a summary and drops detail:

     - the triângulo's outer envelope peak is `0.5v`; the table gives only the
       0.12/(i+1) partial weights and omits the velocity factor entirely
     - the envelope decays to 0.0001 absolute (-80 dB), not 0.001
     - the zabumba's beater click has a FIXED 20 ms envelope, not `dur`
     - `pitchFactor` is applied PER COMPONENT and inconsistently: ganzá's
       bandpass tracks pitch, the triângulo's partials track it but its bandpass
       does not, and CX's, HH's and pandeiro's noise filters are all fixed. A
       global pitch factor would be wrong for five of the eight lanes.

   Zabumba is NOT here. It plays the embedded one-shots (see ZabumbaSampler);
   its synthesis recipe stays in PLANNING.md should that decision be reversed.
============================================================================ */
#pragma once

#include <juce_dsp/juce_dsp.h>

#include "ForroBoxState.h"

#include <array>
#include <cmath>
#include <cstdint>

namespace forrobox
{

/** What produces a layer's raw signal, before its filter and envelope. */
enum class Generator
{
    none,
    sweptSine,        ///< sine, frequency ramped exponentially f0 -> f1
    fixedTriangle,    ///< triangle at a fixed frequency
    filteredNoise,    ///< white noise (the filter is what shapes it)
    detunedSquares    ///< the triângulo's five partials, each randomly detuned
};

enum class FilterKind { none, lowpass, bandpass, highpass };

/** The triângulo's five partials, verbatim from the spec. Randomly detuned by
    +/-0.5% per hit, which is what stops repeated strikes sounding identical. */
inline constexpr std::array<float, 5> trianguloPartials { 5400.0f, 6850.0f, 8120.0f, 9700.0f, 11200.0f };

/** Relative weight of partial i: 0.12 / (i + 1). The absolute level comes from
    the layer's `peakPerVelocity`, applied after the bandpass. */
inline constexpr float trianguloPartialWeight (int i) noexcept { return 0.12f / static_cast<float> (i + 1); }

/** One generator plus its filter and its own amplitude envelope.

    A voice has up to two, because five of the eight lanes are two things at
    once — a membrane and jingles, a body and a snare rattle, a tone and a
    beater click — and each half has its own peak and its own duration. */
struct LayerSpec
{
    Generator generator { Generator::none };

    // ── frequency ───────────────────────────────────────────────────────────
    float f0 { 0.0f };                  ///< start frequency, Hz
    float f1 { 0.0f };                  ///< sweep target; 0 means no sweep
    float sweepFraction { 1.0f };       ///< fraction of the layer's duration the sweep spans
    bool  pitchTracksOscillator { true };

    // ── filter ──────────────────────────────────────────────────────────────
    FilterKind filter { FilterKind::none };
    float filterFreq { 0.0f };
    float filterQ { 1.0f };
    bool  pitchTracksFilter { false };

    // ── envelope ────────────────────────────────────────────────────────────
    float peakPerVelocity { 0.0f };     ///< peak = peakPerVelocity * v
    float durationFactor { 1.0f };      ///< duration = voice duration * this
    float fixedDurationSeconds { 0.0f };///< if > 0, replaces the above outright
    float attackSeconds { 0.001f };
};

/** One lane's complete recipe. */
struct VoiceSpec
{
    bool usesSample { false };          ///< zabumba: the sampler renders it, not this

    // dur = (durBase + durPerVelocity * v) * decayScale
    float durBase { 0.0f };
    float durPerVelocity { 0.0f };

    /** The triângulo's velocity-split articulation, which PLANNING.md calls
        "the instrument's defining behaviour". Above the threshold the note
        rings; below it, it is damped. 0 disables the split. */
    float openThreshold { 0.0f };
    float openDurBase { 0.0f };

    std::array<LayerSpec, 2> layers {};
};

/** Indexed by lane, in `ids::lanes` order:
    zabumba, triangulo, pandeiro, ganza, bb, cx, hh, tom. */
inline constexpr std::array<VoiceSpec, 8> voiceSpecs {{
    // ── 0. zabumba — sampled, see ZabumbaSampler ────────────────────────────
    { true, 0.0f, 0.0f, 0.0f, 0.0f, {} },

    // ── 1. triângulo — five detuned squares through a bandpass ──────────────
    //  dur = (v > 0.55 ? 0.45 : 0.06) * decayScale
    { false, 0.06f, 0.0f, 0.55f, 0.45f, {{
        { Generator::detunedSquares, 0.0f, 0.0f, 1.0f, true,
          FilterKind::bandpass, 7600.0f, 0.7f, false,
          0.5f, 1.0f, 0.0f, 0.001f },
        {}
    }}},

    // ── 2. pandeiro — membrane + jingles ────────────────────────────────────
    { false, 0.10f, 0.10f, 0.0f, 0.0f, {{
        { Generator::sweptSine, 330.0f, 180.0f, 0.7f, true,
          FilterKind::none, 0.0f, 1.0f, false,
          0.55f, 1.0f, 0.0f, 0.001f },
        { Generator::filteredNoise, 0.0f, 0.0f, 1.0f, true,
          FilterKind::highpass, 6500.0f, 1.0f, false,
          0.40f, 0.9f, 0.0f, 0.001f }
    }}},

    // ── 3. ganzá — narrow band of noise. 4 ms attack, the longest here ──────
    { false, 0.035f, 0.03f, 0.0f, 0.0f, {{
        { Generator::filteredNoise, 0.0f, 0.0f, 1.0f, true,
          FilterKind::bandpass, 6800.0f, 1.2f, true,
          0.6f, 1.0f, 0.0f, 0.004f },
        {}
    }}},

    // ── 4. bateria BB — bumbo ───────────────────────────────────────────────
    { false, 0.14f, 0.0f, 0.0f, 0.0f, {{
        { Generator::sweptSine, 130.0f, 48.0f, 0.6f, true,
          FilterKind::none, 0.0f, 1.0f, false,
          1.0f, 1.0f, 0.0f, 0.001f },
        {}
    }}},

    // ── 5. bateria CX — caixa: rattle + body ────────────────────────────────
    { false, 0.14f, 0.0f, 0.0f, 0.0f, {{
        { Generator::filteredNoise, 0.0f, 0.0f, 1.0f, true,
          FilterKind::highpass, 1700.0f, 1.0f, false,
          0.6f, 1.0f, 0.0f, 0.001f },
        { Generator::fixedTriangle, 190.0f, 0.0f, 1.0f, true,
          FilterKind::none, 0.0f, 1.0f, false,
          0.4f, 0.7f, 0.0f, 0.001f }
    }}},

    // ── 6. bateria HH — chimbal ─────────────────────────────────────────────
    { false, 0.03f, 0.04f, 0.0f, 0.0f, {{
        { Generator::filteredNoise, 0.0f, 0.0f, 1.0f, true,
          FilterKind::highpass, 9000.0f, 1.0f, false,
          0.45f, 1.0f, 0.0f, 0.001f },
        {}
    }}},

    // ── 7. bateria TOM ──────────────────────────────────────────────────────
    { false, 0.2f, 0.0f, 0.0f, 0.0f, {{
        { Generator::sweptSine, 190.0f, 110.0f, 0.7f, true,
          FilterKind::none, 0.0f, 1.0f, false,
          0.8f, 1.0f, 0.0f, 0.001f },
        {}
    }}},
}};

static_assert (voiceSpecs.size() == static_cast<size_t> (State::kNumLanes),
               "one voice recipe per sequencer lane");

/** The envelope's terminal value, from `audio.js`'s
    `exponentialRampToValueAtTime(0.0001, t + dur)`. Absolute, not relative to
    the peak — which is why a hit quieter than this is simply silent. */
inline constexpr float kEnvelopeFloor = 0.0001f;

/** A single sounding synthesised note. Fixed size, no allocation after
    `prepare`; `trigger` reconfigures it in place. */
class SynthVoice
{
public:
    /** Allocates the filter state. Message/prepare thread only. */
    void prepare (double sampleRate);

    /** Reconfigures this voice to sound `lane` at `velocity`. Audio thread.

        `detuneRandom` supplies the triângulo's per-partial detune. It is passed
        in rather than drawn here so all randomness in the engine comes from one
        seeded generator, which is what makes a render reproducible. */
    void trigger (int lane, float velocity, float pitchSemitones, float decayPercent,
                  juce::Random& detuneRandom) noexcept;

    /** One mono sample, advancing the voice. Returns 0 and clears `active` once
        the longest layer has run out. */
    float nextSample (juce::Random& noiseSource) noexcept;

    bool isActive() const noexcept { return active; }
    int  getLane()  const noexcept { return lane; }
    void clear() noexcept;

    /** Monotonic at trigger time, so the pool can steal the oldest. */
    std::uint64_t getStartOrder() const noexcept { return startOrder; }
    void setStartOrder (std::uint64_t order) noexcept { startOrder = order; }

    /** Samples still to elapse before this voice sounds.

        Lives on the voice rather than in a parallel array beside the pool:
        two containers indexed in step are two representations of one fact, and
        their disagreement is what made a duplicate filter seem necessary in
        02-03. Values beyond the current block are carried forward, which is
        what 03-02's timing jitter needs to place a hit past the block end. */
    int  getSamplesUntilStart() const noexcept { return samplesUntilStart; }
    void setSamplesUntilStart (int samples) noexcept { samplesUntilStart = samples; }
    void advanceStart (int samples) noexcept { samplesUntilStart -= samples; }

    /** Whole-voice length in samples, for the pool's sizing assertions. */
    double getDurationSamples() const noexcept { return durationSamples; }

private:
    struct Layer
    {
        Generator generator { Generator::none };

        // Up to five partials; single-oscillator generators use index 0 only.
        int numPartials { 0 };
        std::array<double, 5> f0 {}, f1 {}, phase {}, weight {};
        /** Whether f1 differs from f0 at all. Decided once at trigger rather
            than by comparing the two floats every sample, which is both faster
            and not a float equality test. */
        bool sweeps { false };

        bool hasFilter { false };
        juce::dsp::StateVariableTPTFilter<float> filter;

        double sweepSamples { 0.0 };
        double durationSamples { 0.0 };
        double attackSamples { 0.0 };
        float  peak { 0.0f };
        float  env { 0.0f };
        float  envDecayMultiplier { 0.0f };
        double pos { 0.0 };

        float nextSample (double sampleRate, double nyquist, juce::Random& noiseSource) noexcept;
        void  advanceEnvelope() noexcept;
    };

    std::array<Layer, 2> layers;
    bool   active { false };
    int    lane { 0 };
    double durationSamples { 0.0 };
    double sampleRate { 44100.0 };
    double nyquist { 22050.0 };
    std::uint64_t startOrder { 0 };
    int    samplesUntilStart { 0 };
};

} // namespace forrobox
