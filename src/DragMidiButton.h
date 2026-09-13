/* ============================================================================
   FORRÓ BOX — DRAG MIDI

   The footer's primary call to action: `↓  DRAG MIDI  .mid` on a zabumba-tinted
   gradient with a 1.5 px accent border. css:520-543 and PLANNING.md:492-506.

   A STUB. It draws, it hovers, it presses, and it exports nothing — Phase 7 owns
   `juce::DragAndDropContainer::performExternalDragDropOfFiles` and the SMF
   writer. No onClick, no drag source, no state: the same honest shape LOAD, the
   pattern cycler and the preset arrows already have.

   And NO idle pulse. PLANNING.md:499 specifies a 2.6 s breathing glow and a 2 px
   bobbing arrow on the same cycle; both are deferred to Phase 7 with the export.
   An animated call to action for a control that does nothing is the loudest
   possible lie, and it would add this plugin's first animation timer to the plan
   that also moved ~750 lines of header out of Chassis. There is deliberately no
   half-built accessor left behind for it.
============================================================================ */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Chassis.h"
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

    void paint (juce::Graphics&) override;

    /** Keeps the pointer out of the reserved glow margin, so hovering the empty
        space beside the button does not light it. StepPad's rule. */
    bool hitTest (int x, int y) override { return contentBox().contains (x, y); }

    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    ForroBoxLookAndFeel& lnf;

    /** Measured ONCE, in the constructor. `type::trackedWidth` lays the string
        out, and `type::drawTracked` lays it out again to draw it — so measuring
        in `paint` shaped all three runs twice, 11.67 us of a 51 us idle paint.
        The type scale is constexpr, so these cannot change at runtime.
        `Segmented` caches its spans in its constructor for the same reason. */
    const Metrics box;

    bool hovered { false };
    bool pressed { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DragMidiButton)
};

} // namespace forrobox
