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

#include "Humanisation.h"
#include "ParameterIDs.h"

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
    //  The oscillator ALWAYS tracks PITCH; the filter's tracking varies per
    //  voice, which is the whole point of the note at the top of this file. A
    //  `pitchTracksOscillator` flag was here too and was `true` in every row —
    //  documentation posing as behaviour.
    float f0 { 0.0f };                  ///< start frequency, Hz
    float f1 { 0.0f };                  ///< sweep target; 0 means no sweep
    float sweepFraction { 1.0f };       ///< fraction of the layer's duration the sweep spans

    // ── filter ──────────────────────────────────────────────────────────────
    FilterKind filter { FilterKind::none };
    float filterFreq { 0.0f };
    float filterQ { 1.0f };
    bool  pitchTracksFilter { false };

    // ── envelope ────────────────────────────────────────────────────────────
    //  A `fixedDurationSeconds` override lived here for the zabumba's 20 ms
    //  beater click. The zabumba is sampled now, so it was 0 in all nine rows
    //  and its branch was permanently dead.
    float peakPerVelocity { 0.0f };     ///< peak = peakPerVelocity * v
    float durationFactor { 1.0f };      ///< duration = voice duration * this
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
        { Generator::detunedSquares, 0.0f, 0.0f, 1.0f,
          FilterKind::bandpass, 7600.0f, 0.7f, false,
          0.5f, 1.0f, 0.001f },
        {}
    }}},

    // ── 2. pandeiro — membrane + jingles ────────────────────────────────────
    { false, 0.10f, 0.10f, 0.0f, 0.0f, {{
        { Generator::sweptSine, 330.0f, 180.0f, 0.7f,
          FilterKind::none, 0.0f, 1.0f, false,
          0.55f, 1.0f, 0.001f },
        { Generator::filteredNoise, 0.0f, 0.0f, 1.0f,
          FilterKind::highpass, 6500.0f, 1.0f, false,
          0.40f, 0.9f, 0.001f }
    }}},

    // ── 3. ganzá — narrow band of noise. 4 ms attack, the longest here ──────
    { false, 0.035f, 0.03f, 0.0f, 0.0f, {{
        { Generator::filteredNoise, 0.0f, 0.0f, 1.0f,
          FilterKind::bandpass, 6800.0f, 1.2f, true,
          0.6f, 1.0f, 0.004f },
        {}
    }}},

    // ── 4. bateria BB — bumbo ───────────────────────────────────────────────
    { false, 0.14f, 0.0f, 0.0f, 0.0f, {{
        { Generator::sweptSine, 130.0f, 48.0f, 0.6f,
          FilterKind::none, 0.0f, 1.0f, false,
          1.0f, 1.0f, 0.001f },
        {}
    }}},

    // ── 5. bateria CX — caixa: rattle + body ────────────────────────────────
    { false, 0.14f, 0.0f, 0.0f, 0.0f, {{
        { Generator::filteredNoise, 0.0f, 0.0f, 1.0f,
          FilterKind::highpass, 1700.0f, 1.0f, false,
          0.6f, 1.0f, 0.001f },
        { Generator::fixedTriangle, 190.0f, 0.0f, 1.0f,
          FilterKind::none, 0.0f, 1.0f, false,
          0.4f, 0.7f, 0.001f }
    }}},

    // ── 6. bateria HH — chimbal ─────────────────────────────────────────────
    { false, 0.03f, 0.04f, 0.0f, 0.0f, {{
        { Generator::filteredNoise, 0.0f, 0.0f, 1.0f,
          FilterKind::highpass, 9000.0f, 1.0f, false,
          0.45f, 1.0f, 0.001f },
        {}
    }}},

    // ── 7. bateria TOM ──────────────────────────────────────────────────────
    { false, 0.2f, 0.0f, 0.0f, 0.0f, {{
        { Generator::sweptSine, 190.0f, 110.0f, 0.7f,
          FilterKind::none, 0.0f, 1.0f, false,
          0.8f, 1.0f, 0.001f },
        {}
    }}},
}};

static_assert (voiceSpecs.size() == static_cast<size_t> (State::kNumLanes),
               "one voice recipe per sequencer lane");

/** The envelope's terminal value, from `audio.js`'s
    `exponentialRampToValueAtTime(0.0001, t + dur)`. Absolute, not relative to
    the peak — which is why a hit quieter than this is simply silent. */
inline constexpr float kEnvelopeFloor = 0.0001f;

/** `decayScale = 0.4 + (decay/100) x 1.4`, from PLANNING.md's preamble to the
    Voice Specifications, and its own maximum.

    One definition, because this was written out three times — in Voices.cpp, in
    VoiceEngine.cpp, and in the test, where the copy divided by a literal 100.0
    instead of ids::kPercentMax. That is the identical drift that halved every
    pan earlier in this plan, and `1.8` was separately re-derived by hand in
    VoiceEngine.cpp as "the formula's maximum".

    `inline`, not `inline constexpr`: juce::jlimit is not constexpr in JUCE 8
    (juce_MathsFunctions.h), unlike jmin/jmax. */
inline constexpr float kDecayScaleMin  = 0.4f;
inline constexpr float kDecayScaleSpan = 1.4f;
inline constexpr float kDecayScaleMax  = kDecayScaleMin + kDecayScaleSpan;

inline float decayScaleFor (float decayPercent) noexcept
{
    return kDecayScaleMin
         + juce::jlimit (0.0f, ids::kPercentMax, decayPercent) / ids::kPercentMax * kDecayScaleSpan;
}

/** Semitones to a frequency/rate multiplier. Written out in both Voices.cpp
    and VoiceEngine.cpp; JUCE has no helper for it (checked). */
inline double pitchFactorForSemitones (double semitones) noexcept
{
    return std::pow (2.0, semitones / 12.0);
}


/** A single sounding synthesised note. Fixed size, no allocation after
    `prepare`; `trigger` reconfigures it in place. */
class SynthVoice
{
public:
    /** Allocates the filter state. Message/prepare thread only. */
    void prepare (double sampleRate);

    /** Reconfigures this voice to sound `lane` at `velocity`. Audio thread.

        Returns false, and leaves the voice cleared, if it could not be
        configured. The caller must not stamp a voice it failed to trigger:
        doing so left a stolen voice sounding its old note behind the newest
        stealing order, so the slot was pinned and the new note lost silently.

        `seed` and `step` KEY the triângulo's per-partial detune rather than
        drawing it from a stream. It used to take a `juce::Random&`, and drew
        from it five times — for one lane only, only after the caller's
        audibility gate, and only for a voice that was actually claimed. That
        made the shared stream's position depend on the pattern and the mute
        state, so muting the triângulo re-levelled every other channel. Keyed,
        each value is a function of its key and cannot depend on any of that. */
    [[nodiscard]] bool trigger (int lane, float velocity, float pitchSemitones, float decayPercent,
                                std::uint64_t seed, std::uint64_t step) noexcept;

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

private:
    struct Layer
    {
        Generator generator { Generator::none };

        // ── single-oscillator generators (sweptSine, fixedTriangle) ─────────
        //  A plain phase accumulator. `frequency` is advanced by `sweepRatio`
        //  each sample while the sweep runs, rather than recomputing
        //  f0 * (f1/f0)^(pos/sweepSamples) — which called std::pow on EVERY
        //  sample. Measured: 6.64 ns for the pow against 0.43 ns for the
        //  multiply, worth 6.2 ns/sample on each of the three swept lanes
        //  (bb 17.5 -> 9.7 ns, tom 17.6 -> 9.7, pandeiro 21.1 -> 13.4).
        //  Identical curve: f0 * r^pos with r = (f1/f0)^(1/sweepSamples), and
        //  drift over the longest sweep (12 096 samples) is ~1e-12 relative.
        double phase { 0.0 };
        double frequency { 0.0 };
        double sweepRatio { 1.0 };
        double sweepSamples { 0.0 };

        // ── detunedSquares (the triângulo) ──────────────────────────────────
        //  Each partial's sin and cos are CARRIED FORWARD by a rotation rather
        //  than recomputed, so a sample costs no transcendental at all:
        //      s' = s*cosStep + c*sinStep
        //      c' = c*cosStep - s*sinStep
        //  Legal because these partials never sweep — their frequency is fixed
        //  at trigger — which is what makes a constant rotation step correct.
        //
        //  This was 74% of the whole engine's render cost. Measured:
        //  107.7 ns/sample for the triângulo against 5.5-21.1 for every other
        //  lane, and 65.9% of a 512-sample budget with its pool saturated.
        //  After: 10.9 ns/sample (9.9x), and 7.1% saturated. The harmonic sum
        //  is bit-identical over 500k evaluations; a one-second render differs
        //  in 39 of 48 000 samples by at most 5.8e-11, i.e. -184.6 dB.
        int numPartials { 0 };
        std::array<double, 5> sinPhase {}, cosPhase {}, sinStep {}, cosStep {}, weight {};

        /** Odd harmonics of this partial that fit below Nyquist, counted at
            trigger. At 48 kHz the five partials give 2/2/1/1/1, so three of
            them reduce to a plain sine and skip the recurrence entirely. */
        std::array<int, 5> numHarmonics {};

        bool hasFilter { false };
        juce::dsp::StateVariableTPTFilter<float> filter;

        double durationSamples { 0.0 };
        double attackSamples { 0.0 };
        float  peak { 0.0f };
        float  env { 0.0f };
        float  envDecayMultiplier { 0.0f };
        double pos { 0.0 };

        bool isRunning() const noexcept
        {
            return generator != Generator::none && pos < durationSamples;
        }

        float nextSample (double sampleRate, juce::Random& noiseSource) noexcept;
        void  advanceEnvelope() noexcept;
    };

    std::array<Layer, 2> layers;
    bool   active { false };
    int    lane { 0 };
    double sampleRate { 44100.0 };
    double nyquist { 22050.0 };
    std::uint64_t startOrder { 0 };
    int    samplesUntilStart { 0 };
};

} // namespace forrobox
