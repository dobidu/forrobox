#include "Clock.h"

#include <cmath>

namespace forrobox
{

void Clock::advance (double startInSteps,
                     double stepsPerSample,
                     int numSamples,
                     const Params& params,
                     StepListener& listener) noexcept
{
    // Non-finite values are rejected explicitly. NaN fails every comparison, so
    // a `rate <= 0.0` test would NOT catch it: the emission loop below would
    // never terminate, hanging the audio thread.
    if (numSamples <= 0
        || ! std::isfinite (startInSteps)
        || ! std::isfinite (stepsPerSample)
        || ! (stepsPerSample > 0.0))
        return;

    const double endInSteps = startInSteps + stepsPerSample * static_cast<double> (numSamples);

    if (! std::isfinite (endInSteps))
        return;

    // juce::jlimit cannot sanitise NaN either — both comparisons are false, so
    // it returns the NaN unchanged.
    const auto rawSwing = static_cast<double> (params.swing);
    const auto swing    = juce::jlimit (0.0, 100.0, std::isfinite (rawSwing) ? rawSwing : 0.0);
    const auto window   = juce::jlimit (1, State::kMaxSteps, params.activeSteps);

    // In position space a step is exactly 1.0 long, so the swing offset is a
    // plain fraction of a step and needs no tempo to express.
    const double swingSteps = swing * 0.01 * kMaxSwingFraction;

    // A swung step moves by at most 0.6 of a step, so placement is strictly
    // increasing in the step index and the first candidate cannot be more than
    // one step behind the span start.
    auto step = static_cast<long long> (std::floor (startInSteps)) - 1;

    for (;;)
    {
        const auto windowed = static_cast<int> (((step % window) + window) % window);

        // Swing offsets an odd step's PLACEMENT only; the grid it sits on never
        // moves, which is why swing does not accumulate — the structure of
        // app.js:637, not its scheduler. Parity is taken on the windowed index
        // to match the prototype exactly; the two agree because the window is
        // always even.
        const double placement = static_cast<double> (step)
                               + (windowed % 2 == 1 ? swingSteps : 0.0);

        if (placement >= endInSteps)
            break;

        // Membership is decided in POSITION space, not on the rounded sample
        // offset. Contiguous spans tile the timeline exactly, so every placement
        // belongs to exactly one span — deciding on the rounded offset instead
        // would let a placement near a block edge be rejected by both the span
        // that contains it and the next one, dropping the step.
        if (placement >= startInSteps)
        {
            // Truncating floor, deliberately, and with NO clamp.
            //
            // Membership was decided in position space, so placement is in
            // [start, end) and therefore (placement - start) * samplesPerStep is
            // in [0, numSamples): the floor of that is provably a valid offset.
            // That is the whole reason to floor rather than round to nearest —
            // rounding can push a placement near the block end to numSamples,
            // which then needs clamping, and the clamp is not
            // translation-invariant. In a one-sample block every placement
            // clamps to 0 while the same placement in a large block lands a
            // sample later, so the step sequence would depend on the host's
            // buffer size. 02-02 hit the same wall from the other direction.
            //
            // floor itself IS translation-invariant for integer shifts —
            // floor(x + n) == floor(x) + n — which is what makes any partition
            // of the same span agree. The cost is that a step fires on the
            // sample it has reached rather than the nearest one: at most one
            // sample early, consistently.
            const auto offset = static_cast<int> (
                std::floor ((placement - startInSteps) / stepsPerSample));

            jassert (juce::isPositiveAndBelow (offset, numSamples));
            listener.stepTriggered ({ windowed, juce::jmax (0, offset) });
            lastEmittedStep = windowed;
        }

        ++step;
    }
}

} // namespace forrobox
