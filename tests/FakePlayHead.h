/* ============================================================================
   FORRÓ BOX — scriptable playhead

   Host sync is entirely testable offline: AudioProcessor::setPlayHead is
   virtual and PositionInfo has a setter for every field, so a test can describe
   a host timeline — tempo, transport, bar starts, loops, jumps — with no DAW
   and no audio device.

   Every PositionInfo field is Optional and real hosts populate them
   inconsistently, so this deliberately makes each one independently omittable:
   a plugin that only works when the host fills in everything works in almost no
   hosts.
============================================================================ */
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace fbtest
{

class FakePlayHead final : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override
    {
        ++positionQueries;

        if (! providePosition)
            return juce::nullopt;

        PositionInfo info;
        info.setIsPlaying (hostPlaying);
        info.setIsLooping (hostLooping);

        if (providePpq)
            info.setPpqPosition (ppq);

        if (provideBpm)
            info.setBpm (bpm);

        if (provideBarStart)
            info.setPpqPositionOfLastBarStart (lastBarStartPpq);

        if (provideTimeSignature)
            info.setTimeSignature (TimeSignature { numerator, denominator });

        if (provideBarCount)
            info.setBarCount (barCount);

        if (provideLoopPoints)
            info.setLoopPoints (LoopPoints { loopStartPpq, loopEndPpq });

        return info;
    }

    /** Moves the host forward by `numSamples` at its ACTUAL tempo, keeping the
        bar anchor consistent for the given meter.

        `actualBpmFactor` lets a test make the host advance at a tempo other
        than the one it reports. Real hosts do this whenever tempo is automated:
        PositionInfo carries one bpm for the block while the position advances
        along the real curve. A factor of 1.0 is the well-behaved host. */
    void advance (int numSamples, double sampleRate)
    {
        ppq += (static_cast<double> (numSamples) * bpm * actualBpmFactor) / (sampleRate * 60.0);

        // Wrap like a looping host: the position comes back inside the loop,
        // which is what makes the wrap land mid-block for most block sizes.
        if (hostLooping && provideLoopPoints && loopEndPpq > loopStartPpq)
            while (ppq >= loopEndPpq)
                ppq -= (loopEndPpq - loopStartPpq);

        lastBarStartPpq = std::floor (ppq / beatsPerBar()) * beatsPerBar();
        barCount = static_cast<juce::int64> (std::floor (ppq / beatsPerBar()));
    }

    /** Puts the host at a bar boundary. */
    void seekToBar (int bar)
    {
        ppq = static_cast<double> (bar) * beatsPerBar();
        lastBarStartPpq = ppq;
        barCount = bar;
    }

    double beatsPerBar() const
    {
        return static_cast<double> (numerator) * 4.0 / static_cast<double> (denominator);
    }

    int  queryCount() const noexcept { return positionQueries; }
    void resetQueryCount() noexcept  { positionQueries = 0; }

    // The timeline the test is describing.
    double ppq { 0.0 };
    double bpm { 120.0 };
    double lastBarStartPpq { 0.0 };
    bool   hostPlaying { true };
    bool   hostLooping { false };
    double actualBpmFactor { 1.0 };
    double loopStartPpq { 0.0 };
    double loopEndPpq { 4.0 };
    int    numerator { 4 };
    int    denominator { 4 };
    juce::int64 barCount { 0 };

    // Which fields this "host" bothers to report.
    bool providePosition { true };
    bool providePpq { true };
    bool provideBpm { true };
    bool provideBarStart { true };
    bool provideTimeSignature { true };
    bool provideBarCount { false };   // many hosts omit it
    bool provideLoopPoints { false }; // ditto

private:
    mutable int positionQueries { 0 };
};

} // namespace fbtest
