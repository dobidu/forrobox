/* ============================================================================
   FORRÓ BOX — the output stage

   `PLANNING.md`'s signal chain is `each channel -> gain -> stereo pan ->
   character bus (dry + lowpass->tanh->wet) -> limiter -> master -> output`.
   VoiceEngine owns everything up to the sum; this owns the rest.

   A SIBLING of the engine rather than part of it, which overrides STATE's
   original Phase 3 design input ("a persistent VoiceEngine … owning voices,
   RNG, the jitter queue, smoothers, character bus and limiter"). The engine is
   about voices — a pool, per-voice state, per-lane humanisation keys — and this
   is one global stage with no per-voice anything. 03-02 reduced processBlock to
   a single `engine.render` call site precisely so a second stage could be added
   without being added to some exits and not others: three call sites would have
   left the ring-out after STOP unlimited and at unity master, a +3.5 dB jump on
   the most common gesture in the plugin.

   The numbers are `PLANNING.md`'s (~line 619 and ~638), cross-checked against
   `audio.js:26-130`, which settles five things the prose does not say:

     - HI-FI HALVES its own wet gain (`m * 0.5`); the other two do not. At MIX
       100 the wet path is 0.5 for HI-FI against 1.0 for the others, so HI-FI is
       the subtle character by construction rather than by taste.
     - dry is `1 - wet * 0.5`, NOT `1 - wet`. The two paths deliberately sum
       past unity, which is part of why the limiter is on by default.
     - `setTargetAtTime(target, now, 0.02)` is an exponential one-pole with a
       20 ms time constant, not a linear ramp — so juce::SmoothedValue, which
       ramps linearly over a duration, is the wrong tool.
     - the lowpass is a `createBiquadFilter()` whose Q is never set, so it is
       Web Audio's default of 1.0, not Butterworth 0.707. The resonant lift near
       cutoff is the spec; "fixing" it would be a silent deviation.
     - the limiter is made TRANSPARENT when off, never removed. PLANNING.md:
       "bypassed rather than removed, to avoid a click".
============================================================================ */
#pragma once

#include <juce_dsp/juce_dsp.h>

#include "ParameterIDs.h"

#include <array>
#include <atomic>

namespace forrobox
{

/** One timbre character. Declarative so it diffs against PLANNING.md's table
    line by line, in the `ids::timbre` choice order (HI-FI, LO-FI, CICLOTRON). */
struct TimbreSpec
{
    const char* displayName;
    float cutoffHz;
    float drive;

    /** HI-FI's `wet = m * 0.5`. The one per-timbre irregularity, and it lives
        in the table rather than as a branch inside the process loop. */
    bool halvesWet;
};

inline constexpr std::array<TimbreSpec, 3> timbreSpecs {{
    { "HI-FI",     16000.0f, 1.2f, true  },
    { "LO-FI",      5200.0f, 2.4f, false },
    { "CICLOTRON",  9000.0f, 9.0f, false },
}};

/** The lowpass Q. Web Audio's BiquadFilterNode default, which the sketch never
    overrides — so a mild resonant lift at cutoff, not Butterworth. JUCE's
    `resonance` IS Q (verified in 03-01 against juce_StateVariableTPTFilter.cpp:
    `R2 = 1 / resonance`), so this goes straight into setResonance. */
inline constexpr float kCharacterFilterQ = 1.0f;

/** `setTargetAtTime`'s time constant, in seconds. */
inline constexpr double kBusSmoothingSeconds = 0.02;

/** Limiter settings, from PLANNING.md. "Off" is threshold 0 dB and ratio 1:1 —
    the same object made transparent. */
inline constexpr float kLimiterThresholdDb = -6.0f;
inline constexpr float kLimiterRatio       = 20.0f;
inline constexpr float kLimiterAttackMs    = 2.0f;
inline constexpr float kLimiterReleaseMs   = 120.0f;

class MixBus
{
public:
    /** The four global values this stage reads. Deliberately NOT folded into
        VoiceEngine::Settings: the engine would carry values it never looks at,
        and each stage reading only what it uses is what keeps the split
        honest. */
    struct Settings
    {
        int   timbreIndex { 0 };
        float charMix { 40.0f };     ///< 0-100
        bool  limiterOn { true };
        float master { 82.0f };      ///< 0-100
    };

    /** Allocates the filter and compressor state. prepareToPlay only. */
    void prepare (double sampleRate, int maxBlockSize);

    /** Clears filter and envelope state and snaps the smoothers to their
        targets, so a render from a known state is reproducible. */
    void reset() noexcept;

    /** Processes in place. Called ONCE per block, immediately after
        engine.render, and unconditionally — see processBlock. */
    void process (juce::AudioBuffer<float>& buffer, const Settings& settings) noexcept;

    /** How much the limiter took off the last block, in dB, as a positive
        number — the meter grows right-to-left from zero.

        Published for Phase 8, which ROADMAP names as the GR meter's home, and
        deliberately an accessor with no current consumer. The project's rule is
        that a guarantee with no caller is not a guarantee, so the reason has to
        be explicit: the alternative is a limiter that computes this and throws
        it away, and Phase 8 reopening this file to get it back. A test asserts
        the value is real — non-zero under limiting, exactly zero when off —
        which makes it a measurement rather than a promise.

        It is a per-block PEAK RATIO, not the compressor's instantaneous
        internal gain the way Web Audio's `reduction` is. juce::dsp::Compressor
        exposes no gain accessor, and a meter polled once a frame cannot tell
        the difference. */
    float getGainReductionDb() const noexcept
    {
        return gainReductionDb.load (std::memory_order_relaxed);
    }

    // ── observability, for the tests ────────────────────────────────────────
    float getWetGain() const noexcept    { return wetGain; }
    float getDryGain() const noexcept    { return dryGain; }
    float getCutoffHz() const noexcept   { return cutoffHz; }
    float getMasterGain() const noexcept { return masterGain; }

    /** The wet and dry gains a timbre and mix ask for, before smoothing. Pure,
        so the spec's two formulas can be checked without rendering. */
    static float wetGainFor (int timbreIndex, float charMixPercent) noexcept;
    static float dryGainFor (int timbreIndex, float charMixPercent) noexcept;

private:
    /** One exponential step of `setTargetAtTime`. */
    float smoothTowards (float current, float target) const noexcept
    {
        return current + (target - current) * smoothingCoeff;
    }

    juce::dsp::StateVariableTPTFilter<float> characterFilter;
    juce::dsp::Compressor<float> limiter;

    // Smoothed per sample, so the result cannot depend on where a block
    // boundary happened to fall.
    float cutoffHz { timbreSpecs[0].cutoffHz };
    float wetGain { 0.0f };
    float dryGain { 1.0f };
    float masterGain { 1.0f };

    /** The cutoff last pushed into the filter. setCutoffFrequency calls
        std::tan, so it is skipped while the smoothed value is not moving —
        which is every block except the ~20 ms after a change. */
    float appliedCutoffHz { -1.0f };

    /** Set by reset(), cleared by the first process() call, which SNAPS the
        smoothers to their targets instead of ramping.

        Without it a fresh instance spends 20 ms sliding from the constructor's
        defaults toward whatever the parameters actually say — so its first
        block depends on a history it does not have, and two instances
        configured identically render differently. It also made every
        transparent-bus test inexact for its first 960 samples. */
    bool needsSnap { true };

    float smoothingCoeff { 1.0f };
    double sampleRate { 44100.0 };
    bool prepared { false };

    std::atomic<float> gainReductionDb { 0.0f };

    static_assert (std::atomic<float>::is_always_lock_free,
                   "the gain reduction is written on the audio thread");
    static_assert (timbreSpecs.size() == 3,
                   "three timbre characters, matching the ids::timbre choice list");
};

} // namespace forrobox
