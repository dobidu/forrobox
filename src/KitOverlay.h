/* ============================================================================
   FORRÓ BOX — KitOverlay

   The bateria kit panel: BB / CX / HH / TOM, each on its own row of pads.

   WHY IT EXISTS. 05-01 made the BATERIA row show `max(bb, cx, hh, tom)` and
   write CAIXA alone, because `app.js:389` says so in its own comment —
   "collapsed row edits caixa (cx) — the backbeat; deep edits live in the kit
   view". That shipped three plans ago with no kit view to defer to, so four of
   the eight lanes have been visible and unreachable. This is the view.

   COVERS THE WHOLE CHASSIS. `PLANNING.md:519` says the backdrop covers "the
   matrix + side panel area"; `app.js:37` appends the subview to `#fb-window`
   and `css:554` is `position: absolute; inset: 0`. Where the prose and the
   design source disagree the source wins — the ruling 04-03 made for
   `.pad.beat`'s duplicate declaration.

   NO BACKDROP BLUR. `css:556` asks for `backdrop-filter: blur(3px)`, which JUCE
   has no equivalent for short of capturing the region behind and blurring it.
   Decided with the user at planning: the `--bg` 78% scrim the same rule
   specifies does the separation on its own. The divergence is recorded at the
   scrim rather than left to be rediscovered.
============================================================================ */
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Button.h"
#include "LookAndFeel.h"
#include "PatternPads.h"
#include "StepPad.h"
#include "Surface.h"

class ForroBoxAudioProcessor;

namespace forrobox
{

namespace kit
{
/// `background: color-mix(in srgb, var(--bg) 78%, transparent)` — css:555.
inline constexpr float kScrimOpacity = 0.78f;

/// `width: 620px` — css:561.
inline constexpr int kPanelWidth = 620;

/// `padding: 18px 20px` — css:563.
inline constexpr int kPanelPadY = 18;
inline constexpr int kPanelPadX = 20;

/// `border-left: 1px solid var(--line-strong)` — css:561.
inline constexpr int kPanelBorder = 1;

/// `box-shadow: -20px 0 60px rgba(0,0,0,0.4)` — css:562.
inline constexpr int kPanelShadowRadius = 60;
inline constexpr float kPanelShadowOpacity = 0.4f;

/// `.subview-head { margin-bottom: 6px }` — css:568.
inline constexpr int kHeadMarginBottom = 6;

/// `.subclose { width: 26px; height: 26px }` — css:573.
inline constexpr int kCloseSize = 26;

/// `.subview-sub { margin-bottom: 16px }` — css:571.
inline constexpr int kSubLineMarginBottom = 16;

/// `.sub-rows { gap: 10px }` — css:578.
inline constexpr int kRowGap = 10;

/// `.sub-row { grid-template-columns: 120px 1fr; gap: 12px }` — css:579.
inline constexpr int kRowLabelWidth = 120;
inline constexpr int kRowLabelGap = 12;

/// `.sub-rowlabel { gap: 1px }` — css:580.
inline constexpr int kRowLabelLineGap = 1;

/// `.sub-row .pads .pad { height: 26px }` — css:583. TALLER than the grid's.
inline constexpr int kPadHeight = 26;

/// `box-shadow: -20px 0 60px` — css:562. The X OFFSET, which was the one shadow
/// number at the call site as a bare literal while its radius and its alpha were
/// both enrolled in the cross-check. /simplify.
inline constexpr int kPanelShadowOffsetX = -20;

/// `transform: translateX(24px)` at rest — css:564.
inline constexpr int kEntranceOffset = 24;

/// `transition: … 0.2s` — css:565.
inline constexpr double kEntranceSeconds = 0.2;

/// `cubic-bezier(.2,.7,.3,1)` — css:565. The four control points, so the curve
/// is evaluated from the spec's own numbers rather than approximated.
///
/// One declarator each, because the cross-check reads `constexpr <type> <name> =
/// <value>;` and a comma-separated line is invisible to it — the constants were
/// enrolled and silently unfound.
inline constexpr double kEaseX1 = 0.2;
inline constexpr double kEaseY1 = 0.7;
inline constexpr double kEaseX2 = 0.3;
inline constexpr double kEaseY2 = 1.0;
} // namespace kit

/** The kit's four lanes, in the order the overlay shows them.

    Resolved BY NAME, which is what makes the row -> name -> colour binding safe.
    The rows used to be bound positionally: the short code came from
    `ids::lanes[entries[row]]` while the full name and the colour came from
    `row`, and those agreed only because the kit lanes happen to sit last in
    `ids::lanes` in this order. Reordering that tail to cx, bb, … would have
    drawn "CX" over "Bumbo" in bumbo-red, and nothing would have caught it —
    Task 1's own instruction said to resolve by name and the code did not.
    Found by /code-review. */
inline constexpr std::array<const char*, 4> kitLaneIds { "bb", "cx", "hh", "tom" };

/** Every box the overlay reserves, derived once from the chassis bounds. */
struct KitOverlayLayout
{
    juce::Rectangle<int> scrim;      ///< the whole chassis
    juce::Rectangle<int> panel;      ///< 620 wide, right-aligned
    juce::Rectangle<int> border;     ///< the panel's 1 px left edge
    juce::Rectangle<int> content;    ///< panel minus its padding

    juce::Rectangle<int> head;
    juce::Rectangle<int> title;
    juce::Rectangle<int> close;
    juce::Rectangle<int> subLine;

    struct Row
    {
        juce::Rectangle<int> bounds;
        juce::Rectangle<int> label;
        juce::Rectangle<int> name;    ///< 12 px, the short code
        juce::Rectangle<int> full;    ///< 9 px, the Portuguese name
        juce::Rectangle<int> pads;
    };

    std::array<Row, kitLaneIds.size()> rows;

    static KitOverlayLayout forBounds (juce::Rectangle<int> chassis) noexcept;
};

/** The overlay's explanatory line — `app.js:466`, verbatim.

    Brazilian Portuguese, as every instructional string in this plugin is, and
    a function rather than a literal at the paint site for the reason
    `Chassis::subDotsLabel` is one: a string with an accent in it should be
    spelled once. */
const juce::String& subLineText();

/** A kit piece's full Portuguese name — Bumbo, Caixa, Chimbal, Surdo.

    From `data.js:25-28`, in lane order. The SHORT codes are not repeated here:
    those are `ids::lanes`, and duplicating them would be a second list a
    reordered lane table could invalidate. */
juce::String kitPieceName (int index);

/** The easing `css:565` names, evaluated rather than approximated.

    Public so a test can check it against the control points instead of against
    whatever the implementation happens to produce. A `smoothstep` would look
    plausible and be a different curve. */
double cubicBezierEase (double t) noexcept;

class KitOverlay final : public juce::Component
{
public:
    explicit KitOverlay (ForroBoxLookAndFeel&);
    ~KitOverlay() override;

    void attachParameters (juce::AudioProcessorValueTreeState&);

    /** Open or close. Opening starts the entrance from zero. */
    void setOpen (bool shouldBeOpen);

    /** One tick of the overlay's own poll, for the tests.

        CALLED, never waited for — 04-04's lesson, where three checks failed on
        MSVC's clock rather than on the code. */
    void pollForTest() { poll(); }

    /** Whether the entrance/follow poll is running. */
    bool isPolling() const noexcept { return entrancePoll.isTimerRunning(); }

    /** Advance the entrance by elapsed SECONDS it is TOLD.

        Never reads a clock — `GainReductionMeter` and `HitVisualiser` carry the
        same law and the same reason: three 04-04 checks failed on MSVC's clock
        rather than on the code. A test drives this to any point without
        waiting. */
    void advanceEntrance (double seconds) noexcept;

    /** 0 at the start of the entrance, 1 at rest. */
    double getEntranceProgress() const noexcept { return progress; }

    /** Repopulate the pads from the stored pattern. Called, never waited for. */
    void refreshFromState();

    /** Follow writers other than this overlay, while it is open.

        The same law `SequencerGrid::refreshIfStateChanged` carries and for the
        same reason: Task 1 shipped an overlay that read the pattern once at
        open and never again, so a host recall, a profile load or a click in the
        collapsed BATERIA row left the four kit rows showing what was there when
        the panel opened. Found by `/code-review`.

        A closed overlay does nothing — `setOpen` rebuilds and refreshes on the
        way in, so a change made while it was shut is already accounted for. */
    void refreshIfStateChanged();

    const KitOverlayLayout& getLayout() const noexcept { return layout; }

    /** The pad at one kit row and step, or nullptr. */
    StepPad* padFor (int row, int step) const { return padGrid->padFor (row, step); }

    /** The publication these pads are showing — see PatternPads::getGeneration. */
    std::uint32_t getPadGeneration() const noexcept { return padGrid->getGeneration(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    /** The row table this overlay gives `PatternPads`: four rows, each reading
        and writing ONE of the lanes the composite BATERIA row covers. */
    std::vector<PatternRow> rowTable() const;

    /** How far the panel is pushed right, in pixels, at the current progress. */
    int entranceOffset() const noexcept;

    /** One tick: advance the entrance by real elapsed time, and follow the
        pattern. The CLOCK IS READ HERE and nowhere below it — `advanceEntrance`
        is told an interval, which is what lets a test drive it to any point. */
    void poll();

    /** Paint the panel's contents, in the panel's own coordinates. */
    void paintPanel (juce::Graphics&);

    /** Put the panel where the entrance says, at the alpha the entrance says. */
    void placePanel();

    /** `.subview-panel` — css:561, and a COMPONENT rather than a rectangle this
        class draws.

        The stylesheet gives the panel exactly two container properties,
        `opacity` and `transform: translateX`, and both were open-coded at every
        leaf: nine `.translated (shift, 0)` calls, eight `.withAlpha (eased)`
        calls, and a loop pushing the alpha onto each child. Every element ever
        added had to remember two invisible obligations, and one of them was
        forgotten within this plan — the pads and the close button did not fade,
        so at progress 0 a full-brightness kit floated over nothing.

        A child component carries both for its whole subtree: JUCE composites it
        through `beginTransparencyLayer` when its alpha is below 1, and its
        position IS the transform. The overlay is then only the scrim, which
        correctly does not fade, plus the drop shadow, which falls OUTSIDE the
        panel's bounds and so cannot be painted by it. /simplify. */
    struct Panel final : juce::Component
    {
        explicit Panel (KitOverlay&);
        void paint (juce::Graphics&) override;

        KitOverlay& owner;
    };

    std::unique_ptr<Panel> panel;

    ForroBoxLookAndFeel& lnf;
    KitOverlayLayout layout;

    ::ForroBoxAudioProcessor* processor { nullptr };

    std::unique_ptr<Button> closeButton;

    /** The pads, shared with the sequencer grid — see PatternPads.h. They are
        children of the PANEL, not of this: the panel is what fades and slides.

        Assigned in the constructor and never reset, so it is never null and
        nothing here checks it. Three call sites used to, while `resized()` —
        reachable from the same `onRebuilt` callback — dereferenced it twice
        without a check: guards that could not fire, next to the one place that
        would have needed one, which reads as a missing check rather than an
        impossible state. /code-review. */
    std::unique_ptr<PatternPads> padGrid;

    double progress { 1.0 };

    /** 60 Hz while OPEN, and stopped otherwise.

        The overlay's own, not the chassis's. `HeaderBar`, `FooterBar`,
        `SequencerGrid` and `Chassis` each own a `PollTimer` — `Surface.h`'s own
        doc names this as the third of them — and this was the one component
        whose tick lived in its parent, which cost `Chassis` a member meaning
        "when another component's animation last ticked" and a call placed
        deliberately above `pollVisualisers`'s own early return, with a comment
        explaining the exception. Started on open and stopped on close, so a shut
        panel does no work at all rather than 60 wake-ups a second for the
        plugin's life. /simplify. */
    PollTimer entrancePoll;

    /** When the previous tick ran, in seconds, or 0 before the first. Real
        elapsed time rather than the nominal 1/60 s: a throttled message thread
        drops ticks, and counting them would stretch a 0.2 s animation. */
    double lastPollSeconds { 0.0 };


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KitOverlay)
};

} // namespace forrobox
