/* ============================================================================
   FORRÓ BOX — sequencer clock

   A sixteenth-note step counter advanced from the audio block's sample
   position. PLANNING.md is explicit that the prototype's 25 ms setInterval
   lookahead scheduler is to be "replaced entirely": a wall-clock timer cannot
   place a trigger at a known sample, and a groove whose timing depends on the
   host's buffer size is not something a listening test can later diagnose.

   Deliberately a plain class with no processor, host or audio device
   dependency. Its whole behaviour is a function of (sample rate, bpm, swing,
   window, block partitioning), so the tests sweep it exhaustively offline —
   and 02-03 can substitute the host playhead as the tempo source without
   touching the step maths below.
============================================================================ */
#pragma once

#include <juce_core/juce_core.h>

#include "ForroBoxState.h"

namespace forrobox
{

/** One step firing, placed at a sample within the block being rendered. */
struct StepEvent
{
    int step { 0 };          // index within the active window, [0, activeSteps)
    int sampleOffset { 0 };   // offset within the current block, [0, numSamples)
};

/** What the clock does with a step it has placed.

    An interface rather than a template so the whole implementation stays in
    Clock.cpp, and rather than a returned buffer so there is no capacity to
    overflow — an oversized block simply produces more calls. The indirect call
    costs nothing measurable at a handful of steps per block, and it allocates
    and locks nothing, which is what the audio thread actually cares about. */
class StepListener
{
public:
    virtual ~StepListener() = default;
    virtual void stepTriggered (StepEvent event) = 0;
};

class Clock
{
public:
    /** Read once per block. Passed in rather than read from the APVTS so the
        tests can sweep tempo and swing without constructing a processor. */
    struct Params
    {
        int   bpm { 132 };          // clamped to [kMinBpm, kMaxBpm]
        float swing { 0.0f };        // clamped to [0, 100] percent
        int   activeSteps { 16 };    // the window: 16 or 32
    };

    static constexpr int kMinBpm = 40;
    static constexpr int kMaxBpm = 300;

    /** Sixteenth notes: four steps per beat. */
    static constexpr double kStepsPerBeat = 4.0;

    /** PLANNING.md, "Swing": delay = (swing/100) x 0.6 x stepDuration, on odd
        sixteenths only. At swing 100 an odd step is pushed 0.6 of a step late —
        still strictly before the next step's grid position, which is why
        emissions stay in order at every swing value. */
    static constexpr double kMaxSwingFraction = 0.6;

    /** Reported by currentStep() while stopped. Matches PLANNING.md's
        `currentStep` default of -1. */
    static constexpr int kStoppedStep = -1;

    /** The only place this class touches anything but its own scalars. */
    void prepare (double sampleRateToUse) noexcept;

    /** Back to "about to emit step 0". Called on transport start and stop, so
        starting never resumes mid-pattern. */
    void reset() noexcept;

    /** Places every step falling inside the next `numSamples` and hands each to
        `listener`. Allocation-free, lock-free and noexcept: this runs on the
        audio thread. */
    void advance (int numSamples, const Params& params, StepListener& listener) noexcept;

    /** The most recently emitted step, or kStoppedStep after a reset.

        Read by the tests. The playhead value the editor will read is the
        processor's own atomic, fed from stepTriggered — this one is not
        thread-safe and is not the UI's source. */
    int currentStep() const noexcept { return lastEmittedStep; }

private:
    double sampleRate { 0.0 };

    // Samples from the current block's start to the next GRID boundary. Kept
    // fractional: accumulating a rounded integer per step is precisely the
    // drift this design exists to avoid.
    //
    // The grid advances unconditionally — it is never held back by a step whose
    // swung placement has not landed yet. Conflating "where the grid is" with
    // "which step is next to emit" is what previously let a deferred step drag
    // the grid backwards by an amount computed at the OLD tempo, which then had
    // to be clamped per advance() call — making placement depend on how the host
    // partitioned the samples, the one thing this class must not do.
    double gridPhase { 0.0 };

    // A step whose swung placement landed past the end of the block its grid
    // boundary fell in. At most one can be outstanding: grid boundaries are
    // stepSamples apart and a swing offset is at most 0.6 of that, so if step k
    // is deferred then step k+1's grid boundary is already beyond the block.
    //
    // The offset is fixed when the step is deferred and only rebased per block,
    // so a later tempo or swing change cannot relocate a step already placed.
    bool   pendingValid  { false };
    int    pendingStep   { 0 };
    double pendingOffset { 0.0 };

    // Absolute step counter since reset. Monotonic; the emitted index is this
    // taken modulo the active window.
    long long nextStep { 0 };

    int lastEmittedStep { kStoppedStep };
};

} // namespace forrobox
