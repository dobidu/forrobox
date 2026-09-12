/* ============================================================================
   FORRÓ BOX — Fader

   An ABSOLUTE control, and that is the whole design. `controls.js:232-247`
   computes the value from the pointer's x inside the track rectangle and
   clamps it — so a click JUMPS to where it landed and a drag is that same
   computation repeated. There is no delta accumulation, no anchor, no
   shift-fine and no wheel.

   Which is why it is not the knob with a different paint. The knob's seam
   speaks `onNudge (direction, fine)` and `onReset`, a vocabulary an absolute
   control has no way to fire; what the two genuinely share is the binding, and
   that lives in ProportionAttachment. Nor is it a `juce::Slider` in
   LinearHorizontal mode: that brings its own mouse handling, including the
   velocity-sensitive drag and the wheel this control must not have.

   No tooltip. `controls.js` calls `showTip` from the knob's five gestures and
   from none of the fader's — its `hideTip` on release is defensive. Inventing
   one because the knob has one would be a silent spec deviation; the readout
   is the label row above it (PLANNING.md:291).
============================================================================ */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "LookAndFeel.h"
#include "Theme.h"

namespace forrobox
{

namespace fader
{
/// `padding: 8px 0` around the track — css:376.
inline constexpr int kPadY = 8;

/// `height: 4px`, `border-radius: 999px` — css:377. Fully rounded, so the
/// radius is half the height rather than a number of its own.
inline constexpr int kTrackHeight = 4;

/// `width: 12px; height: 12px; border-radius: 50%` — css:379.
inline constexpr int kThumbSize = 12;

/// `box-shadow: 0 1px 3px rgba(0,0,0,0.4)` — css:379.
inline constexpr int   kThumbShadowY      = 1;
inline constexpr int   kThumbShadowRadius = 3;
inline constexpr float kThumbShadowAlpha  = 0.4f;

/** `filter: saturate(calc(0.4 + var(--accent-i) * 0.6))` — css:378.

    The KNOB's law (css:613), not the accent bar's `0.3 + i x 0.7` and not the
    step pad's `i x 45%`. Four elements, and the stylesheet gives three
    different laws; the fader happens to share the knob's. Written out here
    rather than reaching for `knob::kSaturationFloor` so that the day css:378
    changes, one file moves. */
inline constexpr float kSaturationFloor = 0.4f;
inline constexpr float kSaturationRange = 0.6f;

/// The css box: padding, track, padding. Cross-checked as `kFaderHeight`.
inline constexpr int kHeight = kPadY * 2 + kTrackHeight;

/// Half a thumb, which is how far it hangs past each end of the track at the
/// extremes of travel.
inline constexpr int kThumbOverhang = kThumbSize / 2;
} // namespace fader

class Fader final : public juce::Component
{
public:
    Fader (ForroBoxLookAndFeel&, juce::Colour fillColour);

    /** The component bounds that give the thumb room to hang past the track.

        At proportion 0 the thumb is centred on the track's left end, so half of
        it sits outside — which is what the prototype draws, the track being
        100% of `.fb-fader` while the thumb is positioned relative to it and
        allowed to overflow. A Component's paint is clipped to its own bounds,
        so the room has to be real. StepPad reserves its glow margin the same
        way, and `ChassisLayout` asks the knob for its height the same way. */
    static juce::Rectangle<int> boundsForBox (juce::Rectangle<int> box) noexcept
    {
        return box.expanded (fader::kThumbOverhang, 0);
    }

    /** The 4 px track, inside this component's bounds — the rectangle the
        prototype measures the pointer against, and therefore the one the value
        law is defined on. */
    juce::Rectangle<int> trackRect() const noexcept;

    void paint (juce::Graphics&) override;

    /** 0..1, left to right. The fader holds no value of its own: a
        ProportionAttachment writes this from the parameter and nothing else
        does. */
    void setProportion (float);
    float getProportion() const noexcept { return proportion; }

    /** The gesture seam, and deliberately only these three — the fader has no
        reset, no nudge and no typed entry to offer. */
    std::function<void (float proportion)> onDragTo;
    std::function<void()> onGestureStart, onGestureEnd;

    /** Fired whenever the displayed position changes, which is only ever from
        `setProportion` — so only ever from the attachment.

        The strip's `NN%` readout hangs off this rather than off a second
        listener on the same parameter. Two listeners can disagree, and a
        readout showing a different number from the fader beside it is the
        worst kind of wrong: both are plausible. */
    std::function<void (float proportion)> onProportionChanged;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    /** The proportion the prototype's `fromEvent` would compute for a pointer
        at `x`: its position within the TRACK, clamped. Public because the
        tests assert the law directly, and because a drag and a click are the
        same call. */
    float proportionForX (int x) const noexcept;

private:
    void dragTo (const juce::MouseEvent&);

    ForroBoxLookAndFeel& lnf;
    const juce::Colour   colour;

    float proportion { 0.0f };
    bool  gestureActive { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Fader)
};

} // namespace forrobox
