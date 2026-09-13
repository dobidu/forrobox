/* ============================================================================
   FORRÓ BOX — the footer bar

   The 56 px row across the bottom: MASTER and its 120 px fader, LIMITER with a
   live gain-reduction meter, the DRAG MIDI call to action, and the OUTPUT
   toggle.

   Its own component from the start, which is the whole reason 04-05 opened by
   taking the header out of Chassis: two build/paint/refresh/resize triads in one
   class was a coincidence, and this was the third.

   Unlike the header, the footer's geometry lives HERE. `ChassisLayout` computes
   the header's because it predates the split and its tests read it; nothing
   outside this file needs the footer's boxes, so `FooterLayout` is derived from
   this component's own bounds and `verify-geometry.py` reads the constants from
   this header.
============================================================================ */
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Button.h"
#include "Chassis.h"
#include "ChoiceAttachment.h"
#include "Fader.h"
#include "DragMidiButton.h"
#include "GainReductionMeter.h"
#include "LookAndFeel.h"
#include "ProportionAttachment.h"
#include "Segmented.h"
#include "ToggleAttachment.h"
#include "Typography.h"

class ForroBoxAudioProcessor;

namespace forrobox
{

namespace footer
{
inline constexpr int kPadX = 18;       ///< css:507 .footer padding 0 18px
inline constexpr int kGap  = 20;       ///< css:507 the gap BETWEEN groups
inline constexpr int kGroupGap = 9;    ///< css:510 .foot-group gap

inline constexpr int kMasterFaderWidth = 120;   ///< css:512 .master-fader

/// `.drag-midi` is `margin: 0 auto` and `.out-toggle`'s group `margin-left: auto`
/// — THREE auto margins in one flex row (css:519 and app.js:426).
inline constexpr int kNumAutoMargins = 3;
} // namespace footer

/** Every box the footer reserves, derived once from the row.

    The whole row, not just what this plan fills — `StripLayout`'s rule, and for
    the reason 04-01 learned by discarding the strip's content rect. DRAG MIDI
    and OUTPUT are reserved here and filled in the same plan's third task; the
    auto-margin distribution below cannot be computed without all four widths. */
struct FooterLayout
{
    juce::Rectangle<int> masterLabel, masterFader;
    juce::Rectangle<int> limiterButton, grMeter;
    juce::Rectangle<int> dragMidi;
    juce::Rectangle<int> outputLabel, outputToggle;

    /** The four flex groups, in order, so a test can assert the auto margins
        without re-deriving them from the boxes inside. */
    juce::Rectangle<int> masterGroup, limiterGroup, outputGroup;

    static FooterLayout forBounds (juce::Rectangle<int>) noexcept;
};

/** The OUTPUT toggle's two labels. One table, read by the layout and by the
    control — `ChassisLayout::profileCodes`' rule. */
const juce::StringArray& outputModeLabels();

class FooterBar final : public juce::Component
{
public:
    explicit FooterBar (ForroBoxLookAndFeel&);
    ~FooterBar() override;

    void attachParameters (juce::AudioProcessorValueTreeState&);

    /** Pull the footer into step with the processor.

        Today that is the gain-reduction meter and nothing else — MASTER and
        LIMITER have attachments. Public for the reason
        `HeaderBar::refreshFromProcessor` is: the timer is scheduling, the
        refresh is the behaviour, and a poll reachable only through a timer can
        only be tested flakily.

        `seconds` is how long to advance the meter's decay by. The poll passes
        its own interval; a test passes what it wants to prove, so no check
        waits on a wall clock. */
    void refreshFromProcessor (float seconds);

    void paint (juce::Graphics&) override;
    void resized() override;

    const FooterLayout& getLayout() const noexcept { return layout; }

    /** The poll's interval, in seconds — what the timer hands the refresh. */
    static constexpr int kFooterPollHz = 30;
    static constexpr float kPollSeconds = 1.0f / static_cast<float> (kFooterPollHz);

private:
    void buildFooterControls (juce::AudioProcessorValueTreeState&);
    void paintFooterText (juce::Graphics&) const;

    ForroBoxLookAndFeel& lnf;
    FooterLayout layout;

    /** The footer's controls.

        Each attachment declared AFTER the control it binds, and each holding it
        through a SafePointer — the ordering covers destruction and the
        SafePointer covers assignment, which runs the other way. 04-03 and 04-04
        each shipped a heap-use-after-free on that difference. */
    struct FooterControls
    {
        std::unique_ptr<Fader>  master;
        std::unique_ptr<Button> limiter;
        std::unique_ptr<GainReductionMeter> grMeter;
        std::unique_ptr<DragMidiButton> dragMidi;          ///< STUB until Phase 7
        std::unique_ptr<Segmented> output;                 ///< READ-ONLY until 04-06

        std::unique_ptr<ProportionAttachment<Fader>> masterAttachment;
        std::unique_ptr<ToggleAttachment>            limiterAttachment;

        /** Display only, and OUTPUT is read-only — but a read-only control
            still has to FOLLOW its parameter. Without this the segment read
            `output_mode` once at build time and then lit the wrong one for
            every host automation, project reload and undo. */
        std::unique_ptr<ChoiceAttachment>            outputAttachment;
    };

    FooterControls footerControls;

    struct FooterPoll final : juce::Timer
    {
        void timerCallback() override { if (tick != nullptr) tick(); }
        std::function<void()> tick;
    };

    FooterPoll footerPoll;

    // Global scope, not forrobox:: — a forward declaration inside this
    // namespace would name a different, incomplete type.
    ::ForroBoxAudioProcessor* polledProcessor { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FooterBar)
};

} // namespace forrobox
