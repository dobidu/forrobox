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
#include "TestSuites.h"

#include <cstddef>
#include <cstdlib>
#include <limits>
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

#if defined (__clang__)
 #pragma clang diagnostic push
 #pragma clang diagnostic ignored "-Wmissing-prototypes"
#endif

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

#if defined (__clang__)
 #pragma clang diagnostic pop
#endif

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

        // The length of the block currently being advanced, and a tally of
        // events placed outside it. A sample offset of numSamples is one past
        // the end of the buffer the host handed us — in Phase 3 that is a write
        // past the end of the audio block. Comparing sequences between
        // partitions does NOT catch it, because every partition can agree on
        // the same out-of-range offset. Found by a negative control that the
        // invariance case passed.
        int currentBlockLength = 0;
        int offsetViolations   = 0;

        void stepTriggered (StepEvent e) override
        {
            if (e.sampleOffset < 0 || e.sampleOffset >= currentBlockLength)
                ++offsetViolations;

            hits.push_back ({ e.step, blockBase + e.sampleOffset });
        }
    };

    /** A listener that stores nothing, for the allocation measurement. */
    struct CountingListener final : forrobox::StepListener
    {
        int count = 0;
        void stepTriggered (StepEvent) override { ++count; }
    };

    /** Set by run() so every case can assert the offsets stayed in their blocks. */
    int lastRunOffsetViolations = 0;

    /** Position within a constant-tempo segment, derived by ONE multiply from an
        exact integer sample count rather than by accumulating a double per
        block.

        Accumulating is what broke block-size invariance on the first attempt:
        8192 additions of `perSample` do not sum to one addition of
        `8192 * perSample`, so the span boundaries drift apart between partitions
        and a placement can land in a gap (lost) or an overlap (emitted twice).
        A new segment starts whenever the tempo changes, so the multiply is
        always from a position the caller stated exactly. */
    struct PositionCursor
    {
        double segmentStart   = 0.0;   // position, in steps, where this tempo began
        long long segmentSamples = 0;  // samples rendered since then
        double perSample      = 0.0;   // steps per sample at this tempo

        void setTempo (double sampleRate, int bpm)
        {
            const auto next = (static_cast<double> (bpm) * Clock::kStepsPerBeat) / (sampleRate * 60.0);

            if (! juce::approximatelyEqual (next, perSample))
            {
                segmentStart  += static_cast<double> (segmentSamples) * perSample;
                segmentSamples = 0;
                perSample      = next;
            }
        }

        double positionAt (long long extraSamples) const
        {
            return segmentStart + static_cast<double> (segmentSamples + extraSamples) * perSample;
        }

        double rate() const { return perSample; }
    };

    /** Drives a clock over `blocks`, converting each block into the musical span
        it covers at `bpm`. Spans are contiguous: each starts where the last
        ended, exactly. */
    std::vector<Hit> runAt (double sampleRate, int bpm,
                            const Clock::Params& params,
                            const std::vector<int>& blocks,
                            double startPosition = 0.0)
    {
        Clock clock;
        Recorder rec;
        rec.hits.reserve (16384);

        PositionCursor cursor;
        cursor.segmentStart = startPosition;
        cursor.setTempo (sampleRate, bpm);

        for (auto n : blocks)
        {
            const auto start = cursor.positionAt (0);
            rec.currentBlockLength = n;
            clock.advance ({ start, cursor.rate(), n }, params, rec);
            rec.blockBase += n;
            cursor.segmentSamples += n;
        }

        lastRunOffsetViolations = rec.offsetViolations;
        return rec.hits;
    }

    /** Like runAt, but the caller chooses tempo and params per block. */
    template <typename ParamsForBlock>
    std::vector<Hit> runVarying (double sampleRate,
                                 const std::vector<int>& blocks,
                                 ParamsForBlock paramsForBlock)
    {
        Clock clock;
        Recorder rec;
        rec.hits.reserve (16384);

        PositionCursor cursor;
        long long elapsed = 0;

        for (auto n : blocks)
        {
            const auto spec = paramsForBlock (elapsed);
            cursor.setTempo (sampleRate, spec.bpm);

            const auto start = cursor.positionAt (0);
            rec.currentBlockLength = n;
            clock.advance ({ start, cursor.rate(), n }, { spec.swing, spec.activeSteps }, rec);
            rec.blockBase += n;
            cursor.segmentSamples += n;
            elapsed += n;
        }

        lastRunOffsetViolations = rec.offsetViolations;
        return rec.hits;
    }

    /** How two partitions of the same timeline are compared.

        Step indices, their order and their count must match EXACTLY. Sample
        positions must match within one sample.

        The one-sample tolerance is a real trade, not a fudge, and it is the
        price of being position-driven — which host sync requires. 02-02's clock
        owned a sample phase, so any partition of a block produced bit-identical
        offsets. A position-driven clock must convert a block-RELATIVE position
        delta into an offset, and `(P - start_k) / rate` is not bit-identical to
        `(P - origin) / rate - samples_k` however carefully it is arranged: the
        subtraction in position space loses bits the absolute form keeps.
        Measured worst case: 1 sample, i.e. 20 microseconds at 48 kHz.

        What is NOT traded away: no step is lost, none is duplicated, none is
        reordered, and none escapes its block. Those are asserted exactly. */
    struct PartitionDiff
    {
        int indexDiffs = 0;
        int positionDiffs = 0;   // beyond one sample
    };

    PartitionDiff comparePartitions (const std::vector<Hit>& reference,
                                     const std::vector<Hit>& other)
    {
        PartitionDiff d;
        for (size_t i = 0; i < reference.size() && i < other.size(); ++i)
        {
            if (other[i].step != reference[i].step)
                ++d.indexDiffs;
            if (std::llabs (other[i].absolutePosition - reference[i].absolutePosition) > 1)
                ++d.positionDiffs;
        }
        return d;
    }

    /** `total` samples as one block, as single samples, and as a repeating
        irregular pattern — the three partitions AC-2 compares. */
    std::vector<int> singleBlock (int total) { return { total }; }

    /** `total` samples as equal blocks of `size`. */
    std::vector<int> singleBlockList (int total, int size)
    {
        std::vector<int> blocks;
        for (int emitted = 0; emitted < total; emitted += size)
            blocks.push_back (juce::jmin (size, total - emitted));
        return blocks;
    }

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
        const int pBpm = 120;
        const Clock::Params p { 0.0f, 16 };
        const auto hits = runAt (48000.0, pBpm, p, singleBlock (48000));

        checkEqual (static_cast<int> (hits.size()), 8, "one second at 120 bpm emits 8 sixteenths");

        bool positionsExact = true;
        for (size_t i = 0; i < hits.size(); ++i)
            if (hits[i].absolutePosition != static_cast<long long> (i) * 6000)
                positionsExact = false;

        check (positionsExact, "each step lands exactly on its 6000-sample grid position");

        check (hits.empty() || hits.front().absolutePosition == 0,
               "the first step lands at sample 0");

        // Drift only shows over distance — and only when the step duration is
        // FRACTIONAL. At 48 kHz and 120 bpm a sixteenth is exactly 6000
        // samples, so truncating the step duration to an integer is invisible
        // here; a negative control proved this case could not see it. 44100 Hz
        // at 137 bpm gives 4828.467 samples, where truncation drifts by 0.467
        // of a sample per step and 4667 samples over the run.
        const int    steps        = 10000;
        const double driftStep    = stepSamplesFor (44100.0, 137);
        const int driftBpm = 137;
        const Clock::Params drift { 0.0f, 16 };
        const auto   driftTotal   = static_cast<int> (static_cast<double> (steps + 1) * driftStep);
        const auto   longRun      = runAt (44100.0, driftBpm, drift,
                                         std::vector<int> (static_cast<size_t> (driftTotal / 512) + 1, 512));

        check (static_cast<int> (longRun.size()) >= steps, "the long run reached 10000 steps");

        if (static_cast<int> (longRun.size()) >= steps)
        {
            const auto expected = static_cast<double> (steps - 1) * driftStep;
            const auto actual   = static_cast<double> (longRun[static_cast<size_t> (steps - 1)].absolutePosition);
            check (std::abs (actual - expected) <= 1.0,
                   "step 10000 of a fractional grid is within 1 sample (no cumulative drift)");
        }

        // A non-integer step duration is the ordinary case, not the exception.
        const int oddBpm = 137;
        const Clock::Params odd { 0.0f, 16 };
        const auto fractional = runAt (44100.0, oddBpm, odd, singleBlock (44100 * 4));
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

        struct Case { const char* name; int bpm; Clock::Params params; };
        const Case cases[] {
            { "120 bpm, no swing, 16 steps",  120, { 0.0f,   16 } },
            { "132 bpm, swing 38, 16 steps",  132, { 38.0f,  16 } },
            { "138 bpm, swing 54, 32 steps",  138, { 54.0f,  32 } },
            { "300 bpm, swing 100, 32 steps", 300, { 100.0f, 32 } },
            { "40 bpm, swing 100, 16 steps",   40, { 100.0f, 16 } },
        };

        for (const auto& c : cases)
        {
            const auto reference = runAt (44100.0, c.bpm, c.params, singleBlock (total));
            const auto units     = runAt (44100.0, c.bpm, c.params, unitBlocks (total));
            const auto irregular = runAt (44100.0, c.bpm, c.params, irregularBlocks (total));

            checkEqual (static_cast<int> (units.size()), static_cast<int> (reference.size()),
                        juce::String ("unit blocks emit the same count -- ") + c.name);
            checkEqual (static_cast<int> (irregular.size()), static_cast<int> (reference.size()),
                        juce::String ("irregular blocks emit the same count -- ") + c.name);

            const auto u = comparePartitions (reference, units);
            const auto ir = comparePartitions (reference, irregular);

            checkEqual (u.indexDiffs, 0,
                        juce::String ("8192 blocks of 1 emit the same step indices -- ") + c.name);
            checkEqual (u.positionDiffs, 0,
                        juce::String ("8192 blocks of 1 place every step within a sample -- ") + c.name);
            checkEqual (ir.indexDiffs, 0,
                        juce::String ("irregular partitions emit the same step indices -- ") + c.name);
            checkEqual (ir.positionDiffs, 0,
                        juce::String ("irregular partitions place every step within a sample -- ") + c.name);

            // Sequence equality is not enough: partitions can agree on an
            // offset that is nonetheless past the end of the block.
            runAt (44100.0, c.bpm, c.params, unitBlocks (total));
            checkEqual (lastRunOffsetViolations, 0,
                        juce::String ("every offset lands inside its own block, single samples -- ") + c.name);
            runAt (44100.0, c.bpm, c.params, irregularBlocks (total));
            checkEqual (lastRunOffsetViolations, 0,
                        juce::String ("every offset lands inside its own block, irregular -- ") + c.name);
        }
    }

    /** A sample offset must address a sample the host actually gave us. In
        Phase 3 an offset of numSamples is a write one past the end of the audio
        block; here it is simply wrong. Stated as its own case because the
        invariance sweep demonstrably passes a clock that violates it. */
    void testOffsetsStayInsideTheirBlock()
    {
        section ("offsets stay inside their block");

        struct Case { const char* name; int bpm; Clock::Params params; };
        const Case cases[] {
            { "swing 0",   120, { 0.0f,   16 } },
            { "swing 38",  132, { 38.0f,  16 } },
            { "swing 100", 120, { 100.0f, 32 } },
            { "max bpm",   300, { 100.0f, 16 } },
        };

        for (const auto& c : cases)
        {
            // Single-sample blocks are the strictest case: the only legal
            // offset is 0, so any rounding that escapes the block shows up.
            runAt (48000.0, c.bpm, c.params, unitBlocks (24000));
            checkEqual (lastRunOffsetViolations, 0,
                        juce::String ("no offset escapes a one-sample block -- ") + c.name);

            runAt (48000.0, c.bpm, c.params, std::vector<int> (240, 100));
            checkEqual (lastRunOffsetViolations, 0,
                        juce::String ("no offset escapes a 100-sample block -- ") + c.name);

            runAt (48000.0, c.bpm, c.params, irregularBlocks (48000));
            checkEqual (lastRunOffsetViolations, 0,
                        juce::String ("no offset escapes an irregular block -- ") + c.name);
        }
    }

    /** Half-sample grid positions. juce::roundToInt uses the magic-number
        trick, which rounds ties to EVEN — and ties-to-even is not
        translation-invariant, so a block-relative placement rounds differently
        depending on the parity of the block base. 44100 Hz at the plugin's
        minimum 40 bpm gives a step of 16537.5 samples, every other step
        landing exactly on a tie. The original invariance sweep could not see
        this: its only 40-bpm case used swing 100, whose placements happen to
        be integral, and its 8192-sample total never reached step 1. */
    void testTieRoundingIsTranslationInvariant()
    {
        section ("half-sample grid positions");

        const int pBpm = 40;
        const Clock::Params p { 0.0f, 16 };   // stepSamples = 16537.5 at 44.1k
        const int total = 70000;

        const auto reference = runAt (44100.0, pBpm, p, singleBlock (total));
        const auto units     = runAt (44100.0, pBpm, p, unitBlocks (total));
        const auto irregular = runAt (44100.0, pBpm, p, irregularBlocks (total));

        checkEqual (static_cast<int> (units.size()), static_cast<int> (reference.size()),
                    "a half-sample grid emits the same count in single-sample blocks");

        const auto u = comparePartitions (reference, units);
        const auto ir = comparePartitions (reference, irregular);
        checkEqual (u.indexDiffs, 0, "a half-sample grid emits the same indices in single-sample blocks");
        checkEqual (u.positionDiffs, 0, "a half-sample grid places every step within a sample");
        checkEqual (ir.indexDiffs, 0, "a half-sample grid emits the same indices in irregular blocks");
        checkEqual (ir.positionDiffs, 0, "half-sample placement is stable under irregular partitions");
    }

    /** Block-size invariance must survive a tempo change, not just hold at a
        fixed tempo. This is the gap that let a per-advance-call clamp through:
        run() holds Params constant for a whole run, so the main sweep never
        crosses a tempo change, and the parameter-change case uses one fixed
        block size, so it cannot compare partitions. Host tempo automation makes
        this the normal path, not an edge case. */
    void testInvarianceAcrossTempoChanges()
    {
        section ("block-size invariance across a tempo change");

        const int total = 32768;

        // Tempo and swing as functions of absolute sample position, so every
        // partition sees the same automation.
        struct Spec { int bpm; float swing; int activeSteps; };
        auto automation = [] (long long elapsed) -> Spec
        {
            const auto phase = static_cast<int> (elapsed / 4096) % 4;
            const int   bpms[]   { 40, 300, 132, 200 };
            const float swings[] { 100.0f, 100.0f, 38.0f, 0.0f };
            return { bpms[phase], swings[phase], 16 };
        };

        // Partitions that REFINE the reference — every block boundary of the
        // 4096 reference is also a boundary of these. That is the well-posed
        // question, and it is the one a host actually varies: buffer size.
        //
        // Irregular partitions are deliberately NOT compared here. Host tempo
        // is sampled once per block by definition, so a block straddling a
        // 4096-sample automation edge legitimately runs the whole block at one
        // tempo while the reference switches mid-way. The two see different
        // tempo timelines, so a difference is not a clock defect and comparing
        // them would assert something untrue. Irregular partitions ARE compared
        // in the constant-tempo sweep, where the question is well-posed.
        const auto reference = runVarying (48000.0, singleBlockList (total, 4096), automation);

        struct Refinement { const char* name; int size; };
        const Refinement refinements[] { { "2048", 2048 }, { "512", 512 }, { "64", 64 } };

        for (const auto& r : refinements)
        {
            const auto refined = runVarying (48000.0, singleBlockList (total, r.size), automation);
            const auto d = comparePartitions (reference, refined);
            const auto label = juce::String (" (blocks of ") + r.name + ")";

            checkEqual (static_cast<int> (refined.size()), static_cast<int> (reference.size()),
                        "a tempo change emits the same step count" + label);
            checkEqual (d.indexDiffs, 0, "a tempo change emits the same indices" + label);
            checkEqual (d.positionDiffs, 0, "a tempo change places every step within a sample" + label);
        }

        const auto units = runVarying (48000.0, unitBlocks (total), automation);
        const auto u = comparePartitions (reference, units);
        checkEqual (static_cast<int> (units.size()), static_cast<int> (reference.size()),
                    "a tempo change emits the same step count in single-sample blocks");
        checkEqual (u.indexDiffs, 0, "a tempo change emits the same indices in single-sample blocks");
        checkEqual (u.positionDiffs, 0, "a tempo change places every step within a sample");
        checkEqual (lastRunOffsetViolations, 0, "every step across a tempo change lands inside its block");
    }

    /** A tempo jump, and a BACKWARDS jump, must not produce a burst.

        In 02-02 the burst came from swing debt accumulated in the clock's own
        position: once the step got shorter, every past grid position fell due at
        once and clamped onto offset 0. The clock now holds no position, so the
        debt cannot exist — but the property is what mattered, so it is asserted
        through the span API instead, including the case 02-02 could not express:
        a span that starts BEHIND where the previous one ended, which is what a
        host loop or scrub produces. */
    void testNoBurstAfterTempoJump()
    {
        section ("tempo and position jumps");

        Clock clock;
        Recorder rec;
        rec.hits.reserve (512);

        // Run at the minimum tempo with maximum swing, then jump to the maximum
        // tempo — the exact scenario that burst in 02-02.
        PositionCursor cursor;
        cursor.setTempo (48000.0, 40);
        for (int block = 0; block < 56; ++block)
        {
            rec.currentBlockLength = 512;
            clock.advance ({ cursor.positionAt (0), cursor.rate(), 512 }, { 100.0f, 16 }, rec);
            rec.blockBase += 512;
            cursor.segmentSamples += 512;
        }

        const auto position = cursor.positionAt (0);
        const auto beforeJump = rec.hits.size();
        rec.currentBlockLength = 512;
        cursor.setTempo (48000.0, 300);
        clock.advance ({ cursor.positionAt (0), cursor.rate(), 512 }, { 100.0f, 16 }, rec);

        const auto emittedInJumpBlock = static_cast<int> (rec.hits.size() - beforeJump);
        check (emittedInJumpBlock <= 2,
               juce::String ("a tempo jump emits at most a couple of steps, not a burst (got ")
                   + juce::String (emittedInJumpBlock) + ")");

        int coincident = 0;
        for (size_t i = beforeJump; i + 1 < rec.hits.size(); ++i)
            if (rec.hits[i].absolutePosition == rec.hits[i + 1].absolutePosition)
                ++coincident;

        checkEqual (coincident, 0, "no two steps share a sample position after a tempo jump");
        checkEqual (rec.offsetViolations, 0, "the tempo jump places every step inside its block");

        // A BACKWARDS jump of 100 steps: a host looping back. The span length is
        // still one block, so the steps in between are simply not this block's
        // business — nothing catches up.
        Recorder jumped;
        jumped.hits.reserve (64);
        jumped.currentBlockLength = 512;
        Clock afterLoop;
        const auto slowPerSample = (40.0 * Clock::kStepsPerBeat) / (48000.0 * 60.0);
        afterLoop.advance ({ position - 100.0, slowPerSample, 512 }, { 100.0f, 16 }, jumped);

        check (static_cast<int> (jumped.hits.size()) <= 2,
               juce::String ("a backwards jump of 100 steps emits no catch-up burst (got ")
                   + juce::String (static_cast<int> (jumped.hits.size())) + ")");
        checkEqual (jumped.offsetViolations, 0, "a backwards jump places every step inside its block");
    }

    /** Non-finite inputs must not spin the emission loop.

        NaN fails every comparison, so a `span <= 0` test does not catch it and
        the loop would never terminate — an audio-thread hang. In 02-02 the risk
        lived on the sample rate; the clock no longer has one, so the span
        carries it, along with swing. */
    void testNonFiniteInputsDoNotHang()
    {
        section ("non-finite inputs");

        const auto nan = std::numeric_limits<double>::quiet_NaN();
        const auto inf = std::numeric_limits<double>::infinity();

        struct BadSpan { const char* name; double start; double rate; };
        const BadSpan bad[] {
            { "NaN start",        nan,  0.001 },
            { "NaN rate",         0.0,  nan },
            { "both NaN",         nan,  nan },
            { "infinite start",   inf,  0.001 },
            { "infinite rate",    0.0,  inf },
            { "zero rate",        5.0,  0.0 },
            { "negative rate",    5.0, -0.001 },
        };

        for (const auto& b : bad)
        {
            Clock clock;
            CountingListener none;
            clock.advance ({ b.start, b.rate, 512 }, { 38.0f, 16 }, none);
            checkEqual (none.count, 0, juce::String ("emits nothing for a ") + b.name);
        }

        // A NaN swing does not hang, but it does corrupt every ODD step's
        // placement. Asserting only "did not hang" could not see that, so the
        // honest question is what it emits: the same sequence as swing 0.
        const auto reference = runAt (48000.0, 132, { 0.0f, 16 }, std::vector<int> (200, 512));

        for (const float badSwing : { std::numeric_limits<float>::quiet_NaN(),
                                      std::numeric_limits<float>::infinity(),
                                      -std::numeric_limits<float>::infinity() })
        {
            const auto hits = runAt (48000.0, 132, { badSwing, 16 }, std::vector<int> (200, 512));

            checkEqual (static_cast<int> (hits.size()), static_cast<int> (reference.size()),
                        "a non-finite swing emits the same number of steps as swing 0");

            int diffs = 0;
            for (size_t i = 0; i < reference.size() && i < hits.size(); ++i)
                if (hits[i] != reference[i]) ++diffs;

            checkEqual (diffs, 0, "a non-finite swing is treated as 0 rather than corrupting placements");
            checkEqual (lastRunOffsetViolations, 0, "a non-finite swing places every step inside its block");
        }

        // A zero or negative block length emits nothing rather than dividing by it.
        for (const int n : { 0, -1, -512 })
        {
            Clock clock;
            CountingListener none;
            clock.advance ({ 0.0, 0.001, n }, { 38.0f, 16 }, none);
            checkEqual (none.count, 0, "emits nothing for a non-positive block length");
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
            const Clock::Params p { swing, 16 };
            const auto hits = runAt (sampleRate, bpm, p, singleBlock (48000 * 2));
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
        const auto shuffled = runAt (sampleRate, bpm, { 100.0f, 16 }, singleBlock (48000 * 4));
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
        const int pBpm = 120;
        const Clock::Params p { 100.0f, 16 };
        const int total = 48000;

        const auto reference = runAt (48000.0, pBpm, p, singleBlock (total));
        const auto chopped   = runAt (48000.0, pBpm, p, std::vector<int> (static_cast<size_t> (total / 100), 100));

        checkEqual (static_cast<int> (chopped.size()), static_cast<int> (reference.size()),
                    "no step is lost or duplicated when every odd step crosses a boundary");

        int diffs = 0;
        for (size_t i = 0; i < reference.size() && i < chopped.size(); ++i)
            if (chopped[i] != reference[i]) ++diffs;

        checkEqual (diffs, 0, "each owed step fires in a later block at its correct absolute position");
        checkEqual (continuityBreaks (chopped, 16), 0, "step indices remain unbroken across boundaries");

        // A block shorter than the swing offset is the extreme case.
        const auto tiny = runAt (48000.0, pBpm, p, std::vector<int> (static_cast<size_t> (total), 1));
        checkEqual (static_cast<int> (tiny.size()), static_cast<int> (reference.size()),
                    "single-sample blocks still emit every step exactly once at swing 100");
    }

    // ── AC-5: the index wraps over the active window, not the storage ────────
    void testWindowWrap()
    {
        section ("window wrap");

        for (const int window : { 16, 32 })
        {
            const Clock::Params p { 38.0f, window };
            const auto hits = runAt (44100.0, 132, p, singleBlock (44100 * 6));

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
        const auto sixteen = runAt (44100.0, 132, { 0.0f, 16 }, singleBlock (44100 * 4));
        int beyondWindow = 0;
        for (const auto& h : sixteen)
            if (h.step >= 16)
                ++beyondWindow;

        checkEqual (beyondWindow, 0, "a 16-step window never emits an index from the 32-slot storage");

        // Changing the window mid-run must not drop or duplicate a step.
        {
            Clock clock;
            Recorder rec;
            rec.hits.reserve (1024);

            long long counted = 0;
            int breaks = 0;
            PositionCursor cursor;
            cursor.setTempo (44100.0, 132);

            for (int block = 0; block < 200; ++block)
            {
                const int window = block < 100 ? 16 : 32;
                const auto before = rec.hits.size();
                rec.currentBlockLength = 512;
                clock.advance ({ cursor.positionAt (0), cursor.rate(), 512 }, { 38.0f, window }, rec);
                cursor.segmentSamples += 512;

                for (size_t i = before; i < rec.hits.size(); ++i)
                {
                    if (rec.hits[i].step != static_cast<int> (counted % window))
                        ++breaks;
                    ++counted;
                }

                rec.blockBase += 512;
            }

            checkEqual (breaks, 0, "switching the window mid-run neither drops nor duplicates a step");
            checkEqual (rec.offsetViolations, 0, "the window switch places every step inside its block");
            check (counted > 0, "the mid-run window switch actually emitted steps");
        }

        // An out-of-range window is clamped, not wrapped into nonsense. 02-01's
        // lesson: a caller forwarding the `steps` CHOICE index (0 or 1) instead
        // of 16 or 32 must fail visibly rather than silently.
        const auto clamped = runAt (44100.0, 132, { 0.0f, 0 }, singleBlock (44100));
        int clampedOutOfRange = 0;
        for (const auto& h : clamped)
            if (h.step != 0)
                ++clampedOutOfRange;

        checkEqual (clampedOutOfRange, 0, "an out-of-range window clamps to a valid one");
    }

    // ── AC-6 (clock half): reset and start-of-timeline semantics ────────────
    void testResetSemantics()
    {
        section ("reset semantics");

        Clock clock;
        checkEqual (clock.currentStep(), Clock::kStoppedStep, "a fresh clock reports the stopped sentinel");

        // "The first step is step 0" is now a property of the SPAN, not of
        // remembered state: a span starting at position 0 begins the pattern.
        // That is what makes a host jump harmless — there is no internal
        // position for the jump to contradict.
        const auto fromZero = runAt (48000.0, 120, { 0.0f, 16 }, singleBlock (48000));
        check (! fromZero.empty() && fromZero.front().step == 0, "a span starting at 0 emits step 0 first");
        check (! fromZero.empty() && fromZero.front().absolutePosition == 0, "step 0 lands at offset 0");

        Recorder rec;
        rec.hits.reserve (64);
        rec.currentBlockLength = 48000;
        clock.advance ({ 0.0, (120.0 * Clock::kStepsPerBeat) / (48000.0 * 60.0), 48000 },
                       { 0.0f, 16 }, rec);

        checkEqual (clock.currentStep(), rec.hits.empty() ? -99 : rec.hits.back().step,
                    "currentStep reports the most recently emitted step");

        clock.reset();
        checkEqual (clock.currentStep(), Clock::kStoppedStep, "reset returns currentStep to the sentinel");

        // A span starting mid-timeline emits the step the position implies, NOT
        // step 0 — the property that lets the plugin join a host already playing.
        const auto midTimeline = runAt (48000.0, 120, { 0.0f, 16 }, singleBlock (24000), 5.0);
        check (! midTimeline.empty(), "a span starting mid-timeline emits steps");
        check (! midTimeline.empty() && midTimeline.front().step == 5,
               "a span starting at position 5 emits step 5, not step 0");

        // And position 21 with a 16-step window wraps to step 5.
        const auto wrapped = runAt (48000.0, 120, { 0.0f, 16 }, singleBlock (24000), 21.0);
        check (! wrapped.empty() && wrapped.front().step == 5,
               "position 21 in a 16-step window emits step 5");
    }

    // ── AC-7: parameter changes mid-stream ──────────────────────────────────
    void testParameterChangesMidStream()
    {
        section ("parameter changes mid-stream");

        Clock clock;
        Recorder rec;
        rec.hits.reserve (8192);

        // Tempo jumps with swing held CONSTANT. Continuity is exact here: the
        // placement function does not change, so the step a position maps to
        // does not move.
        const int bpms[] { 132, 300, 40, 200, 40, 300, 132 };

        PositionCursor cursor;
        for (int block = 0; block < 700; ++block)
        {
            const auto i = static_cast<size_t> (block / 100);
            cursor.setTempo (48000.0, bpms[i]);
            rec.currentBlockLength = 512;
            clock.advance ({ cursor.positionAt (0), cursor.rate(), 512 }, { 38.0f, 16 }, rec);
            rec.blockBase += 512;
            cursor.segmentSamples += 512;
        }

        checkEqual (continuityBreaks (rec.hits, 16), 0,
                    "step indices stay unbroken across bpm jumps of 40 to 300 and back");

        int outOfOrder = 0;
        long long previous = -1;
        for (const auto& h : rec.hits)
        {
            if (h.absolutePosition <= previous)
                ++outOfOrder;
            previous = h.absolutePosition;
        }

        checkEqual (outOfOrder, 0, "no event is emitted at or before the position of the one before it");

        // Compared against the analytic count rather than an arbitrary floor.
        // An arbitrary floor is how the first version of this case failed: 81
        // steps is the correct answer and the threshold said 100.
        double expectedSteps = 0.0;
        for (size_t i = 0; i < std::size (bpms); ++i)
            expectedSteps += 100.0 * 512.0 * ((static_cast<double> (bpms[i]) * Clock::kStepsPerBeat) / (48000.0 * 60.0));

        check (std::abs (static_cast<double> (rec.hits.size()) - expectedSteps) <= static_cast<double> (std::size (bpms)),
               "the emitted count matches the sum of the segments' expected counts");

        // Tempo clamping moved to the processor with the tempo itself — see the
        // bpm cases in StateRoundTripTest. What is still the clock's is the
        // WINDOW clamp: a caller forwarding the `steps` CHOICE index instead of
        // a step count must fail visibly, not run a 1-step pattern silently.
        for (const int badWindow : { 0, -1, 999 })
        {
            const auto clamped = runAt (48000.0, 132, { 0.0f, badWindow }, singleBlock (48000));
            int outOfRange = 0;
            for (const auto& h : clamped)
                if (h.step < 0 || h.step >= forrobox::State::kMaxSteps)
                    ++outOfRange;

            checkEqual (outOfRange, 0, "an out-of-range window clamps to a usable one");
        }
    }

    // ── AC-8: the clock allocates nothing when advanced ─────────────────────
    void testNoAllocationOnAdvance()
    {
        section ("audio-thread contract");

        Clock clock;
        CountingListener listener;

        const auto before = allocations;

        PositionCursor cursor;
        for (int block = 0; block < 2000; ++block)
        {
            cursor.setTempo (48000.0, 132 + (block % 100));
            clock.advance ({ cursor.positionAt (0), cursor.rate(), 512 },
                           { static_cast<float> (block % 101), block % 2 == 0 ? 16 : 32 }, listener);
            cursor.segmentSamples += 512;
        }

        const auto delta = allocations - before;

        checkEqual (static_cast<int> (delta), 0,
                    "Clock::advance performed zero allocations across 2000 blocks");
        check (listener.count > 0, "the measured run actually emitted steps (a silent clock proves nothing)");

        // Prove the instrument can register a reading. A counter that is broken
        // — or optimised away — reports zero allocations for everything, which
        // looks exactly like success. A negative control that allocated inside
        // advance was MISSED for this reason: the compiler elided its
        // new/delete pair, so nothing was ever counted. The escape through a
        // volatile pointer here is what makes the allocation unelidable.
        static double* volatile sink = nullptr;
        const auto beforeSelfTest = allocations;
        sink = new double (1.0);
        delete sink;
        sink = nullptr;

        check (allocations > beforeSelfTest,
               "the allocation counter registers a real allocation (it is not stuck at zero)");
    }
}

void runClockTests()
{
    std::cout << "Forro Box — sequencer clock tests" << std::endl;

    testInternalTempo();
    testBlockSizeInvariance();
    testOffsetsStayInsideTheirBlock();
    testTieRoundingIsTranslationInvariant();
    testInvarianceAcrossTempoChanges();
    testNoBurstAfterTempoJump();
    testNonFiniteInputsDoNotHang();
    testSwing();
    testBoundaryCrossing();
    testWindowWrap();
    testResetSemantics();
    testParameterChangesMidStream();
    testNoAllocationOnAdvance();
}
