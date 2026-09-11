/* ============================================================================
   FORRÓ BOX — Knob

   The plugin's most-used control. `PLANNING.md:347` calls the flat indicator
   line the knob's identity — "no cap, no bevel, no notch" — and the whole
   component exists to draw that line and its two arcs correctly at three
   sizes.

   A custom Component, NOT a LookAndFeel override and NOT a juce::Slider
   subclass. Recorded in STATE: it carries per-instance state a stateless L&F
   callback cannot (polarity, accent, size), and juce::Slider brings its own
   mouse handling that would fight the exact drag law controls.js specifies.
   ForroBoxLookAndFeel stays thin — the accent accessor /simplify deleted in
   04-01 is the precedent for what growing it produces.

   EVERYTHING HERE IS RELATIVE. `PLANNING.md:347` renders the same 100x100
   viewBox at 28, 32 and 54 px, so 38 / 30 / 5 / 16 are viewBox units and not
   pixels: at 32 px the track radius is 12.16 px. Treating them as pixels would
   draw one correct 100 px knob and three wrong ones — and 32 px is the strip
   case, the one the header would never reveal.
============================================================================ */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "LookAndFeel.h"
#include "Theme.h"
#include "Typography.h"

namespace forrobox
{

/** The knob's geometry, in the spec's 100x100 viewBox units.

    Cross-checked against `forrobox.css` by `scripts/verify-geometry.py` on
    every build wherever the stylesheet declares the number, for the same
    reason the colour tables are: a wrong length is not a crash or a failed
    test, it is a control that is subtly the wrong shape.

    The radii and the indicator tip are NOT in the CSS — they live in
    `controls.js:102-105` and `:63-65`, which the stylesheet cannot carry
    because they are SVG path geometry. Those are quoted from there. */
namespace knob
{
inline constexpr float kViewBox = 100.0f;

/// `A0 = -135; A1 = 135` (controls.js:103), 0 deg pointing up, 270 deg total.
inline constexpr float kSweepStartDeg = -135.0f;
inline constexpr float kSweepEndDeg   =  135.0f;

/// `_arcPath (A0, A1, 38)` (controls.js:104) — both arcs share the radius.
inline constexpr float kArcRadius = 38.0f;
inline constexpr int   kArcStroke = 5;     ///< css:360 .fb-knob-track stroke-width

/// `r="30"` (controls.js:69).
inline constexpr float kHubRadius = 30.0f;
inline constexpr float kHubStroke = 1.5f;  ///< css:362 .fb-knob-hub stroke-width

/// `gap: 3px` between the dial and its micro-label (css:357 .fb-knob).
///
/// Lives here rather than in ChassisLayout: it is the KNOB's own gap, and the
/// strip's knob-cell height is derived FROM it rather than carrying a copy.
inline constexpr int kLabelGap = 3;

/// The micro-label's row height, read from the type scale rather than retyped.
inline constexpr int kLabelHeight =
    static_cast<int> (type::textStyles[static_cast<size_t> (type::Style::knobMicroLabel)].heightPx);

static_assert (type::textStyles[static_cast<size_t> (type::Style::knobMicroLabel)].style
                   == type::Style::knobMicroLabel,
               "textStyles is no longer indexed by its own enum, so kLabelHeight is reading "
               "some other row's height");

/// The line runs from the centre (50,50) to (50,16) — controls.js:64-65.
inline constexpr float kIndicatorTipY  = 16.0f;
inline constexpr int   kIndicatorStroke = 5;   ///< css:363 .fb-knob-line stroke-width

/// `filter: saturate(calc(0.4 + var(--accent-i) * 0.6))` — css:361 and :613.
///
/// NOT theme::accentFill, whose floor is 0.3/0.7: that is the accent BAR's law
/// (css:285). Two different laws for two different elements, and 04-01's
/// lesson was that a shared mechanism does not imply a shared value.
inline constexpr float kSaturationFloor = 0.4f;
inline constexpr float kSaturationRange = 0.6f;

/** The value angle in degrees for a normalised 0..1 position. */
inline constexpr float angleForProportion (float proportion) noexcept
{
    return kSweepStartDeg + proportion * (kSweepEndDeg - kSweepStartDeg);
}

/** Bipolar arcs grow from the sweep's centre, which is 0 deg — pointing up. */
inline constexpr float kCentreDeg = (kSweepStartDeg + kSweepEndDeg) * 0.5f;
} // namespace knob

/** One knob: the dial and its micro-label.

    The component's bounds hold BOTH — the dial is a square of `dialSize`
    centred horizontally at the top, then a 3 px gap (css:357), then the
    label row. `preferredHeight` reports what that needs. */
class Knob final : public juce::Component
{
public:
    enum class Polarity
    {
        unipolar,   ///< the value arc sweeps from -135 deg
        bipolar,    ///< it grows from 0 deg either way — PITCH and PAN
    };

    Knob (ForroBoxLookAndFeel&, int dialSizePx, Polarity, juce::Colour arcColour,
          juce::String microLabel);

    void paint (juce::Graphics&) override;

    /** The dial's square, in this component's coordinates. */
    juce::Rectangle<float> dialBounds() const noexcept;

    /** The label row below the dial. Empty when the knob has no label. */
    juce::Rectangle<int> labelBounds() const noexcept;

    /** The height a knob of this dial size needs, including its label row. */
    static int preferredHeight (int dialSizePx, bool hasLabel) noexcept;

    /** The displayed position, 0..1 across the parameter's range.

        The ONLY value state the knob holds, and it is a view: 04-02's Task 4
        drives it from the attached parameter, and nothing else writes it.
        There is deliberately no range, interval or default here — those live
        on the parameter (see STATE's decision), so there is nothing to
        diverge. */
    void setProportion (float newProportion);
    float getProportion() const noexcept { return proportion; }

    /** Painted with the hub stroke in `--active` when true (css:359). */
    void setShowingFocusRing (bool);

    // ── the gesture seam ────────────────────────────────────────────────────
    //
    // The knob turns input into INTENT and nothing else. It cannot compute the
    // wheel's `step * max(1, range/50)` because it has no range, interval or
    // default — those live on the parameter, which is the whole point of
    // STATE's decision. `KnobAttachment` installs these and does the
    // arithmetic; a Knob with none installed simply does not move, which is
    // one code path with a no-op edge rather than two.

    /** An absolute normalised target from a drag. */
    std::function<void (float)> onDragTo;

    /** A relative move: `direction` is +1 or -1, `fine` asks for the smallest
        step the parameter has rather than the wheel's coarse multiple.

        The knob cannot compute the magnitude — `step * max(1, range/50)`
        (controls.js:161) needs the range and interval, which live on the
        parameter. So it reports WHICH WAY and HOW FINELY, and the attachment
        does the arithmetic. */
    std::function<void (int direction, bool fine)> onNudge;

    /** Alt+click. The target is the PARAMETER's default (PLANNING.md:876-878). */
    std::function<void()> onReset;

    /** Bracket a drag so the host records one gesture, not a stream. */
    std::function<void()> onGestureStart, onGestureEnd;

    /** The parameter's own formatting — "L20", "+3", "82". Never the knob's. */
    std::function<juce::String()> getDisplayText;

    /** Double-click's inline editor. Returns false when the text is unusable. */
    std::function<bool (const juce::String&)> onTextEntered;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override;

    /** Where the tooltip is shown. Presentation only — never part of the value
        path, so a knob with no tooltip behaves identically. */
    void setTooltip (class ValueTooltip* t) noexcept { tooltip = t; }

private:
    void showTooltip (const juce::String& overrideText = {});
    void hideTooltip();

    /** One viewBox unit in this knob's pixels. Every length is scaled through
        this, so the geometry above stays the spec's own numbers. */
    float unitScale() const noexcept;

    void paintArc (juce::Graphics&, juce::Rectangle<float> dial,
                   float fromDeg, float toDeg, juce::Colour) const;

    ForroBoxLookAndFeel& lnf;
    const int            dialSize;
    const Polarity       polarity;
    const juce::Colour   accentColour;
    const juce::String   label;

    float proportion { 0.0f };
    bool  showFocusRing { false };

    /** The drag is anchored at mouse-DOWN and computed from the total delta.
        An incremental `dv` per mouse-move accumulates rounding per event and
        makes the result depend on the mouse's report rate — the same class of
        bug as 02-02's block-size-dependent swing clamp. */
    float dragStartProportion { 0.0f };
    int   dragStartY { 0 };

    /** True only between a drag's onGestureStart and its onGestureEnd.

        Without it, mouseUp ended a gesture that mouseDown's Alt branch never
        began: JUCE asserts on an unbalanced endChangeGesture, and a host sees a
        gesture-end with no matching begin. It also gates mouseDrag, so
        releasing Alt mid-press cannot resume a drag from a STALE anchor left
        over from the previous gesture. */
    bool gestureActive { false };

    /** The inline editor from a double-click, owned rather than leaked.

        Component's destructor removes its children but does not delete them
        (juce_Component.cpp), so a raw `new` here leaked whenever the plugin
        window closed with an editor open. */
    std::unique_ptr<juce::TextEditor> inlineEditor;

    void closeInlineEditor();

    class ValueTooltip* tooltip { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

} // namespace forrobox
