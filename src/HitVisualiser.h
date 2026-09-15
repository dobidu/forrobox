/* ============================================================================
   FORRÓ BOX — HitVisualiser

   One channel's trigger LED and activity meter — `PLANNING.md:480-489`,
   `css:277-281` and `css:303-319`.

   ONE component for both, because both read ONE level. The prototype keeps them
   in one `refs.viz_<id>` record and moves them together in `vizLoop`
   (`app.js:244-261`); splitting them here would mean two decays that could
   disagree about how bright the channel is.

   TOLD the elapsed frames, never reading a clock. `GainReductionMeter` is the
   precedent and the reason is recorded there: three 04-04 checks failed on
   MSVC's clock rather than on the code. x0.82 per frame is a per-FRAME law, so
   the meter is told how many frames passed and the test drives it directly.

   The LED and the meter live in DIFFERENT boxes — the LED in the strip head, the
   meter in the reserved `hitVisualiser` slot lower down — so this component owns
   neither. It is asked to paint each, into bounds its owner gives it.
============================================================================ */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "LookAndFeel.h"

namespace forrobox
{

namespace hitviz
{
/// `width: 8px; height: 8px` — css:278.
inline constexpr int kLedDiameter = 8;

/// `.strip-head-r { gap: 7px }` — css:275, between the LED and the index.
inline constexpr int kLedGap = 7;

/// `opacity: 0.22` at rest — css:279.
inline constexpr float kLedRestingAlpha = 0.22f;

/// On trigger: `0.25 + vel x 0.75` — PLANNING.md:481.
inline constexpr float kLedLitBase = 0.25f;
inline constexpr float kLedLitSpan = 0.75f;

/// `box-shadow: 0 0 <2 + vel x 7>px` — PLANNING.md:481.
inline constexpr float kLedGlowBase = 2.0f;
inline constexpr float kLedGlowSpan = 7.0f;

/// The fill's opacity: `0.35 + level x 0.65` — PLANNING.md:486.
inline constexpr float kFillBase = 0.35f;
inline constexpr float kFillSpan = 0.65f;

/// The fill's far end: `<colour at 30%>` — css:312.
inline constexpr float kFillFarAlpha = 0.30f;

/// `filter: saturate(0.4 + accent-i x 0.6)` — css:313.
inline constexpr float kFillSaturationBase = 0.4f;
inline constexpr float kFillSaturationSpan = 0.6f;

/// 16 divisions, whatever the step count — `repeat(..., 100% / 16)`, css:317.
inline constexpr int kTickDivisions = 16;

/// The ticks: `<bg at 55%>`, whole overlay at `opacity: 0.5` — css:317-318.
inline constexpr float kTickGroundMix = 0.55f;
inline constexpr float kTickAlpha = 0.5f;

/// `level *= 0.82` per frame — app.js:253, PLANNING.md:489.
inline constexpr float kDecayPerFrame = 0.82f;

/// Below this the channel is dark and the meter is cleared — app.js:249.
inline constexpr float kSilenceLevel = 0.001f;
} // namespace hitviz

/** One channel's level, and the two things that show it. */
class HitVisualiser final
{
public:
    explicit HitVisualiser (juce::Colour accentColour) : accent (accentColour) {}

    /** A hit arrived. `max`, not assignment — app.js:237's `Math.max(v.level,
        vel)`, so a quiet ghost cannot pull a loud hit's meter down mid-decay. */
    void trigger (float normalisedVelocity) noexcept;

    /** Advance the decay by `frames`. Told, never measured — see the header. */
    void advance (int frames) noexcept;

    /** Drop to dark immediately, for a stopped transport. */
    void reset() noexcept { level = 0.0f; }

    float getLevel() const noexcept { return level; }
    bool  isLit() const noexcept { return level > hitviz::kSilenceLevel; }

    /** The LED's own alpha and glow radius, so the tests read the LAW rather
        than a rendered approximation of it. */
    float ledAlpha() const noexcept;
    float ledGlowRadius() const noexcept;

    void paintLed (juce::Graphics&, juce::Rectangle<int> box) const;
    void paintMeter (juce::Graphics&, juce::Rectangle<int> box, ForroBoxLookAndFeel&) const;

private:
    juce::Colour accent;
    float level { 0.0f };
};

} // namespace forrobox
