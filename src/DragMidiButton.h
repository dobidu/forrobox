/* ============================================================================
   FORRÓ BOX — DRAG MIDI

   The footer's primary call to action: `↓  DRAG MIDI  .mid` on a zabumba-tinted
   gradient with a 1.5 px accent border. css:520-543 and PLANNING.md:492-506.

   DRAGGING it hands the host a real `.mid` of the current groove through
   `juce::DragAndDropContainer::performExternalDragDropOfFiles`; CLICKING it
   opens an async save dialog for the same bytes. The button knows how to ASK for
   an export and nothing about the processor — `FooterBar` supplies the
   `std::function`.
============================================================================ */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Chassis.h"
#include "GrooveExport.h"
#include "LookAndFeel.h"
#include "Typography.h"

namespace forrobox
{

namespace dragmidi
{
inline constexpr int kPadX = 30;    ///< css:524 padding 9px 30px
inline constexpr int kPadY = 9;
inline constexpr int kGap  = 11;    ///< css:521
inline constexpr int kRadiusExtra = 2;   ///< css:524 `calc(var(--r) + 2px)`

/** `border: 1.5px` — css:523. The only fractional border in the design, and the
    reason this box is measured with a rounded total rather than through
    `type::boxHeight`, whose border argument is an int because every other
    border is one. */
inline constexpr float kBorder = 1.5f;

inline constexpr float kTintPct       = 14.0f;  ///< css:522 the gradient's top stop
inline constexpr float kHoverTintPct  = 26.0f;  ///< css:536
inline constexpr float kBorderPct     = 45.0f;  ///< css:523
inline constexpr float kRingPct       = 18.0f;  ///< css:526 `0 0 0 1px <18%>`
inline constexpr int   kRingWidth     = 1;
inline constexpr float kHoverRingWidth = 1.5f;  ///< css:537 `0 0 0 1.5px`
inline constexpr int   kHoverGlowRadius = 30;   ///< css:537 `0 0 30px`
inline constexpr float kHoverGlowPct  = 55.0f;
inline constexpr float kInsetAlpha    = 0.06f;  ///< css:526 `inset 0 1px 0 rgba(255,255,255,0.06)`
inline constexpr float kPressScale    = 0.98f;  ///< css:538

/** The glow `0 0 30px` reaches past the box, so the component reserves room for
    it the way Button, StepPad and Fader reserve theirs. Hover-only, but the
    margin is unconditional: bounds that changed on hover would move the control
    under the pointer. */
inline constexpr int kGlowMargin = kHoverGlowRadius;

/** How far the pointer must travel before a press becomes a file drag.

    JUCE's own default, the one `DragAndDropContainer::startDragging` uses. Not
    zero: `mouseDrag` fires on any pointer-STATE change while a button is held,
    and that state carries pressure and orientation, so a still press on a
    trackpad or a pen still delivers drags. */
inline constexpr int kDragThresholdPx = 8;

// ── the idle pulse — css:527-532 and css:539-541 ────────────────────────────
//
//  PLANNING.md:499 describes it as "breathing the glow between `0 0 0 1px
//  <zabumba 18%>` and `+ 0 0 20px <zabumba 28%>`", which reads as though only
//  the outer glow moves. THE STYLESHEET SAYS OTHERWISE: css:531 keeps the 1 px
//  ring and takes it from 18% to 35% across the same cycle. The design source
//  wins over PLANNING's prose — the ruling 04-03 made for `.pad.beat`'s
//  duplicate declaration — so the ring alpha animates too.

/** `animation: midipulse 2.6s ease-in-out infinite` — css:527. */
inline constexpr double kPulseSeconds = 2.6;

/** The ring at the top of the breath: 18% at rest, 35% at the peak — css:531. */
inline constexpr float kPulseRingPct = 35.0f;

/** `0 0 20px color-mix(--c-zabumba 28%)` at the peak, nothing at rest — css:531. */
inline constexpr int   kPulseGlowRadius = 20;
inline constexpr float kPulseGlowPct    = 28.0f;

/** `@keyframes midiarrow { 50% { transform: translateY(2px) } }` — css:540. */
inline constexpr int kArrowBobPx = 2;
} // namespace dragmidi

class DragMidiButton final : public juce::Component
{
public:
    /** The three runs, measured once, and the box they need.

        ASKED of the control, the way `Button::widthOf` and `Segmented::widthOf`
        are — not computed again by whoever reserves the box. `FooterBar` used to
        carry its own copy of this arithmetic in an anonymous namespace while
        `paint` re-derived the same three widths, and the two already rounded
        differently: the measurer rounded each run and the doubled border to int,
        the painter subtracted a raw 1.5f. Nothing compared them, so a changed
        gap or a fourth run would have moved the reserved box away from the drawn
        content in silence — 04-03's two short strip rows, one control over.
        Found by /simplify from three angles. */
    struct Metrics
    {
        int arrowWidth, labelWidth, subWidth;
        int width, height;
    };

    /** Measured from the type scale, so it costs three text layouts. Call it
        once per layout pass and keep the result — `paint` does. */
    static Metrics metrics();

    explicit DragMidiButton (ForroBoxLookAndFeel&);

    /** The component bounds that give the hover glow room to fall outside the
        box — `0 0 30px` reaches well past it, and a Component's paint is
        clipped to its own bounds.

        Button, StepPad and Fader each reserve their own margin the same way.
        The margin is UNCONDITIONAL even though only the hover state uses it:
        bounds that changed on hover would move the control out from under the
        pointer that is hovering it.

        MEASURED, because reserving is not the same as getting: JUCE clips a
        child to its own bounds intersected with its PARENT's, and the footer is
        56 px tall around a 37 px button — so the row allows 9 px above and 10 px
        below, and the other ~20 of the 30 is cut. The browser lets a box-shadow
        spill over the sequencer (`overflow: hidden` clips children, not
        shadows); this does not. Recorded rather than fixed: widening FooterBar
        past its own region to chase it would put the footer over the sequencer.
        Found by /code-review on 04-05, and the figure is asserted in the tests
        so it cannot drift silently. */
    static juce::Rectangle<int> boundsForBox (juce::Rectangle<int> box) noexcept
    {
        return box.expanded (dragmidi::kGlowMargin);
    }

    /** The button itself, inside its bounds — `boundsForBox`'s inverse. */
    juce::Rectangle<int> contentBox() const noexcept
    {
        return getLocalBounds().reduced (dragmidi::kGlowMargin);
    }

    bool isHovered() const noexcept { return hovered; }
    bool isPressed() const noexcept { return pressed; }

    /** Advance the breath by elapsed SECONDS it is TOLD.

        Never reads a clock. `KitOverlay::advanceEntrance`, `GainReductionMeter`
        and `HitVisualiser` all carry the same law and the same reason: three
        04-04 checks failed on MSVC's clock rather than on the code, and a test
        that cannot drive an animation to a chosen point has to wait for one.

        Does nothing while hovered or dragging. css:534 stops BOTH animations on
        hover; under `.hot` (the dragging class, `app.js:447`) the stylesheet
        stops only the box-shadow — `.drag-midi:hover .dm-arrow` is the arrow's
        only `animation: none`, so in the prototype the arrow keeps bobbing
        through a drag. We stop it, deliberately: a 2 px bob under a pointer
        that is dragging the control away reads as a stutter.

        The phase is HELD rather than reset, so releasing resumes the breath
        where it was instead of snapping it back to the start. */
    void advancePulse (double seconds) noexcept;

    /** 0 at the start of the cycle, 1 at the far end of the breath, 0 again at
        the close — the eased 0 → 1 → 0 the keyframes describe. */
    double pulseAmount() const noexcept;

    /** Whether the breath is running at all. False while hovered or dragging. */
    bool isPulsing() const noexcept { return ! hovered && ! dragging; }

    /** How to ask for the current groove. Supplied by whoever has a processor;
        this class deliberately has no idea where the bytes come from. */
    std::function<GrooveExport()> onExportRequested;

    /** Deletes every `.mid` in `folder` except `keep`.

        WHY NOT THE COMPLETION CALLBACK. `performExternalDragDropOfFiles` takes
        one, and deleting there looks tidiest — but it fires when the DRAG ends,
        which is not the instant the receiving application has finished reading
        the file. A host that copies lazily would find it gone, and the failure
        mode is a silently empty MIDI track with no error anywhere. So a dragged
        file is never deleted while it might still be read; the folder is swept
        on the NEXT export instead, and the OS clears its temp directory anyway.

        Takes the FOLDER so a test can drive it against a scratch directory. It
        deletes every `.mid` it finds, so pointing it at the shared temp folder
        from a test would destroy a file a real drag was still using. */
    static void sweepOldExports (const juce::File& folder, const juce::File& keep);

    void paint (juce::Graphics&) override;

    /** Keeps the pointer out of the reserved glow margin, so hovering the empty
        space beside the button does not light it. StepPad's rule. */
    bool hitTest (int x, int y) override { return contentBox().contains (x, y); }

    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    ForroBoxLookAndFeel& lnf;

    /** Measured ONCE, in the constructor. `type::trackedWidth` lays the string
        out, and `type::drawTracked` lays it out again to draw it — so measuring
        in `paint` shaped all three runs twice, 11.67 us of a 51 us idle paint.
        The type scale is constexpr, so these cannot change at runtime.
        `Segmented` caches its spans in its constructor for the same reason. */
    const Metrics box;

    /** The current groove, or an empty one when nothing is wired to ask. */
    GrooveExport askForExport() const;

    /** Writes the current groove into `instanceFolder` and sweeps older ones.
        Returns an empty File if there is nothing to ask or the write failed. */
    juce::File writeExportFile();

    /** Starts or stops the breath's tick to match `isPulsing()`. */
    void syncPulseTimer();

    bool hovered { false };
    bool pressed { false };

    /** Set when a drag starts, so `mouseUp` can tell a click from the release at
        the end of a drag — a drag is a press, a move and a release, and without
        this the drop would also open a save dialog. */
    bool dragging { false };

    /** Whether this gesture has already tried to export, whatever came of it.

        Separate from `dragging` because `dragging` is the VISUAL state: it stays
        false when the write fails and is cleared again when the OS refuses the
        drag, so using it as the latch re-ran the whole export on the next
        pointer event of the same gesture. */
    bool gestureTried { false };

    /** A MEMBER, not a local. `FileChooser::launchAsync` returns immediately and
        calls back later; a chooser declared on the stack in `mouseUp` is
        destroyed before its own callback runs, which is a use-after-free that
        usually appears to work.

        And never REPLACED while one is open. The platform Pimpl holds a
        `FileChooser&` back-reference and `~FileChooser` only nulls its callback,
        so overwriting a live chooser leaves a native dialog pointing at freed
        memory — JUCE asserts on it (`juce_FileChooser.cpp:196`). On a platform
        whose save panel is modeless, a second click would do exactly that. */
    std::unique_ptr<juce::FileChooser> chooser;

    /** Where THIS button's dragged files are written: a per-instance folder
        under `<temp>/forrobox`.

        A real path is unavoidable — a native drag hands the OS a FILENAME, not
        a buffer.

        PER INSTANCE, not one folder shared by every plugin in the project. Two
        instances both on CAMPINA at 132 BPM produce the same filename, so a
        shared folder means instance B's `replaceWithData` overwrites the file
        the host is still copying for instance A — the host lands B's groove on
        A's track — and B's sweep deletes A's pending file outright. That is the
        "silently empty MIDI track" the sweep exists to avoid, arriving through
        the sweep itself. */
    const juce::File instanceFolder;

    /** Where in the 2.6 s cycle the breath is, in [0, 1). */
    double pulsePhase { 0.0 };

    /** The component's OWN tick, not the chassis's. `KitOverlay` was the one
        component whose animation lived in its parent, and it cost `Chassis` a
        member meaning "when another component's animation last ticked";
        /simplify moved it back. Started in the constructor and stopped whenever
        the breath is not running, so a hovered or dragged button does no work
        rather than 30 wake-ups a second for nothing. */
    PollTimer pulsePoll;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DragMidiButton)
};

} // namespace forrobox
