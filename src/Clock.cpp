#include "Clock.h"

#include <cmath>

namespace forrobox
{

void Clock::advance (const Span& span, const Params& params, StepListener& listener) noexcept
{
    const auto numSamples     = span.numSamples;
    const auto startInSteps   = span.startInSteps;
    const auto stepsPerSample = span.stepsPerSample;

    // Non-finite values are rejected explicitly. NaN fails every comparison, so
    // a `rate <= 0.0` test would NOT catch it: the emission loop below would
    // never terminate, hanging the audio thread.
    //
    // The magnitude bounds are preconditions of the loop, not input validation:
    // static_cast<long long> of a position past LLONG_MAX is undefined
    // behaviour and yields LLONG_MIN in practice, after which the loop iterates
    // ~9.2e18 times; and past 2^53 a double cannot represent consecutive
    // integers, so incrementing the candidate stops changing the placement and
    // the loop never advances at all. A garbage host position passes isfinite
    // perfectly happily.
    if (numSamples <= 0
        || span.sampleOffset < 0
        || ! std::isfinite (startInSteps)
        || ! std::isfinite (stepsPerSample)
        || ! (stepsPerSample > 0.0)
        || stepsPerSample > kMaxStepsPerSample
        || std::abs (startInSteps) > kMaxPosition)
        return;

    const double endInSteps = startInSteps + stepsPerSample * static_cast<double> (numSamples);

    // juce::jlimit cannot sanitise NaN either — both comparisons are false, so
    // it returns the NaN unchanged.
    const auto rawSwing = static_cast<double> (params.swing);
    const auto swing    = juce::jlimit (0.0, 100.0, std::isfinite (rawSwing) ? rawSwing : 0.0);
    const auto window   = juce::jlimit (1, State::kMaxSteps, params.activeSteps);

    // In position space a step is exactly 1.0 long, so the swing offset is a
    // plain fraction of a step and needs no tempo to express.
    const double swingSteps = swing * 0.01 * kMaxSwingFraction;

    // The first candidate is floor(start), not floor(start) - 1.
    //
    // Every emitted placement satisfies `placement >= start - swingSteps`, and
    // swingSteps <= 0.6 < 1, so any emitter e obeys e > start - 1 >=
    // floor(start) - 1, hence e >= floor(start). The floor(start) - 1 candidate
    // therefore cannot emit at any swing value — it was one guaranteed-wasted
    // iteration per call, against roughly two useful ones in a 256-sample block.
    auto step = static_cast<long long> (std::floor (startInSteps));

    for (;;)
    {
        // Swing offsets an odd step's PLACEMENT only; the grid it sits on never
        // moves, which is why swing does not accumulate — the structure of
        // app.js:637, not its scheduler.
        //
        // Parity is taken on the ABSOLUTE step, not the windowed index. They
        // agree for every even window, which is the only kind the parameter
        // offers, so this matches the prototype — but the clamp above admits an
        // odd window, and with one the windowed parity flips at each wrap, so
        // the same sixteenth would be swung or not depending on which pass
        // through the pattern it is.
        const double placement = static_cast<double> (step)
                               + (step % 2 != 0 ? swingSteps : 0.0);

        if (placement >= endInSteps)
            break;

        // Membership is decided in POSITION space, not on the rounded sample
        // offset. Contiguous spans tile the timeline exactly, so every placement
        // belongs to exactly one span — deciding on the rounded offset instead
        // would let a placement near a block edge be rejected by both the span
        // that contains it and the next one, dropping the step.
        if (placement >= startInSteps)
        {
            // Truncating floor, and no clamp is needed: membership was decided
            // in position space, so (placement - start) / rate is in
            // [0, numSamples) and its floor is a valid offset by construction.
            //
            // That is the reason to floor rather than round to nearest. Rounding
            // can push a placement near the block end to numSamples, which then
            // needs clamping, and the clamp is not translation-invariant: in a
            // one-sample block every placement clamps to 0 while the same
            // placement in a large block lands a sample later, so the step
            // sequence would depend on the host's buffer size. floor is exactly
            // translation-invariant for integer shifts — floor(x + n) ==
            // floor(x) + n — which is what makes any partition agree. The cost
            // is that a step fires on the sample it has reached rather than the
            // nearest one: at most one sample early, consistently.
            // Division, deliberately, not a hoisted reciprocal multiply: the
            // two are not bit-identical, and a reciprocal can flip this floor at
            // a boundary — which would break the partition stability the comment
            // above is built on. It runs only on emitted steps, roughly 0.09
            // times per block, so there is nothing to win anyway.
            const auto offset = static_cast<int> (
                std::floor ((placement - startInSteps) / stepsPerSample));

            jassert (juce::isPositiveAndBelow (offset, numSamples));

            const auto windowed = static_cast<int> (((step % window) + window) % window);
            listener.stepTriggered ({ windowed, span.sampleOffset + offset });
            lastEmittedStep = windowed;
        }

        ++step;
    }
}

} // namespace forrobox
