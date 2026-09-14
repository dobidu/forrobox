/* ============================================================================
   FORRÓ BOX — the sequencer grid

   The 196 px row across the bottom of the main area: a head row, then five rows
   of step pads that show the pattern the audio thread plays and edit it on
   click.

   Its own component owning its own layout, exactly as HeaderBar and FooterBar
   do — and for the reason both recorded: `Component::getBounds` is
   parent-relative, and this region sits at y=528, so anything comparing a
   child's bounds against a chassis-space rectangle repeats the bug that made two
   tests silently read the footer's control as the header's.
============================================================================ */
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Button.h"
#include "Chassis.h"
#include "ForroBoxState.h"
#include "LookAndFeel.h"
#include "StepPad.h"
#include "Surface.h"
#include "Typography.h"
#include "VoiceEngine.h"

#include <vector>

class ForroBoxAudioProcessor;

namespace forrobox
{

namespace seq
{
inline constexpr int kPadTop    = 12;   ///< css:446 .seq padding 12px 16px 14px
inline constexpr int kPadSide   = 16;
inline constexpr int kPadBottom = 14;

inline constexpr int kHeadGap          = 12;  ///< css:450 .sh-left gap
inline constexpr int kHeadMarginBottom = 11;  ///< css:449 .seq-head margin-bottom
inline constexpr int kStepsGap         = 6;   ///< css:501 .seq-len gap

inline constexpr int kLabelWidth = 92;  ///< css:452 .seq-row grid-template-columns
inline constexpr int kLabelGap   = 10;  ///< css:452 .seq-row gap, label -> pads

inline constexpr int kChipWidth  = 4;   ///< css:459 .rl-chip
inline constexpr int kChipHeight = 18;
inline constexpr int kChipRadius = 1;
inline constexpr int kChipGap    = 7;   ///< css:453 .seq-rowlabel gap

inline constexpr int kPadGap    = 5;    ///< css:463 .pads gap
inline constexpr int kPadHeight = 26;   ///< css:465 .pad height

/** The DECLARED gap between rows, which does not fit and is not what is drawn.

    css:451 says `gap: 7px`, and PLANNING.md:295 says the region is 196 px with
    26 px pads. Those three numbers are inconsistent by 21 px: the five rows need
    158 and the region leaves 137 once its padding, the head row and that row's
    11 px margin are taken.

    The browser resolves it and this reproduces the resolution rather than
    picking a number. `.seq-grid-wrap` is `flex: 1; min-height: 0` and the five
    `.seq-row` children have the default `flex-shrink: 1`, so the ROW BOXES
    compress while each `.pad` keeps its fixed 26 px and overflows its box. Row
    origins land ~28.4 px apart carrying 26 px pads, which reads as a gap of
    about 2 px rather than 7.

    So the PAD HEIGHT is the fixed number here and the gap is derived — see
    `rowGap`. Settled with the user at 05-01 planning; the alternatives were
    shrinking the pads to 21 or taking 21 px from the channel strips. */
inline constexpr int kDeclaredRowGap = 7;

/** The velocity a click writes, and the one it clears to — app.js:390-395.

    100, not 127: the prototype's `togglePad` writes 100, and a pad toggled on
    should look like the profiles' own mid-strong hits rather than the loudest
    value the format allows. */
inline constexpr int kToggleOnVelocity = 100;
inline constexpr int kToggleOffVelocity = 0;
} // namespace seq

/** Which lanes one grid row covers, derived from the lane -> channel map.

    Four of the eight lanes share the BATERIA channel, so its row covers four and
    every other row covers one. DERIVED rather than written down, for the reason
    `VoiceEngine::channelForLane` and `ghostingKitLane` are: a table saying
    "bateria is lanes 4-7" would be a second copy that a reordered lane list could
    silently invalidate. */
std::vector<int> lanesForRow (int channelIndex);

/** The lane a click on one row WRITES.

    A row covering one lane writes that lane. The composite BATERIA row writes
    CAIXA alone — `app.js:389` records why in its own comment: "collapsed row
    edits caixa (cx) — the backbeat; deep edits live in the kit view". Writing all
    four would make one click destroy a pattern.

    Caixa is found by NAME, never by index, the way `ghostingKitLane` finds the
    hi-hat. */
int writeLaneForRow (int channelIndex);

/** What one row DISPLAYS: the maximum velocity across the lanes it covers.

    `app.js:365-371` — four lanes collapse into one row, so the row lights if any
    of them does. */
int displayedVelocity (const State& state, int channelIndex, int step);

/** The isolate hint, `CLIQUE O NOME P/ ISOLAR` — app.js:319.

    Brazilian Portuguese, as every instructional string in this plugin is, and a
    juce::String from explicit UTF-8 so the ASCII constructor cannot mangle it. */
const juce::String& isolateHintText();

/** Every box the sequencer reserves, derived once from the region.

    The WHOLE stack, including what 05-02 and 05-03 fill — StripLayout's rule,
    and the one 04-01 learned by computing the strip's content rect and dropping
    it, which made its own "reserve their boxes" deliverable unreachable. */
struct SequencerLayout
{
    juce::Rectangle<int> head;
    juce::Rectangle<int> sectionLabel, isolateHint;
    juce::Rectangle<int> stepsLabel, steps16, steps32;

    /** The five rows, top to bottom, in `ids::channelInfos` order. */
    struct Row
    {
        juce::Rectangle<int> bounds;   ///< the whole row, for 05-03's dimming
        juce::Rectangle<int> label;    ///< chip + name, clickable in 05-03
        juce::Rectangle<int> chip;
        juce::Rectangle<int> name;
        juce::Rectangle<int> pads;     ///< the strip the pads tile
    };

    std::array<Row, static_cast<size_t> (ChassisLayout::kNumStrips)> rows;

    /** The box one pad occupies, `index` of `stepCount` across `pads`.

        The pads tile `pads` exactly: widths are taken from fractional edges and
        rounded, which distributes the remainder instead of accumulating it in
        the last pad — the rule `ChassisLayout::forBounds` uses for the five
        channel strips, and for the same reason. */
    static juce::Rectangle<int> padBounds (juce::Rectangle<int> pads, int index,
                                           int stepCount) noexcept;

    /** The gap actually drawn between rows, derived from what is left.

        Never `kDeclaredRowGap` — see that constant for why the stylesheet's 7
        does not fit. Clamped at zero so a future region change cannot produce a
        negative stride. */
    static int rowGap (int availableHeight) noexcept;

    static SequencerLayout forBounds (juce::Rectangle<int>) noexcept;
};

class SequencerGrid final : public juce::Component
{
public:
    explicit SequencerGrid (ForroBoxLookAndFeel&);
    ~SequencerGrid() override;

    void attachParameters (juce::AudioProcessorValueTreeState&);

    /** Pull every pad into step with the stored pattern.

        Public because the behaviour must be reachable without a timer — 04-04's
        lesson, where three checks failed on MSVC's clock rather than on the code.
        Called after an edit, and by Phase 6's reload. */
    void refreshFromState();

    /** How many steps the grid is showing, from `ids::steps`. */
    int getStepCount() const noexcept { return stepCount; }

    void paint (juce::Graphics&) override;
    void resized() override;

    const SequencerLayout& getLayout() const noexcept { return layout; }

private:
    void paintHeadRow (juce::Graphics&, juce::Rectangle<int> clip) const;
    void paintRowLabels (juce::Graphics&, juce::Rectangle<int> clip) const;

    void rebuildPads();
    void toggleCell (int row, int step);

    ForroBoxLookAndFeel& lnf;
    SequencerLayout layout;

    /** One placed pad, carrying WHICH cell it is.

        The cell is stored rather than derived from the pad's index, for the
        reason `Chassis::PlacedKnob` records: `i / n` and `i % n` assume a full
        rectangular pool in order, and a single skipped cell shifts every later
        pad into the wrong row. */
    struct PlacedPad
    {
        std::unique_ptr<StepPad> pad;
        int row { 0 };
        int step { 0 };
    };

    std::vector<PlacedPad> pads;

    int stepCount { 0 };

    // Global scope, not forrobox:: — a forward declaration inside this namespace
    // would name a different, incomplete type.
    ::ForroBoxAudioProcessor* processor { nullptr };
    juce::AudioProcessorValueTreeState* apvts { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SequencerGrid)
};

} // namespace forrobox
