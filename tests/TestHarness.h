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
#include <limits>
#include <vector>
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

    // ── message literals are UTF-8, and this is the only place that knows ────
    //
    // `juce::String (const char*)` reads its bytes as LATIN-1
    // (`juce_String.cpp:308`, whose own comment recommends
    // `String (CharPointer_UTF8 (...))`). 228 message literals in UiTest.cpp
    // carry an em dash or an accent, so every one of them printed as mojibake —
    // at exactly the moment a check fails and someone is reading it. 06-04 hit
    // it in its own new message and fixed that one by hand; 06-05's review
    // found the other 227.
    //
    // OVERLOADS, not 228 edits. The harness is the one place that can be wrong
    // about this, so it is the one place that has to be right — and a
    // `const char*` argument binds to these in preference to the `juce::String`
    // conversion, so no call site changes. /simplify.
    inline juce::String utf8 (const char* text) { return juce::String::fromUTF8 (text); }

    inline void check (bool condition, const char* description)
    {
        check (condition, utf8 (description));
    }

    template <typename A, typename B>
    void checkEqual (A actual, B expected, const char* description)
    {
        checkEqual (actual, expected, utf8 (description));
    }

    inline void section (const char* name) { section (utf8 (name)); }

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
        a narrow one are comparable per-octave rather than per-Hz.

        FOR BROADBAND CONTENT. The grid steps by 2^(1/12), so it can miss a pure
        tone entirely: `bandEnergy (900, 1100)` samples 900, 953.5, 1010.2 and
        1070.3, and never 1000 Hz. A 1 kHz sine therefore reads as leakage
        rather than as its own power, and the answer depends on where the grid
        happens to land. This surfaced when the instruments were given
        self-tests — the percussion voices this was written for are broadband or
        sweeping, so it had never mattered.

        For a single known frequency, call goertzelPower directly. */
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

    /** The note's own length in samples: last audible sample minus first
        non-zero one.

        Measured from the ONSET, not from sample 0. Those were the same thing
        until 03-02 delayed every trigger by a 32 ms lookahead, at which point
        every duration read 32 ms too long and every DECAY ratio compressed
        toward 1 — a 4.5x expectation measured 2.96x. Relative to the onset it
        is correct either way. */
    inline int renderedNoteLength (const juce::AudioBuffer<float>& buffer)
    {
        const auto start = firstNonZeroSample (buffer);
        const auto end   = findTailEnd (buffer);

        return (start < 0 || end < start) ? -1 : end - start;
    }

    /** Where each of `count` hits actually landed, relative to where it was
        expected, in samples.

        For measuring a distribution rather than one outcome. Each hit is
        searched for in its own window of +/-`searchRadius` around
        `firstExpected + i * spacing`, so the windows must not overlap — pick a
        lane whose voice is shorter than the spacing, and a radius under half of
        it. A hit that cannot be found in its window reports `notFound`.

        Allocates, so it belongs to tests and never to the audio thread. */
    inline constexpr int notFound = std::numeric_limits<int>::min();

    inline std::vector<int> measureHitDisplacements (const juce::AudioBuffer<float>& buffer,
                                                     double firstExpected,
                                                     double spacing,
                                                     int count,
                                                     int searchRadius,
                                                     float thresholdFraction = 0.0f)
    {
        std::vector<int> displacements;
        displacements.reserve (static_cast<size_t> (juce::jmax (0, count)));

        const auto threshold = bufferPeak (buffer) * thresholdFraction;

        for (int i = 0; i < count; ++i)
        {
            const auto centre = static_cast<int> (firstExpected + spacing * static_cast<double> (i));
            const auto from = juce::jmax (0, centre - searchRadius);
            const auto to   = juce::jmin (buffer.getNumSamples(), centre + searchRadius);

            auto found = notFound;

            for (int s = from; s < to && found == notFound; ++s)
                for (int c = 0; c < buffer.getNumChannels(); ++c)
                    // `> threshold` already implies non-zero for any
                    // threshold >= 0, including 0 — an explicit exactlyEqual
                    // test alongside it was redundant.
                    if (std::abs (buffer.getSample (c, s)) > threshold)
                    {
                        found = s - centre;
                        break;
                    }

            displacements.push_back (found);
        }

        return displacements;
    }

    /** A copy of `buffer` with everything below `cutoff` removed, by two
        cascaded one-pole highpasses.

        For measuring ONE lane's onsets in a render that contains others. Onset
        detection cannot separate summed voices, so a neighbouring lane's early
        ghost becomes the first non-zero sample in the window and is reported as
        this lane's hit — which is how a mute-retiming test came to fail against
        correct code. Two one-poles rather than a biquad because the job only
        needs an octave of rejection and there is then no resonance to reason
        about.

        Attenuates; it does not erase. A loud low voice still leaves a residual
        far above zero, so measure the result with a peak-relative threshold
        rather than with exact-zero detection. */
    inline juce::AudioBuffer<float> highpassed (const juce::AudioBuffer<float>& buffer,
                                                double cutoff, double sampleRate = 48000.0)
    {
        // Not cleared: every sample is written by the loop below.
        juce::AudioBuffer<float> out (buffer.getNumChannels(), buffer.getNumSamples());

        const auto a = std::exp (-2.0 * juce::MathConstants<double>::pi * cutoff / sampleRate);

        for (int c = 0; c < buffer.getNumChannels(); ++c)
        {
            const auto* in = buffer.getReadPointer (c);
            auto* dest = out.getWritePointer (c);

            double previousIn[2] { 0.0, 0.0 };
            double previousOut[2] { 0.0, 0.0 };

            for (int s = 0; s < buffer.getNumSamples(); ++s)
            {
                auto value = static_cast<double> (in[s]);

                for (int stage = 0; stage < 2; ++stage)
                {
                    const auto filtered = a * (previousOut[stage] + value - previousIn[stage]);
                    previousIn[stage] = value;
                    previousOut[stage] = filtered;
                    value = filtered;
                }

                dest[s] = static_cast<float> (value);
            }
        }

        return out;
    }

    /** How many separate onsets there are in `[from, to)`.

        An onset is a non-zero sample preceded by at least `minSilence`
        consecutive EXACT zeros. Within a sounding voice exact zeros are
        isolated single samples, so this counts voices that start at different
        times rather than zero crossings.

        For asking "did these two lanes start together?" when they are summed
        into one buffer and cannot be separated by onset detection alone. */
    inline int countOnsets (const juce::AudioBuffer<float>& buffer, int from, int to,
                            int minSilence = 64)
    {
        auto onsets = 0;
        auto silent = minSilence;   // treat the window's start as preceded by silence

        for (int s = juce::jmax (0, from); s < juce::jmin (buffer.getNumSamples(), to); ++s)
        {
            auto nonZero = false;

            for (int c = 0; c < buffer.getNumChannels(); ++c)
                if (! juce::exactlyEqual (buffer.getSample (c, s), 0.0f))
                    nonZero = true;

            if (nonZero)
            {
                if (silent >= minSilence)
                    ++onsets;

                silent = 0;
            }
            else
            {
                ++silent;
            }
        }

        return onsets;
    }

    /** The largest absolute difference between two buffers, sample for sample.

        Written out inline at four sites before this existed — "render twice and
        compare" is the shape of every determinism and transparency assertion in
        the suite. Returns -1 if the shapes differ, so a mismatch cannot read as
        agreement. */
    inline float maxDifference (const juce::AudioBuffer<float>& a,
                                const juce::AudioBuffer<float>& b)
    {
        if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples())
            return -1.0f;

        auto worst = 0.0f;

        for (int c = 0; c < a.getNumChannels(); ++c)
            for (int s = 0; s < a.getNumSamples(); ++s)
                worst = juce::jmax (worst, std::abs (a.getSample (c, s) - b.getSample (c, s)));

        return worst;
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
