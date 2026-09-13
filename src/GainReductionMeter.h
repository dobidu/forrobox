/* ============================================================================
   FORRÓ BOX — the gain-reduction meter

   56x6, fully rounded, `--screen` ground with a 1 px `--line` border and a
   `--danger` fill that grows RIGHT to LEFT (css:513-518).

   It holds a DISPLAYED reduction, not the last one it was handed, and it is
   told how much time passed rather than asking a clock — so a test drives the
   decay exactly and no check waits on a wall clock. That is 04-04's lesson
   stated as an interface: the header's tests pumped a real message loop hoping a
   30 Hz timer ticked inside it, which it did on GCC and Clang and did not on
   MSVC.
============================================================================ */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "LookAndFeel.h"
#include "MixBus.h"

namespace forrobox
{

namespace grmeter
{
inline constexpr int kWidth  = 56;   ///< css:513 .gr-meter
inline constexpr int kHeight = 6;
inline constexpr int kBorder = 1;    ///< css:514 `border: 1px solid var(--line)`

/** `transition: width 0.06s linear` — css:518.

    The FALLING edge only. Every read of the source is a peak since the last
    read (see MixBus::takeGainReductionDb), so a rise must be shown at once or
    the peak the meter exists to display is the thing it smooths away. 60 ms is
    how long a fall from full scale to empty takes. */
inline constexpr float kDecaySeconds = 0.06f;

/** Full scale. The limiter's own threshold, so the meter is full exactly when
    the loudest sample was pushed from 0 dBFS down to the threshold — asked of
    MixBus rather than picked, because a meter with a made-up range reads as
    precise while meaning nothing. */
inline constexpr float kRangeDb = -kLimiterThresholdDb;
} // namespace grmeter

class GainReductionMeter final : public juce::Component
{
public:
    explicit GainReductionMeter (ForroBoxLookAndFeel&);

    /** Feed it one reading and say how long it has been since the last one.

        `seconds` is passed rather than measured so the decay is a pure function
        of its arguments. The poll passes its own interval; a test passes
        whatever it wants to prove. */
    void setReductionDb (float reductionDb, float seconds);

    /** What is DRAWN, which is not what was last fed in. */
    float getDisplayedDb() const noexcept { return displayedDb; }

    /** 0..1 of the track, left to right, for a test that wants the law rather
        than a pixel. */
    float displayedProportion() const noexcept;

    void paint (juce::Graphics&) override;

private:
    ForroBoxLookAndFeel& lnf;
    float displayedDb { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GainReductionMeter)
};

} // namespace forrobox
