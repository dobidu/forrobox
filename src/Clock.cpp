#include "Clock.h"

#include <cmath>

namespace forrobox
{

void Clock::prepare (double sampleRateToUse) noexcept
{
    // A non-finite rate is rejected rather than stored. NaN fails every
    // comparison, so `sampleRate <= 0.0` would NOT catch it downstream: the
    // step duration becomes NaN, the rounded offset becomes 0, and the
    // emission loop below never terminates — an audio-thread hang.
    sampleRate = std::isfinite (sampleRateToUse) && sampleRateToUse > 0.0 ? sampleRateToUse : 0.0;
    reset();
}

void Clock::reset() noexcept
{
    gridPhase       = 0.0;   // step 0 is due immediately
    nextStep        = 0;
    lastEmittedStep = kStoppedStep;
}

void Clock::advance (int numSamples, const Params& params, StepListener& listener) noexcept
{
    if (numSamples <= 0 || sampleRate <= 0.0)
        return;   // not prepared, or nothing to render

    // Clamped rather than trusted. A zero or negative step duration would make
    // the loop below non-terminating, so this is a safety property, not
    // tidiness: nothing may reach stepSamples that could make it non-positive.
    // juce::jlimit cannot sanitise NaN — both `v < lo` and `hi < v` are false,
    // so it returns the NaN unchanged. Filtered explicitly first.
    const auto rawSwing = static_cast<double> (params.swing);
    const auto swing    = juce::jlimit (0.0, 100.0, std::isfinite (rawSwing) ? rawSwing : 0.0);
    const auto bpm      = juce::jlimit (kMinBpm, kMaxBpm, params.bpm);
    const auto window   = juce::jlimit (1, State::kMaxSteps, params.activeSteps);

    const double stepSamples = sampleRate * 60.0 / (static_cast<double> (bpm) * kStepsPerBeat);
    jassert (stepSamples > 0.0);

    const double swingSamples = (swing / 100.0) * kMaxSwingFraction * stepSamples;

    // gridPhase can be behind by as much as one swing offset — that is how an
    // owed step crosses a block boundary. But the debt was incurred at the
    // PREVIOUS tempo, and if the step has since become much shorter, every past
    // grid position falls due at once and clamps onto offset 0. Measured: 56
    // blocks at 40 bpm / swing 100 then one block at 300 bpm emitted five steps
    // at offsets 0 0 0 0 368 — in Phase 3, four simultaneous voice triggers on
    // one sample. Re-bound the debt to what the current tempo could justify.
    gridPhase = juce::jmax (gridPhase, -kMaxSwingFraction * stepSamples);

    for (;;)
    {
        const auto step = static_cast<int> (nextStep % window);

        // Swing offsets an odd step's PLACEMENT without advancing the grid, so
        // it never accumulates — the structure of app.js:637, not its scheduler.
        // Parity is taken on the windowed index to match the prototype exactly;
        // the two agree because the window is always even.
        const double placement = gridPhase + (step % 2 == 1 ? swingSamples : 0.0);

        // Round FIRST, then decide. Deciding on the unrounded placement and
        // then rounding the offset lets the two disagree at a block edge: a
        // placement of 0.6 in a one-sample block passes "0.6 < 1", rounds to 1,
        // and has to be clamped back to 0 — while the same step in a large
        // block lands at 1. That is a step sequence that changes with the host's
        // buffer size, which is the one thing this class exists to prevent.
        // floor(x + 0.5), not juce::roundToInt: roundToInt's magic-number trick
        // rounds ties to EVEN, and ties-to-even is not translation-invariant.
        // `placement` is block-relative, so the same absolute half-sample would
        // round differently depending on the parity of the block base — and
        // half-sample grids are ordinary, not exotic: 44100 Hz at 40 bpm gives a
        // step of exactly 16537.5 samples.
        const int offset = static_cast<int> (std::floor (placement + 0.5));

        // Every later step's placement is larger — a swung step is pushed by at
        // most 0.6 of a step, so it always lands before the next step's grid
        // position. So the first step past the block end ends the block, and
        // emissions are strictly increasing at every swing value.
        if (offset >= numSamples)
            break;

        // A step owed from a previous block can have a placement slightly in the
        // past; it fires at the top of this block rather than being dropped.
        listener.stepTriggered ({ step, juce::jmax (0, offset) });

        lastEmittedStep = step;
        ++nextStep;
        gridPhase += stepSamples;
    }

    // Rebase onto the next block. gridPhase is left negative when a step is
    // still owed, which is what carries it across the boundary exactly once.
    gridPhase -= static_cast<double> (numSamples);
}

} // namespace forrobox
