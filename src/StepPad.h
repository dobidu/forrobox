/* ============================================================================
   FORRÓ BOX — the step pad

   The phase's second headline component, and Phase 5's entire grid is eighty
   instances of it. A custom Component for the recorded reason: velocity, lit
   state and press are per-instance, which a stateless LookAndFeel callback
   cannot carry.

   SIX static states, and the assertion that matters is that they are PAIRWISE
   distinct — six states drawn by one paint is six chances to draw the same
   thing twice.

   NOT here, because both need Phase 5's trigger FIFO and playhead: the trigger
   flash (`1 + strength` decaying over ~260 ms) and the currently-playing
   outline. No half-built accessor is left for either — 02-04's rule is that a
   guarantee with no caller is not a guarantee.
============================================================================ */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "LookAndFeel.h"
#include "Theme.h"

namespace forrobox
{

namespace pad
{
inline constexpr int kHeight = 26;   ///< css:465 .pad height
inline constexpr int kGap    = 5;    ///< css:463 .pads gap

// ── the unlit ground (css:616-624) ──────────────────────────────────────────
//
// Two ROWS, not one row at a different alpha: the light theme has its own
// gradient AND one inset shadow where the dark theme has two. This is the shape
// theme::Shadows learned in 04-01, where one field serving two declared values
// shipped the light header ten times too bright.
inline constexpr float kOffTopWhite   = 0.03f;   ///< css:618 dark, white
inline constexpr float kOffBottomBlack = 0.18f;  ///< css:618 dark, black
inline constexpr float kOffTopBlackLight    = 0.10f;   ///< css:622 light, both black
inline constexpr float kOffBottomBlackLight = 0.05f;   ///< css:622

/// `inset 0 1px 1px rgba(0,0,0,<a>)` — the recessed top edge.
inline constexpr float kInsetTopAlphaDark  = 0.40f;   ///< css:619 (and css:632)
inline constexpr float kInsetTopAlphaLight = 0.12f;   ///< css:623

/// `inset 0 0 0 1px rgba(0,0,0,0.25)` — css:619, dark theme only.
inline constexpr float kInsetRingAlpha = 0.25f;

// ── the lit ground (css:625-631) ────────────────────────────────────────────

/** `radial-gradient(120% 100% at 50% 22%, ...)` — css:627.

    An ELLIPSE with an offset origin, which `juce::ColourGradient` cannot
    express: its radial mode is circular (`juce_ColourGradient.h:67`). The
    resolution, settled at 04-03 planning: build a circular gradient of radius
    `ry` about the origin, then hand the `FillType` an x-scale of `rx / ry`
    about that same point. `juce::FillType::transform` (`juce_FillType.h:154`)
    applies to the GRADIENT, so the pad's rounded rectangle is untouched.

    `Graphics::addTransform` would have been the obvious move and would have
    stretched the pad too. */
inline constexpr float kLitRadiusX = 1.20f;   ///< of the pad's WIDTH
inline constexpr float kLitRadiusY = 1.00f;   ///< of the pad's HEIGHT
inline constexpr float kLitOriginX = 0.50f;
inline constexpr float kLitOriginY = 0.22f;

/// The lit gradient's stops: `<c + 22% white>` at the origin, `<c>` from 70% out.
///
/// The percentage is the RAW one the stylesheet writes. `theme::mixWeight` turns
/// the pair into the weight a browser actually applies — see its comment for why
/// 22 is not 0.22 here.
inline constexpr float kLitCentreWhitePct = 22.0f;   ///< css:627
inline constexpr float kLitOuterStop      = 0.70f;   ///< css:627 `var(--c) 70%`

/// `inset 0 1px 0 color-mix(in srgb, var(--c) 100%, white 35%)` — css:629.
inline constexpr float kLitSheenWhitePct = 35.0f;

/// `0 0 9px color-mix(in srgb, var(--c) calc(var(--accent-i) * 45%), transparent)` — css:630.
///
/// The pad's OWN accent-intensity law. Not the accent bar's `i x 35%`, and not
/// the knob's `saturate(0.4 + i x 0.6)`. Three elements, three CSS rules.
inline constexpr int   kLitGlowRadius  = 9;
inline constexpr float kLitGlowOpacity = 0.45f;

/// `.pad { --c mixed with 100%, white <pct>% }` — the base percentage of both
/// two-part mixes above. Named once because both write it.
inline constexpr float kLitBasePct = 100.0f;

// ── velocity (app.js:377-379) ───────────────────────────────────────────────

/// `0.32 + (vel/127) x 0.68`, applied by the prototype as `pad.style.opacity`.
inline constexpr float kVelocityOpacityFloor = 0.32f;
inline constexpr float kVelocityOpacityRange = 0.68f;

/** A hit at or below this velocity is a ghost — app.js:379.

    A ghost is a LIT pad that also carries the dot, not an unlit one. The
    prototype adds `ghost` on top of `on` and `.pad.ghost` (css:476-480) only
    appends the `::after` dot — it never touches the background. `PLANNING.md:451`
    says the pad "renders as off", which the running prototype contradicts; the
    design reference wins, the same standing rule that resolved `.pad.beat`. */
inline constexpr int kGhostVelocityMax = 42;

/// A muted, soloed-out or non-isolated row — `PLANNING.md:589`, "dims to 32%".
inline constexpr float kDimmedAlpha = 0.32f;

/// The ghost dot: 3 px, centred, at 90% (css:478-479).
inline constexpr int   kGhostDotSize    = 3;
inline constexpr float kGhostDotOpacity = 0.90f;

/// `transform: scale(0.9)` on press — css:472.
inline constexpr float kPressScale = 0.90f;

/** The lit layer's opacity for a 0..127 velocity. ONE definition; the pad
    paints through it and the tests assert against it. */
inline constexpr float opacityForVelocity (int velocity) noexcept
{
    return kVelocityOpacityFloor
         + (static_cast<float> (velocity) / 127.0f) * kVelocityOpacityRange;
}
} // namespace pad

class StepPad final : public juce::Component
{
public:
    StepPad (ForroBoxLookAndFeel&, juce::Colour instrumentColour);

    /** The component bounds that give `padRect` its 9 px of room for the lit
        glow. The glow is an OUTER box-shadow, and a Component's paint is
        clipped to its own bounds — painted at the pad's exact rect it would
        contribute nothing at all, which is a law with no observable effect.

        So the grid lays pads out on the css 26/5 pitch and asks this for the
        bounds, exactly as `ChassisLayout::kKnobCellHeight` asks the knob for
        its height. Neighbouring pads then OVERLAP in their transparent margins,
        which is why `hitTest` exists below. */
    static juce::Rectangle<int> boundsForPadRect (juce::Rectangle<int> padRect) noexcept
    {
        return padRect.expanded (pad::kLitGlowRadius);
    }

    /** The pad itself, inside this component's bounds — the inverse of
        `boundsForPadRect`. */
    juce::Rectangle<int> padRect() const noexcept
    {
        return getLocalBounds().reduced (pad::kLitGlowRadius);
    }

    void paint (juce::Graphics&) override;

    /** The glow margin is transparent and overlaps the neighbours, so clicks in
        it must fall through rather than be swallowed by whichever pad happens
        to be on top. */
    bool hitTest (int x, int y) override { return padRect().contains (x, y); }

    /** 0 is off. Anything above lights the pad; 1..42 also carries the ghost
        dot. See `pad::kGhostVelocityMax` for why a ghost is lit. */
    void setVelocity (int);
    int  getVelocity() const noexcept { return velocity; }

    /** Dim the whole pad to `pad::kDimmedAlpha`.

        `PLANNING.md:589` — a muted channel's sequencer row dims to 32%, and
        `:591` says an isolate dims every other row by the same amount. The pad
        dims ITSELF rather than the grid painting a scrim over it: the pads are
        components, so a translucent rectangle laid over the row would also dim
        the playhead crossing it, which belongs to neither the row nor the mute.

        Applied as a factor on the group opacity `paint` already computes for
        velocity, not as a `Component::setAlpha` — see `paint` for the measured
        reason, and for why it is a GROUP opacity rather than an alpha threaded
        through every `setColour`. */
    void setDimmed (bool);
    bool isDimmed() const noexcept { return dimmed; }

    /** Every fourth step carries the beat ring — but only when UNLIT. See
        `paint` for why that is the stylesheet's intent and not an oversight. */
    void setBeat (bool);
    bool isBeat() const noexcept { return beat; }

    bool isLit() const noexcept { return velocity > 0; }
    bool isGhost() const noexcept { return velocity > 0 && velocity <= pad::kGhostVelocityMax; }

    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    /** Clicked. Phase 5 wires this to the pattern grid; 04-03 leaves it unset,
        and an unset callback changes nothing, visibly and honestly. */
    std::function<void()> onClick;

private:
    void paintUnlit (juce::Graphics&, juce::Rectangle<float>, float radius) const;
    void paintLit (juce::Graphics&, juce::Rectangle<float>, float radius) const;

    ForroBoxLookAndFeel& lnf;
    const juce::Colour   colour;

    int  velocity { 0 };
    bool beat { false };
    bool dimmed { false };
    bool hovered { false };
    bool pressed { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepPad)
};

} // namespace forrobox
