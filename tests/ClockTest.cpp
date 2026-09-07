/* ============================================================================
   FORRÓ BOX — sequencer clock tests

   Runs headless: no GUI, no audio device, no processor. The clock is a plain
   class precisely so its timing can be swept exhaustively here.

   The load-bearing case is block-size invariance: the same total number of
   samples, partitioned three different ways, must produce byte-identical step
   sequences. A clock that passes every other case here and fails that one would
   sound subtly different in every host.
============================================================================ */
#include <JuceHeader.h>

#include "Clock.h"
#include "ForroBoxState.h"

#include "TestHarness.h"

#include <cstddef>
#include <new>
#include <vector>

using namespace fbtest;

// ── allocation counter (AC-8) ───────────────────────────────────────────────
// Global operator new/delete are replaced for this whole executable, then the
// delta is read around the advance calls only. A comment claiming the audio
// path allocates nothing is worth nothing; this counts.
namespace
{
    std::size_t allocations = 0;
}

void* operator new (std::size_t size)                 { ++allocations; return std::malloc (size); }
void* operator new[] (std::size_t size)               { ++allocations; return std::malloc (size); }
void* operator new (std::size_t size, const std::nothrow_t&) noexcept   { ++allocations; return std::malloc (size); }
void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept { ++allocations; return std::malloc (size); }
void operator delete (void* p) noexcept               { std::free (p); }
void operator delete[] (void* p) noexcept             { std::free (p); }
void operator delete (void* p, std::size_t) noexcept  { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }
void operator delete (void* p, const std::nothrow_t&) noexcept   { std::free (p); }
void operator delete[] (void* p, const std::nothrow_t&) noexcept { std::free (p); }

namespace
{
    using forrobox::Clock;
    using forrobox::StepEvent;

    struct Hit
    {
        int       step;
        long long absolutePosition;

        bool operator!= (const Hit& other) const noexcept
        {
            return step != other.step || absolutePosition != other.absolutePosition;
        }
    };

    /** Records every step with its position on an absolute timeline, so results
        from different block partitions are directly comparable. */
    struct Recorder final : forrobox::StepListener
    {
        std::vector<Hit> hits;
        long long blockBase = 0;

        void stepTriggered (StepEvent e) override
        {
            hits.push_back ({ e.step, blockBase + e.sampleOffset });
        }
    };

    /** A listener that stores nothing, for the allocation measurement. */
    struct CountingListener final : forrobox::StepListener
    {
        int count = 0;
        void stepTriggered (StepEvent) override { ++count; }
    };

    /** Drives a freshly reset clock over `blocks`, returning the absolute hits. */
    std::vector<Hit> run (double sampleRate,
                          const Clock::Params& params,
                          const std::vector<int>& blocks)
    {
        Clock clock;
        clock.prepare (sampleRate);

        Recorder rec;
        rec.hits.reserve (4096);

        for (auto n : blocks)
        {
            clock.advance (n, params, rec);
            rec.blockBase += n;
        }

        return rec.hits;
    }

    /** `total` samples as one block, as single samples, and as a repeating
        irregular pattern — the three partitions AC-2 compares. */
    std::vector<int> singleBlock (int total) { return { total }; }

    std::vector<int> unitBlocks (int total) { return std::vector<int> (static_cast<size_t> (total), 1); }

    std::vector<int> irregularBlocks (int total)
    {
        const int pattern[] = { 1, 7, 3, 512, 63, 128, 2, 999, 17, 256 };
        std::vector<int> blocks;
        int emitted = 0;
        size_t i = 0;

        while (emitted < total)
        {
            const int n = juce::jmin (pattern[i++ % std::size (pattern)], total - emitted);
            blocks.push_back (n);
            emitted += n;
        }

        return blocks;
    }

    double stepSamplesFor (double sampleRate, int bpm)
    {
        return sampleRate * 60.0 / (static_cast<double> (bpm) * Clock::kStepsPerBeat);
    }

    // ── AC-1: internal tempo and absence of drift ───────────────────────────
    void testInternalTempo()
    {
        section ("internal tempo");

        // 48 kHz, 120 bpm: a sixteenth is 60/120/4 = 0.125 s = 6000 samples.
        const Clock::Params p { 120, 0.0f, 16 };
        const auto hits = run (48000.0, p, singleBlock (48000));

        checkEqual (static_cast<int> (hits.size()), 8, "one second at 120 bpm emits 8 sixteenths");

        bool positionsExact = true;
        for (size_t i = 0; i < hits.size(); ++i)
            if (hits[i].absolutePosition != static_cast<long long> (i) * 6000)
                positionsExact = false;

        check (positionsExact, "each step lands exactly on its 6000-sample grid position");

        check (hits.empty() || hits.front().absolutePosition == 0,
               "the first step lands at sample 0");

        // Drift is the failure mode a fractional step duration exists to
        // prevent, and it only shows over distance.
        const int  steps = 10000;
        const auto total = static_cast<int> (static_cast<double> (steps) * 6000.0) + 6000;
        const auto longRun = run (48000.0, p, std::vector<int> (static_cast<size_t> (total / 512) + 1, 512));

        check (static_cast<int> (longRun.size()) >= steps, "the long run reached 10000 steps");

        if (static_cast<int> (longRun.size()) >= steps)
        {
            const auto expected = static_cast<long long> (steps - 1) * 6000;
            const auto actual   = longRun[static_cast<size_t> (steps - 1)].absolutePosition;
            check (std::llabs (actual - expected) <= 1,
                   "step 10000 is within 1 sample of its ideal position (no cumulative drift)");
        }

        // A non-integer step duration is the ordinary case, not the exception.
        const Clock::Params odd { 137, 0.0f, 16 };
        const auto fractional = run (44100.0, odd, singleBlock (44100 * 4));
        const auto expectedStep = stepSamplesFor (44100.0, 137);

        bool fractionalOk = true;
        for (size_t i = 0; i < fractional.size(); ++i)
        {
            const auto ideal = static_cast<double> (i) * expectedStep;
            if (std::abs (static_cast<double> (fractional[i].absolutePosition) - ideal) > 1.0)
                fractionalOk = false;
        }

        check (fractionalOk, "a fractional step duration (44100 Hz, 137 bpm) stays within 1 sample");
    }

    // ── AC-2: the step sequence is independent of block size ────────────────
    void testBlockSizeInvariance()
    {
        section ("block-size invariance");

        const int total = 8192;

        struct Case { const char* name; Clock::Params params; };
        const Case cases[] {
            { "120 bpm, no swing, 16 steps",   { 120, 0.0f,   16 } },
            { "132 bpm, swing 38, 16 steps",   { 132, 38.0f,  16 } },
            { "138 bpm, swing 54, 32 steps",   { 138, 54.0f,  32 } },
            { "300 bpm, swing 100, 32 steps",  { 300, 100.0f, 32 } },
            { "40 bpm, swing 100, 16 steps",   { 40,  100.0f, 16 } },
        };

        for (const auto& c : cases)
        {
            const auto reference = run (44100.0, c.params, singleBlock (total));
            const auto units     = run (44100.0, c.params, unitBlocks (total));
            const auto irregular = run (44100.0, c.params, irregularBlocks (total));

            checkEqual (static_cast<int> (units.size()), static_cast<int> (reference.size()),
                        juce::String ("unit blocks emit the same count -- ") + c.name);
            checkEqual (static_cast<int> (irregular.size()), static_cast<int> (reference.size()),
                        juce::String ("irregular blocks emit the same count -- ") + c.name);

            int unitDiffs = 0, irregularDiffs = 0;
            for (size_t i = 0; i < reference.size(); ++i)
            {
                if (i < units.size()     && units[i]     != reference[i]) ++unitDiffs;
                if (i < irregular.size() && irregular[i] != reference[i]) ++irregularDiffs;
            }

            checkEqual (unitDiffs, 0,
                        juce::String ("8192 blocks of 1 match one block of 8192 -- ") + c.name);
            checkEqual (irregularDiffs, 0,
                        juce::String ("irregular partitions match one block of 8192 -- ") + c.name);
        }
    }

    // ── AC-3: swing delays odd steps without accumulating or reordering ─────
    void testSwing()
    {
        section ("swing");

        const double sampleRate  = 48000.0;
        const int    bpm         = 120;
        const double stepSamples = 6000.0;

        for (const float swing : { 0.0f, 25.0f, 38.0f, 54.0f, 100.0f })
        {
            const Clock::Params p { bpm, swing, 16 };
            const auto hits = run (sampleRate, p, singleBlock (48000 * 2));
            const auto swingSamples = (static_cast<double> (swing) / 100.0)
                                    * Clock::kMaxSwingFraction * stepSamples;

            int evenWrong = 0, oddWrong = 0, outOfOrder = 0;
            long long previous = -1;

            for (size_t i = 0; i < hits.size(); ++i)
            {
                const auto grid     = static_cast<double> (i) * stepSamples;
                const auto expected = grid + (i % 2 == 1 ? swingSamples : 0.0);
                const auto actual   = static_cast<double> (hits[i].absolutePosition);

                if (std::abs (actual - expected) > 1.0)
                    (i % 2 == 1 ? oddWrong : evenWrong)++;

                if (hits[i].absolutePosition <= previous)
                    ++outOfOrder;

                previous = hits[i].absolutePosition;
            }

            const auto label = juce::String (" (swing ") + juce::String (swing, 0) + ")";
            checkEqual (evenWrong, 0, "even steps stay on the unswung grid" + label);
            checkEqual (oddWrong,  0, "odd steps are delayed by exactly (swing/100) x 0.6 x step" + label);
            checkEqual (outOfOrder, 0, "emitted positions are strictly increasing" + label);
        }

        // Swing must not accumulate: an even step far into the run is still on
        // the grid, which is the property app.js gets from advancing
        // nextNoteTime independently of the swing offset.
        const auto shuffled = run (sampleRate, { bpm, 100.0f, 16 }, singleBlock (48000 * 4));
        bool lateEvenOnGrid = true;
        for (size_t i = 0; i < shuffled.size(); i += 2)
            if (std::abs (static_cast<double> (shuffled[i].absolutePosition)
                          - static_cast<double> (i) * stepSamples) > 1.0)
                lateEvenOnGrid = false;

        check (lateEvenOnGrid, "at swing 100 the 60th even step is still on the grid (no accumulation)");
    }

    /** Shared by AC-4, AC-5 and AC-7: the emitted indices must be the unbroken
        sequence 0,1,2,... taken modulo the window in force at the time. Any
        dropped or duplicated step breaks it. */
    int continuityBreaks (const std::vector<Hit>& hits, int window)
    {
        int breaks = 0;
        for (size_t i = 0; i < hits.size(); ++i)
            if (hits[i].step != static_cast<int> (i % static_cast<size_t> (window)))
                ++breaks;

        return breaks;
    }

    // ── AC-4: a step swung past the block end is emitted once, later ────────
    void testBoundaryCrossing()
    {
        section ("swung steps crossing a block boundary");

        // 100-sample blocks against a 6000-sample step with a 3600-sample swing
        // offset: almost every odd step is placed beyond the end of the block
        // its grid position falls in.
        const Clock::Params p { 120, 100.0f, 16 };
        const int total = 48000;

        const auto reference = run (48000.0, p, singleBlock (total));
        const auto chopped   = run (48000.0, p, std::vector<int> (static_cast<size_t> (total / 100), 100));

        checkEqual (static_cast<int> (chopped.size()), static_cast<int> (reference.size()),
                    "no step is lost or duplicated when every odd step crosses a boundary");

        int diffs = 0;
        for (size_t i = 0; i < reference.size() && i < chopped.size(); ++i)
            if (chopped[i] != reference[i]) ++diffs;

        checkEqual (diffs, 0, "each owed step fires in a later block at its correct absolute position");
        checkEqual (continuityBreaks (chopped, 16), 0, "step indices remain unbroken across boundaries");

        // A block shorter than the swing offset is the extreme case.
        const auto tiny = run (48000.0, p, std::vector<int> (static_cast<size_t> (total), 1));
        checkEqual (static_cast<int> (tiny.size()), static_cast<int> (reference.size()),
                    "single-sample blocks still emit every step exactly once at swing 100");
    }

    // ── AC-5: the index wraps over the active window, not the storage ────────
    void testWindowWrap()
    {
        section ("window wrap");

        for (const int window : { 16, 32 })
        {
            const Clock::Params p { 132, 38.0f, window };
            const auto hits = run (44100.0, p, singleBlock (44100 * 6));

            int outOfRange = 0;
            for (const auto& h : hits)
                if (h.step < 0 || h.step >= window)
                    ++outOfRange;

            const auto label = juce::String (" (window ") + juce::String (window) + ")";
            checkEqual (outOfRange, 0, "every emitted index is inside the window" + label);
            checkEqual (continuityBreaks (hits, window), 0, "indices advance 0..N-1 then wrap to 0" + label);
            check (hits.size() > static_cast<size_t> (window), "the run went past one full window" + label);
        }

        // 02-01 settled that the 32 slots are storage and `steps` is a view, so
        // the clock must never wrap over kMaxSteps when the window is 16.
        const auto sixteen = run (44100.0, { 132, 0.0f, 16 }, singleBlock (44100 * 4));
        int beyondWindow = 0;
        for (const auto& h : sixteen)
            if (h.step >= 16)
                ++beyondWindow;

        checkEqual (beyondWindow, 0, "a 16-step window never emits an index from the 32-slot storage");

        // Changing the window mid-run must not drop or duplicate a step.
        {
            Clock clock;
            clock.prepare (44100.0);
            Recorder rec;
            rec.hits.reserve (1024);

            long long counted = 0;
            int breaks = 0;

            for (int block = 0; block < 200; ++block)
            {
                const int window = block < 100 ? 16 : 32;
                const auto before = rec.hits.size();
                clock.advance (512, { 132, 38.0f, window }, rec);

                for (size_t i = before; i < rec.hits.size(); ++i)
                {
                    if (rec.hits[i].step != static_cast<int> (counted % window))
                        ++breaks;
                    ++counted;
                }

                rec.blockBase += 512;
            }

            checkEqual (breaks, 0, "switching the window mid-run neither drops nor duplicates a step");
            check (counted > 0, "the mid-run window switch actually emitted steps");
        }

        // An out-of-range window is clamped, not wrapped into nonsense. 02-01's
        // lesson: a caller forwarding the `steps` CHOICE index (0 or 1) instead
        // of 16 or 32 must fail visibly rather than silently.
        const auto clamped = run (44100.0, { 132, 0.0f, 0 }, singleBlock (44100));
        int clampedOutOfRange = 0;
        for (const auto& h : clamped)
            if (h.step != 0)
                ++clampedOutOfRange;

        checkEqual (clampedOutOfRange, 0, "an out-of-range window clamps to a valid one");
    }

    // ── AC-6 (clock half): reset semantics ──────────────────────────────────
    void testResetSemantics()
    {
        section ("reset semantics");

        Clock clock;
        clock.prepare (48000.0);
        checkEqual (clock.currentStep(), Clock::kStoppedStep, "a prepared clock reports the stopped sentinel");

        Recorder rec;
        rec.hits.reserve (64);
        clock.advance (48000, { 120, 0.0f, 16 }, rec);

        check (! rec.hits.empty() && rec.hits.front().step == 0, "the first step after reset is step 0");
        check (! rec.hits.empty() && rec.hits.front().absolutePosition == 0, "step 0 lands at offset 0");
        checkEqual (clock.currentStep(), rec.hits.empty() ? -99 : rec.hits.back().step,
                    "currentStep reports the most recently emitted step");

        clock.reset();
        checkEqual (clock.currentStep(), Clock::kStoppedStep, "reset returns currentStep to the sentinel");

        Recorder afterReset;
        afterReset.hits.reserve (64);
        clock.advance (48000, { 120, 0.0f, 16 }, afterReset);

        check (! afterReset.hits.empty() && afterReset.hits.front().step == 0,
               "restarting emits step 0 again rather than resuming mid-pattern");

        // An unprepared clock must emit nothing rather than divide by zero.
        Clock unprepared;
        Recorder none;
        unprepared.advance (512, { 132, 38.0f, 16 }, none);
        checkEqual (static_cast<int> (none.hits.size()), 0, "an unprepared clock emits nothing");

        Recorder zeroLength;
        clock.advance (0, { 132, 38.0f, 16 }, zeroLength);
        checkEqual (static_cast<int> (zeroLength.hits.size()), 0, "a zero-length block emits nothing");
    }

    // ── AC-7: parameter changes mid-stream ──────────────────────────────────
    void testParameterChangesMidStream()
    {
        section ("parameter changes mid-stream");

        Clock clock;
        clock.prepare (48000.0);

        Recorder rec;
        rec.hits.reserve (8192);

        // Large tempo jumps between blocks, alternating with swing changes.
        const int bpms[]   { 132, 300, 40, 200, 40, 300, 132 };
        const float swings[] { 0.0f, 100.0f, 38.0f, 0.0f, 100.0f, 54.0f, 22.0f };

        for (int block = 0; block < 700; ++block)
        {
            const auto i = static_cast<size_t> (block / 100);
            clock.advance (512, { bpms[i], swings[i], 16 }, rec);
            rec.blockBase += 512;
        }

        checkEqual (continuityBreaks (rec.hits, 16), 0,
                    "step indices stay unbroken across bpm jumps of 40 to 300 and back");

        int outOfOrder = 0;
        long long previous = -1;
        for (const auto& h : rec.hits)
        {
            if (h.absolutePosition < previous)
                ++outOfOrder;
            previous = h.absolutePosition;
        }

        checkEqual (outOfOrder, 0, "no event is emitted earlier than the one before it");

        // Compared against the analytic count rather than an arbitrary floor.
        // An arbitrary floor is how the first version of this case failed: 81
        // steps is the correct answer and the threshold said 100.
        double expectedSteps = 0.0;
        for (size_t i = 0; i < std::size (bpms); ++i)
            expectedSteps += (100.0 * 512.0) / stepSamplesFor (48000.0, bpms[i]);

        check (std::abs (static_cast<double> (rec.hits.size()) - expectedSteps) <= static_cast<double> (std::size (bpms)),
               "the emitted count matches the sum of the segments' expected counts");

        // An out-of-range bpm is clamped rather than producing a degenerate
        // step duration — a zero would make the emission loop non-terminating.
        Recorder wild;
        wild.hits.reserve (256);
        Clock wildClock;
        wildClock.prepare (48000.0);
        wildClock.advance (48000, { 0, 0.0f, 16 }, wild);
        check (! wild.hits.empty(), "bpm 0 clamps to the minimum rather than hanging or emitting nothing");

        Recorder huge;
        huge.hits.reserve (4096);
        Clock hugeClock;
        hugeClock.prepare (48000.0);
        hugeClock.advance (48000, { 100000, 0.0f, 16 }, huge);
        const auto atMaxBpm = run (48000.0, { Clock::kMaxBpm, 0.0f, 16 }, singleBlock (48000));
        checkEqual (static_cast<int> (huge.hits.size()), static_cast<int> (atMaxBpm.size()),
                    "an absurd bpm clamps to kMaxBpm");
    }

    // ── AC-8: the clock allocates nothing when advanced ─────────────────────
    void testNoAllocationOnAdvance()
    {
        section ("audio-thread contract");

        Clock clock;
        CountingListener listener;

        // prepare() is the sanctioned allocation point; measurement starts after.
        clock.prepare (48000.0);

        const auto before = allocations;

        for (int block = 0; block < 2000; ++block)
            clock.advance (512, { 132 + (block % 100), static_cast<float> (block % 101), block % 2 == 0 ? 16 : 32 }, listener);

        const auto delta = allocations - before;

        checkEqual (static_cast<int> (delta), 0,
                    "Clock::advance performed zero allocations across 2000 blocks");
        check (listener.count > 0, "the measured run actually emitted steps (a silent clock proves nothing)");
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::cout << "Forro Box — sequencer clock tests" << std::endl;

    testInternalTempo();
    testBlockSizeInvariance();
    testSwing();
    testBoundaryCrossing();
    testWindowWrap();
    testResetSemantics();
    testParameterChangesMidStream();
    testNoAllocationOnAdvance();

    return reportSummary();
}
