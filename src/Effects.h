/* ============================================================================
   FORRÓ BOX — the two chassis-wide treatments, and every number they are made of

   `PLANNING.md:567-573` gives the `CACHAÇA` easter egg and `:628-635` the
   Ciclotron™ one. Both are whole-chassis visual effects driven by one parameter,
   both are keyframe animations, and both are specified entirely in
   `forrobox.css` — so every constant below has a design source and every one is
   compared against it. This header is enrolled in `verify-geometry.py`'s
   `GEOMETRY_HEADERS`, which means a constant added here is compared or excused
   BY NAME rather than trusted.

   THAT ENROLMENT IS WHY THE FILE EXISTS. 08-04 put the wash's two constants in
   `EffectOverlay.h` (then `DrunkOverlay.h`) and the sway's and the pulse's five in `Chassis.h`, and
   `Chassis.h`'s own comment conceded the reason — *"because this header is the
   one enrolled in `verify-geometry`"*. That is enrolment deciding where a number
   lives, and its cost was real: `DrunkOverlay.h`, as it then was, is not enrolled, so
   `kOnsetPercent` and `kSpanPercent` sat outside the coverage gate entirely and
   a third constant beside them would have been born unchecked. `app.js`'s
   `(c - 65) / 35` and its `c >= 88` are adjacent lines of one function and were
   being compared by two different scripts.

   08-05 adds a THIRD group of exactly the same kind, so the choice was to fix
   the split or to repeat it. Recorded in STATE.md as a deferred item with this
   named fix; done here because this is the plan that would have made it worse.

   CLOSED IN FULL at 14-03. The wash's gradient table had stayed in
   `EffectOverlay.cpp`, text-parsed there by `verify-theme.py`, and the sway's
   render cadence — explicitly not a design value — had come in with the rest.
   The table is below now, and `verify-theme` reads it here; the cadence is
   `Chassis::kSwayCommitDegrees`, beside the sway it quantises. Every constant
   in this file has a design source and a named checker.

   NOT the components. `EffectOverlay` owns the wash's pixels and `Chassis` owns
   the sway's transform; what moved is the VALUES, which belong to the design
   rather than to whichever class happens to read them.
============================================================================ */
#pragma once

#include "Theme.h"

#include <array>

namespace forrobox
{

/** `PLANNING.md:567-573` — the `CACHAÇA` easter egg. */
namespace drunk
{

/** `--drunk = clamp((cachaça − 65) / 35, 0, 1)` — PLANNING.md:568, app.js:596. */
inline constexpr float kOnsetPercent = 65.0f;
inline constexpr float kSpanPercent  = 35.0f;

/** css:117-122 — at or above 88% the chassis SWAYS: a 6 s ease-in-out rotation
    of ±0.18° about its own centre.

    88, not 65: the wash and the sway are two thresholds, and the wash spends its
    whole 65..100 ramp getting there. */
inline constexpr float  kTipsyPercent = 88.0f;
inline constexpr float  kSwayDegrees  = 0.18f;
inline constexpr double kSwaySeconds  = 6.0;

/** css:100-101 — while the chassis is tipsy the `♪ NO PONTO` label breathes
    between 0.55 and 1.0 on a 1.6 s ease-in-out loop.

    `kLabelPulse…` rather than `kPulse…`: `dragmidi::kPulseSeconds` already
    exists, and `verify-geometry`'s coverage check counts BARE names — so a
    second `kPulseSeconds` was born already counted as compared, against the
    drag-MIDI button's 2.6 s. */
inline constexpr double kLabelPulseSeconds     = 1.6;
inline constexpr float  kLabelPulseLowOpacity  = 0.55f;
inline constexpr float  kLabelPulseHighOpacity = 1.0f;

// ── the wash's gradients — css:92-95 ─────────────────────────────────────────
//
// Moved here from `EffectOverlay.cpp`'s anonymous namespace (14-03): they are
// this feature's design numbers, and the header's rule is that every one of
// them lives here. `verify-theme.py` compares them against the stylesheet.

/** One `radial-gradient(<rx> <ry> at <cx> <cy>, <colour>, transparent <end>)`
    from css:92-93, with every length as a fraction of the box.

    The two colours are NOT hexes typed here: css writes `rgba(232,101,10,…)`
    and `rgba(242,194,0,…)`, which are `--c-zabumba` and `--c-pandeiro` spelled
    out. Reading them from `theme::accentSpecs` puts them under the cross-check
    that already compares that table against the stylesheet, where a literal
    here would be a fourth copy of a colour nothing compares. */
struct RadialWashLayer
{
    theme::Accent accent;
    float alpha;      ///< the alpha at stop 0
    float endStop;    ///< where it reaches `transparent`, as a fraction of the radius
    float centreX, centreY, radiusX, radiusY;
};

/** FIRST layer on top — the order `background` lists them, and the order
    `EffectOverlay` composites them in. */
inline constexpr std::array<RadialWashLayer, 2> kRadialLayers { {
    { theme::Accent::zabumba,  0.42f, 0.58f, 0.50f,  1.18f, 1.20f, 0.80f },
    { theme::Accent::pandeiro, 0.16f, 0.52f, 0.50f, -0.20f, 1.40f, 1.20f },
} };

/** `linear-gradient(180deg, rgba(232,101,10,0.06), rgba(232,101,10,0.13))` —
    css:95, the bottom layer. */
inline constexpr theme::Accent kLinearAccent     = theme::Accent::zabumba;
inline constexpr float         kLinearTopAlpha   = 0.06f;
inline constexpr float         kLinearEndAlpha   = 0.13f;

} // namespace drunk

/** `PLANNING.md:628-635` — the Ciclotron™ treatment. */
namespace ciclo
{

/** css:109 — `filter: saturate(0.9) contrast(1.06)` on the whole chassis. */
inline constexpr float kSaturate = 0.9f;
inline constexpr float kContrast = 1.06f;

/** css:106 — `repeating-linear-gradient(0deg, rgba(0,0,0,0.14) 0 1px,
    transparent 1px 3px)`: a 3 px vertical period whose first 1 px is black at
    14%, in DEVICE pixels.

    Device, not design: the stylesheet's `1px` is a CSS pixel and the browser
    draws it on the device grid, so a scanline that scaled with the editor would
    be 2 px thick at 2x and stop reading as a scanline. */
inline constexpr int   kScanlinePeriodPx = 3;
inline constexpr int   kScanlineDarkPx   = 1;
inline constexpr float kScanlineAlpha    = 0.14f;

/** css:108 — the overlay sits at 50% opacity and flickers on a 4 s `steps(1)`
    loop, with the four dips css:110-116 names. */
inline constexpr double kFlickerSeconds = 4.0;
inline constexpr float  kFlickerBase    = 0.5f;
inline constexpr float  kFlickerDip1    = 0.16f;
inline constexpr float  kFlickerPeak1   = 0.58f;
inline constexpr float  kFlickerDip2    = 0.28f;
inline constexpr float  kFlickerPeak2   = 0.6f;

/** css:425 — `text-shadow: 1.2px 0 <danger 70%>, -1.2px 0 <triangulo 70%>` on
    the selected CICLOTRON row's name. */
inline constexpr float kAberrationPx     = 1.2f;
inline constexpr float kAberrationWeight = 0.70f;

/** css:426-427 — the sub-label turns `--danger` and blinks on a 1.4 s
    `steps(1)` loop. */
inline constexpr double kBlinkSeconds = 1.4;
inline constexpr float  kBlinkOn      = 1.0f;
inline constexpr float  kBlinkDim     = 0.25f;
inline constexpr float  kBlinkHalf    = 0.4f;

} // namespace ciclo

} // namespace forrobox
