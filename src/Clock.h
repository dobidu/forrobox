/* ============================================================================
   FORRÓ BOX — sequencer clock

   Emits the sixteenth-note steps falling inside a musical span. A block is
   described as "this rendering covers position A to position B", and the clock
   places each step in it at the right sample.

   Why a position range rather than "advance by N samples": PLANNING.md requires
   that SYNC lock step 0 to the host's bar, and is explicit that alignment "must
   be derived from absolute host PPQ each block rather than from a monotonically
   incremented local counter, otherwise a 16- or 32-step pattern drifts out of
   phase with the project". A clock that owns its own position cannot accept a
   host loop, jump or scrub; one that is told the span simply gets a different
   span. Internal tempo and host sync are therefore the same code path — the
   processor decides where the span comes from.

   The clock holds no position. It needs neither the sample rate nor the tempo:
   the span plus the block length gives the position-to-sample mapping, and that
   mapping is linear, so splitting a block cannot move a step.
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
    int sampleOffset { 0 };   // offset within the current BLOCK, [0, blockSize)
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
        tests can sweep swing and window without constructing a processor.

        Tempo is deliberately absent: it lives in the span the caller computes. */
    struct Params
    {
        float swing { 0.0f };        // clamped to [0, 100] percent
        int   activeSteps { 16 };    // the window: 16 or 32
    };

    /** Sixteenth notes: four steps per beat. Positions are measured in steps,
        so a host PPQ position becomes a step position by multiplying by this. */
    static constexpr double kStepsPerBeat = 4.0;

    /** PLANNING.md, "Swing": delay = (swing/100) x 0.6 x stepDuration, on odd
        sixteenths only. At swing 100 an odd step is pushed 0.6 of a step late —
        still strictly before the next step's position, which is why placements
        stay strictly increasing at every swing value. */
    static constexpr double kMaxSwingFraction = 0.6;

    /** Positions beyond this are refused rather than emitted from.

        Two hazards, both audio-thread hangs. `static_cast<long long>` of a
        position past LLONG_MAX is undefined behaviour and in practice yields
        LLONG_MIN, after which the loop iterates ~9.2e18 times. And past 2^53 a
        double can no longer represent consecutive integers, so incrementing the
        step candidate stops changing the placement and the loop never advances.
        A garbage or overflowed host ppq passes isfinite() perfectly happily.

        2^40 sixteenths is about 17 years at 120 BPM — far beyond any real
        session, and far below either cliff. */
    static constexpr double kMaxPosition = 1099511627776.0;   // 2^40

    /** Rates beyond this are refused: a host reporting an absurdly small sample
        rate would otherwise ask for a span millions of steps long. */
    static constexpr double kMaxStepsPerSample = 1.0;

    /** Reported by currentStep() while stopped. Matches PLANNING.md's
        `currentStep` default of -1. */
    static constexpr int kStoppedStep = -1;

    /** Forgets the last emitted step. There is no position to reset — the span
        the caller passes is the only position there is. */
    void reset() noexcept { lastEmittedStep = kStoppedStep; }

    /** One stretch of musical timeline, rendered over part of a block.

        `sampleOffset` is where in the block this stretch begins. The clock adds
        it to every offset it reports, so StepEvent::sampleOffset always means
        "offset within the block" — the caller does not have to shift it, and a
        listener cannot be handed an offset whose frame it has to guess. */
    struct Span
    {
        double startInSteps { 0.0 };
        double stepsPerSample { 0.0 };
        int    numSamples { 0 };
        int    sampleOffset { 0 };
    };

    /** Places every step whose swung position falls in
        [span.startInSteps, span.startInSteps + span.stepsPerSample * span.numSamples)
        and hands each to `listener`.

        The RATE is passed rather than the span's end, so the position-to-sample
        conversion is one exact number rather than a difference of two large
        doubles. Deriving it from `end - start` made the conversion depend on the
        block length: the same step landed on sample 4410 in one block of 8192
        and on 4409 across irregular blocks. A rate is identical for every
        partition of the same stretch of timeline, which is what the guarantee
        needs.

        Contiguous spans tile the timeline exactly, so a step is emitted once and
        only once as long as each span starts where the previous one ended.
        Guaranteeing that is the CALLER's job, and the caller is the only one who
        knows what the timeline is doing. Allocation-free, lock-free and
        noexcept: this runs on the audio thread. */
    void advance (const Span& span, const Params& params, StepListener& listener) noexcept;

    /** The most recently emitted step, or kStoppedStep after a reset.

        Read by the tests. The playhead value the editor will read is the
        processor's own atomic, fed from stepTriggered — this one is not
        thread-safe and is not the UI's source. */
    int currentStep() const noexcept { return lastEmittedStep; }

private:
    int lastEmittedStep { kStoppedStep };
};

} // namespace forrobox
