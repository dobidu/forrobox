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
    /** What this stage costs the host's timeline: nothing, on both counts.

        Named rather than assumed, because STATE recorded it as a consequence of
        the engine/bus split that had to be settled deliberately — and it was
        settled by silence: the processor's two host-facing figures are still
        the engine's alone, and they are correct by coincidence rather than by
        construction.

        Verified in the JUCE source rather than assumed.
        `juce::dsp::StateVariableTPTFilter` holds only two state variables and is
        topology-preserving, so zero latency by construction; and
        `juce::dsp::Compressor::processSample` is a one-pole ballistics envelope
        times the input — feedforward, no lookahead, no delay line. With zero
        input the compressor outputs zero and the lowpass rings for well under a
        millisecond at 5.2 kHz and Q 1.

        The processor now writes both overrides as chain SUMS, so anything a
        later phase adds here — a lookahead limiter, oversampling, the deferred
        "LOAD IR..." convolution — has a structural reminder attached. */
    static constexpr int    kLatencySamples = 0;
    static constexpr double kTailSeconds    = 0.0;

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

    /** How much the limiter took off since this was last called, in dB, as a
        positive number — the meter grows right-to-left from zero.

        TAKEN, not read: it returns the maximum since the previous call and
        resets to zero, so a caller polling slower than the audio thread cannot
        miss a peak. Phase 8's meter runs on a 60 fps UI timer, which at
        512/48 kHz sees 64% of blocks and at a 64-sample buffer only 8% — a
        last-block value would under-read and flicker. Phase 5 already set this
        convention for the activity LEDs: the audio thread publishes the maximum
        and the UI applies its own 0.82/frame decay.

        The first version reported the LAST block, and the first caller got it
        wrong: a test read it once after a 98304-sample render and saw 0.00 dB
        for a limiter that had been working throughout. That was recorded as the
        test's mistake; it was evidence about the accessor.

        Published for Phase 8, which ROADMAP names as the GR meter's home, and
        deliberately an accessor with no current consumer. The project's rule is
        that a guarantee with no caller is not a guarantee, so the reason has to
        be explicit: the alternative is a limiter that computes this and throws
        it away, and Phase 8 reopening this file to get it back. A test asserts
        the value is real — non-zero under limiting, exactly zero when off —
        which makes it a measurement rather than a promise.

        It is the INSTANTANEOUS gain the limiter applied, which is the same
        quantity as the prototype's `-limiterNode.reduction`.
        juce::dsp::Compressor exposes no gain accessor — but `processSample`
        returns `gain * input`, so the gain is recoverable as the ratio, and the
        minimum over a block is the peak reduction.

        The first version used a per-block PEAK RATIO instead, `max|in| /
        max|out|`, whose numerator and denominator can come from different
        samples: a systematic lower bound, decoupled from the release envelope,
        which reads near zero on a block holding both a pre-attack transient and
        heavily limited material. The objection to the ratio form was that it
        divides by zero between hits — handled by the same epsilon the peak form
        already needed. */
    float takeGainReductionDb() const noexcept
    {
        return gainReductionDb.exchange (0.0f, std::memory_order_relaxed);
    }

    /** The wet gain as of the end of the last block.

        An ATOMIC snapshot, not a raw read of the member. The members are
        mutated per sample on the audio thread and this class is reachable from
        the message thread through `PluginProcessor::getMixBus()`, so a plain
        getter is the data race VoiceEngine spends three static_asserts ruling
        out — and it was one, for three accessors, of which this is the only one
        anything called. `getDryGain`, `getCutoffHz` and `getMasterGain` were
        deleted rather than made safe: dry is a pure function of wet, and master
        a pure function of its parameter, so the UI can compute both. */
    float getWetGain() const noexcept { return publishedWetGain.load (std::memory_order_relaxed); }

    /** The wet and dry gains a timbre and mix ask for, before smoothing. Pure,
        so the spec's two formulas can be checked without rendering. */
    static float wetGainFor (int timbreIndex, float charMixPercent) noexcept;
    static float dryGainFor (int timbreIndex, float charMixPercent) noexcept;

    /** `dry = 1 - 0.5 * wet` — the spec's law, NOT `1 - wet`; the two paths
        deliberately sum past unity.

        Here because `process` needs it per sample against the SMOOTHED wet,
        which `dryGainFor` (a function of the parameters) cannot supply. It was
        written out at both sites, under a comment claiming that expressing it
        in `process` "enforces it" — it did the opposite: a negative control
        that changed the per-sample copy to `1 - wet` passed all 1090 checks,
        because the only test of the law goes through `dryGainFor`. Now the
        copies are one function and that control fails. */
    static constexpr float dryForWet (float wet) noexcept { return 1.0f - 0.5f * wet; }

    /** `gain = (value/100)^2` — PLANNING.md's perceptual taper, in one place
        rather than in `process` and again in `reset`. */
    static float masterGainFor (float masterPercent) noexcept;

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
    //
    // `drive` is smoothed too, and the first version did not smooth it. The
    // argument was that the gains either side would mask a step in the curve —
    // true only when the wet gain moves far, and false for the transitions that
    // exist. PETROLINA is the only LO-FI profile, so switching to or from it is
    // LO-FI <-> HI-FI: at the default MIX 40 the wet gain moves 0.40 -> 0.20
    // while the drive steps 2.4 -> 1.2, a -2.8 dB single-sample jump. LO-FI ->
    // CICLOTRON is worse: wet and dry targets are IDENTICAL (1.0 / 0.5), so
    // nothing smooths at all while the drive jumps 2.4 -> 9.0. Phase 6's goal
    // is a full state reload "without a click", and the profile list contains
    // exactly that pair.
    //
    // There is NO dryGain member: dry is exactly `1 - 0.5 * wet`, the map is
    // affine, and the smoother preserves an affine relation — so deriving it
    // per sample enforces the spec formula instead of keeping two smoothers
    // consistent by hand.
    float cutoffHz { timbreSpecs[0].cutoffHz };
    float wetGain { 0.0f };
    float drive { timbreSpecs[0].drive };
    float masterGain { 1.0f };

    /** The cutoff last pushed into the filter. setCutoffFrequency calls
        std::tan, so it is skipped while the smoothed value is not moving.

        Measured: with the guard removed the loop costs 24.47 us/block against
        13.32 — the guard is 46% of the bus, worth 11.16 us/block, and the
        biggest saving in the file. It is open for 38.6-59.5 ms after a change,
        not the "~20 ms" a first version of this comment claimed: 20 ms is ONE
        time constant and reaching the 1e-4 relative threshold takes about nine.
        Starts at 0, so the first comparison always fires without relying on a
        negative tolerance. */
    float appliedCutoffHz { 0.0f };

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

    /** Mutable because taking the value resets it — see takeGainReductionDb. */
    mutable std::atomic<float> gainReductionDb { 0.0f };

    /** Published once per block for the message thread. */
    std::atomic<float> publishedWetGain { 0.0f };

    static_assert (std::atomic<float>::is_always_lock_free,
                   "the published figures are written on the audio thread");
    static_assert (timbreSpecs.size() == 3,
                   "three timbre characters, matching the ids::timbre choice list");
};

} // namespace forrobox
