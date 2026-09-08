/* ============================================================================
   FORRÓ BOX — shared test harness

   The check helpers, extracted so more than one test executable can use them.
   Deliberately still a hand-rolled harness rather than juce::UnitTest: the
   migration has been deferred twice on the grounds that a mechanical rewrite of
   working cases risks silently dropping coverage for no behavioural gain.

   State is `inline`, so two translation units may include this without
   duplicate symbols, and both then share one tally.
============================================================================ */
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <atomic>
#include <cstddef>
#include <cmath>
#include <iostream>
#include <type_traits>

namespace fbtest
{
    /** Counts global allocations. The replacement operator new/delete that feed
        it live in one translation unit (ClockTest.cpp) because a replacement
        operator must be defined exactly once in the program; the counter lives
        here so every suite can read it.

        A counter that is broken — or optimised away — reports zero for
        everything, which looks exactly like success. Whoever asserts on it must
        also prove it can register a reading. */
    // Atomic because this diff is the first to run worker threads in a suite
    // that samples the counter. Safe today only because the writer bodies
    // allocate nothing — and "safe because of what the other thread happens not
    // to do" is not a property worth relying on.
    inline std::atomic<std::size_t> allocations { 0 };

    inline int failures = 0;
    inline int checks   = 0;

    inline void check (bool condition, const juce::String& description)
    {
        ++checks;
        if (! condition)
        {
            ++failures;
            std::cout << "  FAIL  " << description << std::endl;
        }
    }

    template <typename A, typename B>
    void checkEqual (A actual, B expected, const juce::String& description)
    {
        ++checks;

        // Floating-point values compare with a tolerance; everything else exactly.
        // if constexpr keeps the numeric branch from being instantiated for
        // std::string, so one helper serves both.
        bool equal;
        if constexpr (std::is_floating_point_v<A> || std::is_floating_point_v<B>)
            equal = std::abs (static_cast<double> (actual) - static_cast<double> (expected)) < 1.0e-4;
        else
            equal = (actual == expected);

        if (! equal)
        {
            ++failures;
            std::cout << "  FAIL  " << description
                      << "  (expected " << expected << ", got " << actual << ")" << std::endl;
        }
    }

    inline void section (const juce::String& name)
    {
        std::cout << "\n[" << name << "]" << std::endl;
    }

    // ── offline audio measurement ───────────────────────────────────────────
    //
    //  Shared because 03-02 and 03-03 both need them: 03-02 measures jitter
    //  distribution (onset), velocity variation (peak, RMS) and ghost-note
    //  levels; 03-03 measures the character bus (band energy), the limiter
    //  (peak) and the master (RMS). ROADMAP names both as the next two plans.
    //
    //  There is no guaranteed audio device on WSL2, so every audio claim in
    //  this project is proved by rendering offline and measuring. "Is it
    //  silent" is not a test: a voice at the wrong frequency, one whose
    //  duration ignores DECAY, and one panned to the wrong side all pass it.

    /** Power at one frequency, by Goertzel. Cheaper than a DFT and exact at the
        bin, which is all a "does this voice sit where the spec says" question
        needs. */
    inline double goertzelPower (const float* samples, int numSamples, double freq, double sampleRate)
    {
        if (numSamples <= 0 || freq <= 0.0 || freq >= sampleRate * 0.5)
            return 0.0;

        const auto w = 2.0 * juce::MathConstants<double>::pi * freq / sampleRate;
        const auto c = 2.0 * std::cos (w);

        auto s1 = 0.0, s2 = 0.0;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto s0 = static_cast<double> (samples[i]) + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }

        return juce::jmax (0.0, s1 * s1 + s2 * s2 - c * s1 * s2);
    }

    /** Summed power over a band, sampled on a semitone grid so a wide band and
        a narrow one are comparable per-octave rather than per-Hz. */
    inline double bandEnergy (const juce::AudioBuffer<float>& buffer, double lo, double hi,
                       double sampleRate = 48000.0)
    {
        if (buffer.getNumSamples() <= 0)
            return 0.0;

        const auto* samples = buffer.getReadPointer (0);
        const auto n = buffer.getNumSamples();

        auto total = 0.0;

        for (auto f = lo; f < hi; f *= std::pow (2.0, 1.0 / 12.0))
            total += goertzelPower (samples, n, f, sampleRate);

        return total;
    }

    inline float bufferPeak (const juce::AudioBuffer<float>& buffer)
    {
        return buffer.getNumSamples() > 0 ? buffer.getMagnitude (0, buffer.getNumSamples()) : 0.0f;
    }

    inline double bufferRms (const juce::AudioBuffer<float>& buffer)
    {
        const auto n = buffer.getNumSamples();

        if (n <= 0)
            return 0.0;

        auto sum = 0.0;

        for (int c = 0; c < buffer.getNumChannels(); ++c)
        {
            const auto* samples = buffer.getReadPointer (c);

            for (int i = 0; i < n; ++i)
                sum += static_cast<double> (samples[i]) * static_cast<double> (samples[i]);
        }

        return std::sqrt (sum / static_cast<double> (n * juce::jmax (1, buffer.getNumChannels())));
    }

    /** First sample that is not exactly zero.

        The right detector for a synthetic render, and the level-relative
        `findOnset` above is the wrong one for asking WHEN a voice started: it
        answers "when did the waveform get loud", which for a 130 Hz sine is a
        quarter period later and for the zabumba's 3.6 ms sample attack is 174
        samples later. Before a voice starts, the buffer holds exact zeroes, so
        this is exact. */
    inline int firstNonZeroSample (const juce::AudioBuffer<float>& buffer)
    {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            for (int c = 0; c < buffer.getNumChannels(); ++c)
                if (! juce::exactlyEqual (buffer.getSample (c, i), 0.0f))
                    return i;

        return -1;
    }

    /** Last sample above -60 dBFS relative to the buffer's peak. */
    inline int findTailEnd (const juce::AudioBuffer<float>& buffer)
    {
        const auto peak = bufferPeak (buffer);

        if (peak <= 0.0f)
            return -1;

        // -60 dBFS relative to the buffer's own peak, said in code rather than
        // as a magic 0.001 — renderAuditionFiles in this same file already uses
        // juce::Decibels for exactly this.
        const auto threshold = peak * juce::Decibels::decibelsToGain (-60.0f);

        for (int i = buffer.getNumSamples() - 1; i >= 0; --i)
            for (int c = 0; c < buffer.getNumChannels(); ++c)
                if (std::abs (buffer.getSample (c, i)) > threshold)
                    return i;

        return -1;
    }

    /** Exactly silent, asserted exactly.

        checkEqual compares floats with a 1.0e-4 tolerance, which is the right
        default for a measured level and the wrong one for this: a mute leaking
        at -80 dBFS would pass it. Mute and velocity 0 claim exact zero, so
        exact zero is what gets checked. */
    inline void checkSilent (const juce::AudioBuffer<float>& buffer, const juce::String& description)
    {
        const auto peak = bufferPeak (buffer);

        check (! (peak > 0.0f),
               description + (peak > 0.0f ? juce::String (" (leaked peak ") + juce::String (peak, 9) + ")"
                                          : juce::String()));
    }

    inline bool isFinite (const juce::AudioBuffer<float>& buffer)
    {
        for (int c = 0; c < buffer.getNumChannels(); ++c)
        {
            const auto* samples = buffer.getReadPointer (c);

            for (int i = 0; i < buffer.getNumSamples(); ++i)
                if (! std::isfinite (samples[i]))
                    return false;
        }

        return true;
    }


    /** Proves the allocation counter can register a reading.

        A counter that is broken — or optimised away — reports zero for
        everything, which looks exactly like success. A negative control that
        allocated inside Clock::advance was MISSED for exactly that reason: the
        compiler elided its new/delete pair, so nothing was counted. The escape
        through a volatile pointer is what makes the allocation unelidable.

        Lives here, beside the counter it validates, because a second copy of it
        in the voice suite got BOTH halves wrong: it dropped the volatile escape,
        and it put a check() call — which constructs a juce::String, and
        juce::String has no small-string optimisation, so a literal always
        allocates through the same replaced operator new[] — INSIDE the measured
        window. The assertion therefore passed on its own description string
        whether or not the probe allocation survived optimisation. That is the
        seventh assertion in this project that could not fail, and it was the one
        guarding the instrument the others depend on. */
    inline void checkAllocationCounterRegisters()
    {
        static double* volatile sink = nullptr;

        const auto before = allocations.load (std::memory_order_relaxed);

        sink = new double (1.0);
        delete sink;
        sink = nullptr;

        const auto after = allocations.load (std::memory_order_relaxed);

        // No check() between the two loads: constructing its juce::String would
        // allocate and make this pass regardless.
        check (after > before,
               "the allocation counter registers a real allocation (it is not stuck at zero)");
    }

    /** Prints the tally and returns the process exit code. The wording is load
        bearing: the build scripts and plan verification steps grep for
        "N / N checks passed". */
    inline int reportSummary()
    {
        std::cout << "\n" << (checks - failures) << " / " << checks << " checks passed";
        std::cout << (failures == 0 ? "  — OK" : "  — FAILURES") << std::endl;
        return failures == 0 ? 0 : 1;
    }
}
