/* ============================================================================
   FORRÓ BOX — U+266A, drawn

   `PLANNING.md:571` and app.js:601 make the CACHAÇA label read `♪ NO PONTO`
   past 88%. None of the four embedded families carries U+266A: 08-03's coverage
   report says so on every build, and deliberately does not fail on it. A glyph
   this project does not have would render as a box, or as nothing.

   So the note is a Path, which is `GearButton::shapeFor`'s precedent from
   08-02 — the other shape the fonts do not have. Its proportions have NO design
   source: the stylesheet writes the character and leaves the rest to whatever
   font the browser found, so these are chosen to read as an eighth note beside
   9 px type and are not cross-checked against anything. That is why this header
   is not enrolled in `verify-geometry`, exactly as `GearButton.h` is not.

   Sized from the TYPE ROW it sits beside rather than from a number invented
   here — 04-02's `kKnobCellHeight` lesson, which `gear::kSize` follows too.
============================================================================ */
#pragma once

#include <juce_graphics/juce_graphics.h>

namespace forrobox::noteglyph
{

/** The head's width and height, the stem's thickness, and the flag's reach and
    drop, as fractions of the glyph's cap height BEFORE normalisation.

    The shape is then scaled and translated so its ink box is exactly one cap
    height tall and starts at the left edge it is given — the tilted head
    overshoots both, by half a pixel at 12 px type, and a note whose foot sits
    below the baseline of the word beside it reads as a descender. */
inline constexpr float kHeadWidthRatio  = 0.62f;
inline constexpr float kHeadHeightRatio = 0.46f;
inline constexpr float kStemWidthRatio  = 0.12f;
inline constexpr float kFlagWidthRatio  = 0.42f;
inline constexpr float kFlagDropRatio   = 0.52f;

/** A tilt on the head, so it reads as a notehead rather than as a dot. */
inline constexpr float kHeadTiltDegrees = -20.0f;

/** The gap between the note and the word that follows it, as a fraction of the
    cap height — the space in `"♪ NO PONTO"`, which is a space this has to draw
    because the note is not part of the string. */
inline constexpr float kTrailingGapRatio = 0.45f;

/** How wide a note of this height is, including its trailing gap. Measured
    from the normalised shape rather than summed from the ratios above, which
    do not account for the tilt or the flag's curve. */
float widthFor (float capHeight) noexcept;

/** An eighth note whose baseline sits at `baseline` and whose left edge sits at
    `left`, standing `capHeight` tall.

    A free function so a test can measure the shape without constructing a
    component or rendering one — `GearButton::shapeFor`'s reason. */
juce::Path shapeFor (float left, float baseline, float capHeight);

} // namespace forrobox::noteglyph
