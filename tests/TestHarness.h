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

#include <juce_core/juce_core.h>

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
    inline std::size_t allocations = 0;

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
