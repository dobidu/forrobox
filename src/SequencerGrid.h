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
#include "ChoiceButtonsAttachment.h"
#include "StepPad.h"

namespace forrobox { class Playhead; }
#include "Surface.h"
#include "Typography.h"
#include "VoiceEngine.h"

#include <array>
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
/** 60 fps — the sweep is the only thing here that must be SMOOTH rather than
    merely current, so it polls faster than the header's and footer's 30 Hz. */
inline constexpr int kPlayheadPollHz = 60;

inline constexpr int kToggleOnVelocity = 100;
inline constexpr int kToggleOffVelocity = 0;
} // namespace seq

/** Which lanes one grid row covers.

    Four of the eight lanes share the BATERIA channel, so its row covers four and
    every other row covers one. DERIVED rather than written down, for the reason
    `VoiceEngine::channelForLane` and `ghostingKitLane` are: a table saying
    "bateria is lanes 4-7" would be a second copy that a reordered lane list could
    silently invalidate.

    A lookup into `detail::channelToLanes`, which is built once at compile time
    from the same two id lists the forward map uses. Returns a reference: there
    is nothing to construct. */
using LaneSet = detail::LaneCover;

const LaneSet& lanesForRow (int channelIndex);

/** The lane a click on one row WRITES.

    A row covering one lane writes that lane. The composite BATERIA row writes
    CAIXA alone — `app.js:389` records why in its own comment: "collapsed row
    edits caixa (cx) — the backbeat; deep edits live in the kit view". Writing all
    four would make one click destroy a pattern.

    Caixa is found by NAME, never by index, the way `ghostingKitLane` finds the
    hi-hat.

    Returns -1 for a row covering no lanes, which every caller already rejects
    through its bounds guard. A row like that cannot exist while
    `compositeChannel()` returns the FIRST lane-less channel, but returning lane
    0 for it — the old fallback — would have made a second lane-less channel edit
    ZABUMBA on every click instead of doing nothing. Found by /code-review. */
int writeLaneForRow (int channelIndex);

/** What one row DISPLAYS: the maximum velocity across the lanes it covers.

    `app.js:365-371` — four lanes collapse into one row, so the row lights if any
    of them does.

    Takes the row's lane cover rather than deriving it, so a refresh derives once
    per row instead of once per cell — see `LaneSet`. */
int displayedVelocity (const State& state, const LaneSet& covered, int step);

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

    /** The rows block the playhead sweeps: the first row's top to the last
        row's bottom. One rectangle, because the line spans all five rows — the
        prototype appends it to `seqWrap`, the container of every row, not to a
        row (`app.js:339`). */
    juce::Rectangle<int> rowsArea() const noexcept;

    /** Where the playhead is, for the tests. Empty when it is hidden. */
    juce::Rectangle<int> playheadBounds() const noexcept;

    /** Follow writers other than this grid.

        Reads the processor's pattern publication count and the step parameter,
        and refreshes or rebuilds when either has moved. 05-01 shipped a grid
        whose only writer was itself: a host recalling a project replaced the
        lanes and the editor kept showing the previous pattern until the user
        clicked a pad, and a STEPS automation left steps 16-31 invisible behind a
        clock already playing them. Both found by `/code-review` at 05-01.

        CALLED, never waited for — the poll drives it in the plugin and the tests
        drive it directly. */
    void refreshIfStateChanged();

    /** Read the processor's published position and move the line.

        Public and CALLED, never waited for — 04-04's lesson, where three checks
        failed on MSVC's clock rather than on the code. The poll drives it in the
        plugin; the tests drive it directly. */
    void updatePlayhead();

    /** The pad at one cell, or nullptr if the grid has none there.

        Public for the tests, which were finding a pad by scanning every child
        and comparing the CENTRE of a recomputed cell rectangle — asserting pad
        identity through derived floating-point geometry, in a helper copied
        verbatim into two tests. Found by /simplify. */
    StepPad* padFor (int row, int step) const;

    /** The active step window, from `ids::steps`. ONE reader, so the poll and
        the rebuild cannot disagree about how many pads there should be. */
    int readStepCount() const;

    /** Visually isolate one row, or -1 for none — `PLANNING.md:591`.

        VISUAL ONLY, and deliberately not in `State` and not a parameter: the
        spec calls it "a focus aid for editing", it does not affect audio, and a
        host recalling a project should not recall which row someone was
        squinting at. `app.js:22` keeps it in the view's own `state.isolated`
        beside `bateriaOpen` for the same reason, next to the channel data
        rather than in it.

        Setting the row that is already isolated CLEARS it — `app.js:509`,
        "state.isolated === iid ? null : iid". Only one row at a time. */
    void setIsolatedRow (int row);
    int  getIsolatedRow() const noexcept { return isolatedRow; }

    /** Whether a row is currently dimmed, for the tests and for the label
        painter. `app.js:517-518` — dim when the mute/solo gate silences the
        channel OR an isolate is active on a DIFFERENT row. Either, not both:
        an isolated row that is also muted is still dimmed. */
    bool isRowDimmed (int row) const;

    /** Pull the mute/solo gate and the isolate into the pads and the labels.

        Driven from the same 60 Hz poll everything else here is, so mute, solo
        and a profile load reach the grid without a listener. Public and CALLED
        rather than waited for — 04-04's lesson. */
    void refreshRowStates();

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    const SequencerLayout& getLayout() const noexcept { return layout; }

private:
    /** The row whose LABEL box contains a point, or -1.

        The label box, not the row: `css:460` binds the isolate to
        `.seq-rowlabel`, and the pads are children that take their own clicks
        anyway. */
    int rowLabelAt (juce::Point<int>) const;

    void paintHeadRow (juce::Graphics&, juce::Rectangle<int> clip) const;
    void paintRowLabels (juce::Graphics&, juce::Rectangle<int> clip) const;

    void rebuildPads();
    void toggleCell (int row, int step);

    ForroBoxLookAndFeel& lnf;
    SequencerLayout layout;

    /** The pads, row-major and DENSE: `row * stepCount + step`.

        `Chassis::PlacedKnob` stores its cell instead, because its pool skips —
        `Chassis.cpp:487` continues past a channel with no parameter, and one
        skipped slot would shift every later knob into the wrong strip.
        `rebuildPads` cannot skip: it is an unconditional nested loop over every
        row and every step. Carrying the coordinates anyway meant a flat list
        that three call sites then had to un-flatten, one of them with a memo and
        a bounds guard that existed only to protect the memo. Found by /simplify. */
    std::vector<std::unique_ptr<StepPad>> pads;

    int stepCount { 0 };

    /** The isolate and what each row is currently showing.

        `rowDimmed` is not derivable at paint time without re-resolving the
        mute/solo gate, and it is what `refreshRowStates` edge-detects against so
        a 60 Hz poll costs five compares rather than 160 `setDimmed` calls. */
    int isolatedRow { -1 };
    int hoveredLabelRow { -1 };
    std::array<bool, static_cast<size_t> (ChassisLayout::kNumStrips)> rowDimmed {};

    std::unique_ptr<Playhead> playhead;

    /** The STEPS 16/32 buttons, in the boxes 05-01 reserved and left empty.

        `Variant::base`: `.steps-btn` has NO rule of its own in the stylesheet —
        the markup is `class="btn steps-btn"` (app.js:321), so they are plain
        `.btn`s with `.on` (css:131, css:143). Inventing a variant for a class
        that adds nothing would be a second answer to a question the stylesheet
        already answers. */
    std::array<std::unique_ptr<Button>, forrobox::ids::stepWindows.size()> stepButtons;
    std::unique_ptr<ChoiceButtonsAttachment> stepsAttachment;

    /** The pattern publication this grid is currently showing. Recorded after
        every refresh, including the one `toggleCell` does itself, so the grid's
        own edit does not come back around as a second refresh. */
    std::uint32_t lastPatternGeneration { 0 };

    /** 60 fps. The sweep is the only thing in this plugin that has to be smooth
        rather than merely current, so it polls faster than the header's and
        footer's 30 Hz. */
    PollTimer playheadPoll;

    // Global scope, not forrobox:: — a forward declaration inside this namespace
    // would name a different, incomplete type.
    ::ForroBoxAudioProcessor* processor { nullptr };
    juce::AudioProcessorValueTreeState* apvts { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SequencerGrid)
};

} // namespace forrobox
