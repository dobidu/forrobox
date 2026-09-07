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
    pendingValid    = false;
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

    const double swingSamples = swing * 0.01 * kMaxSwingFraction * stepSamples;

    // An owed step comes first: it was deferred from a grid boundary earlier
    // than any boundary in this block, and a swung placement always precedes the
    // next grid position, so it cannot be overtaken.
    if (pendingValid && pendingOffset < static_cast<double> (numSamples))
    {
        listener.stepTriggered ({ pendingStep, juce::jmax (0, static_cast<int> (pendingOffset)) });
        lastEmittedStep = pendingStep;
        pendingValid    = false;
    }

    // The windowed index is carried rather than recomputed: `nextStep % window`
    // in the loop body emitted two hardware divisions per iteration, and the
    // counter only ever advances by one.
    auto step = static_cast<int> (nextStep % window);

    // Every grid boundary falling inside this block.
    while (gridPhase < static_cast<double> (numSamples))
    {

        // Swing offsets an odd step's PLACEMENT without moving the grid, so it
        // never accumulates — the structure of app.js:637, not its scheduler.
        // Parity is taken on the windowed index to match the prototype exactly;
        // the two agree because the window is always even.
        const double placement = gridPhase + (step % 2 == 1 ? swingSamples : 0.0);

        // floor(x + 0.5), not juce::roundToInt: roundToInt's magic-number trick
        // rounds ties to EVEN, and ties-to-even is not translation-invariant.
        // `placement` is block-relative, so the same absolute half-sample would
        // round differently depending on the parity of the block base — and
        // half-sample grids are ordinary, not exotic: 44100 Hz at 40 bpm gives a
        // step of exactly 16537.5 samples.
        const int offset = static_cast<int> (std::floor (placement + 0.5));

        if (offset < numSamples)
        {
            listener.stepTriggered ({ step, juce::jmax (0, offset) });
            lastEmittedStep = step;
        }
        else
        {
            // Swung past the end of this block. Held with the placement it was
            // given, to be emitted in a later block — never dropped, never twice.
            pendingValid  = true;
            pendingStep   = step;
            pendingOffset = static_cast<double> (offset);
        }

        ++nextStep;
        gridPhase += stepSamples;

        if (++step >= window)
            step = 0;
    }

    // Rebase onto the next block. gridPhase stays in [0, stepSamples) — no debt
    // is carried in it, so a tempo change has nothing stale to correct.
    gridPhase -= static_cast<double> (numSamples);

    if (pendingValid)
        pendingOffset -= static_cast<double> (numSamples);
}

} // namespace forrobox
