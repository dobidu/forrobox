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

   NOT the components. `EffectOverlay` owns the wash's pixels and `Chassis` owns
   the sway's transform; what moved is the VALUES, which belong to the design
   rather than to whichever class happens to read them.
============================================================================ */
#pragma once

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

/** The smallest rotation worth committing.

    NOT a design number — `verify-geometry` excuses it for that reason. It is the
    angle at which the chassis's furthest corner moves a quarter of a device
    pixel at the design size: `0.25 / 716` radians, rounded. Below it a new
    transform costs a full-chassis invalidation and moves nothing. */
inline constexpr float kSwayCommitDegrees = 0.02f;

/** css:100-101 — while the chassis is tipsy the `♪ NO PONTO` label breathes
    between 0.55 and 1.0 on a 1.6 s ease-in-out loop.

    `kLabelPulse…` rather than `kPulse…`: `dragmidi::kPulseSeconds` already
    exists, and `verify-geometry`'s coverage check counts BARE names — so a
    second `kPulseSeconds` was born already counted as compared, against the
    drag-MIDI button's 2.6 s. */
inline constexpr double kLabelPulseSeconds     = 1.6;
inline constexpr float  kLabelPulseLowOpacity  = 0.55f;
inline constexpr float  kLabelPulseHighOpacity = 1.0f;

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
