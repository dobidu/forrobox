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

        return info;
    }

    /** Moves the host forward by `numSamples` at the current tempo, keeping the
        bar anchor consistent for the given meter. */
    void advance (int numSamples, double sampleRate)
    {
        ppq += (static_cast<double> (numSamples) * bpm) / (sampleRate * 60.0);

        const auto beatsPerBar = static_cast<double> (numerator) * 4.0 / static_cast<double> (denominator);
        lastBarStartPpq = std::floor (ppq / beatsPerBar) * beatsPerBar;
    }

    /** Puts the host at a bar boundary. */
    void seekToBar (int bar)
    {
        const auto beatsPerBar = static_cast<double> (numerator) * 4.0 / static_cast<double> (denominator);
        ppq = static_cast<double> (bar) * beatsPerBar;
        lastBarStartPpq = ppq;
    }

    int  queryCount() const noexcept { return positionQueries; }
    void resetQueryCount() noexcept  { positionQueries = 0; }

    // The timeline the test is describing.
    double ppq { 0.0 };
    double bpm { 120.0 };
    double lastBarStartPpq { 0.0 };
    bool   hostPlaying { true };
    bool   hostLooping { false };
    int    numerator { 4 };
    int    denominator { 4 };

    // Which fields this "host" bothers to report.
    bool providePosition { true };
    bool providePpq { true };
    bool provideBpm { true };
    bool provideBarStart { true };
    bool provideTimeSignature { true };

private:
    mutable int positionQueries { 0 };
};

} // namespace fbtest
