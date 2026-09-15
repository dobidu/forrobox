/* ============================================================================
   FORRÓ BOX — Playhead

   The line that sweeps the sequencer while the groove plays. `css:486-498`.

   A CHILD COMPONENT that moves, not a shape painted by the grid. At 60 fps a
   grid that painted it would repaint eighty pads a frame, and `StepPad::paint`
   is already the most expensive paint in the plugin — `/simplify` measured its
   glow at 19 us a lit pad. A narrow component repaints its own rectangle and
   the pads underneath it are untouched.

   CONTINUOUS, and that is the whole reason it needs a position rather than a
   step. The prototype gets its sweep free from the browser: `movePlayhead`
   (`app.js:719`) sets `transition: left <stepDur>ms linear` and lets CSS
   interpolate between step centres. JUCE has no such thing, so the sweep is
   driven from the clock's own fractional position at frame rate.

   It passes through each pad's CENTRE, by interpolating between the centres
   `SequencerLayout::padBounds` gives — not by dividing the strip into equal
   cells as the prototype does. The prototype's `padGeom.width / state.steps`
   ignores the gaps between pads, which at 16 steps is 75 px of a ~1000 px strip
   spread across the sweep; the line would drift up to half a pad away from the
   pad it is over. Interpolating between real centres cannot disagree with the
   pads, because it is built from them.
============================================================================ */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "LookAndFeel.h"
#include "SequencerGrid.h"

namespace forrobox
{

namespace playhead
{
/// `width: 3px` — css:487.
inline constexpr int kLineWidth = 3;

/// `top: -3px; bottom: -3px` — it overhangs the rows block at both ends.
inline constexpr int kOverhang = 3;

/// `border-radius: 3px` — css:490.
inline constexpr float kCornerRadius = 3.0f;

/// The trailing column BEHIND the line: `::before`, `width: 26px` (css:495).
inline constexpr int kTrailWidth = 26;

/// The trail's strongest alpha, at the line: `pandeiro 16%` (css:496).
inline constexpr float kTrailAlpha = 0.16f;

/// `box-shadow: 0 0 10px <pandeiro>` — css:490.
inline constexpr int kGlowRadius = 10;

/// `0 0 3px white`, the inner core of the same shadow — css:490.
inline constexpr int kCoreGlowRadius = 3;

/// The head's own gradient: `pandeiro 90% mixed with white` down to pandeiro.
inline constexpr float kTopWhiteMix = 0.10f;
} // namespace playhead

class Playhead final : public juce::Component
{
public:
    /** No LookAndFeel. Every other component here takes one; this one has
        nothing to ask it. `css:488` names `--c-pandeiro` directly, the accent
        colours are the same in both themes, and `--accent-i` applies to the
        accent bar and the activity fill but not to the playhead. Taking a
        reference and not using it is what Clang caught. */
    Playhead();

    /** The component's bounds for a line centred at `centreX` over `rowsArea`.

        Reserves the trail to the LEFT and the glow on every side, the way
        `StepPad::boundsForPadRect` reserves its own glow: a Component's paint is
        clipped to its bounds, so anything that falls outside the line itself has
        to be inside them. */
    static juce::Rectangle<int> boundsForLineAt (int centreX,
                                                 juce::Rectangle<int> rowsArea) noexcept;

    /** Where the line sits for one fractional position, in the grid's own
        coordinates.

        `position` is in STEPS and may be fractional, negative, or past the
        window — it is the clock's, not a pad index. Wrapped the way
        `Clock.cpp:104` wraps it, `((n % window) + window) % window`, because the
        published position IS negative for the first `outputDelaySamples()` after
        Play and again after a host loop wrap. `fmod` alone returns a negative
        there and the line would land off the left edge. */
    static int lineCentreFor (double position, juce::Rectangle<int> pads, int stepCount) noexcept;

    void paint (juce::Graphics&) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Playhead)
};

} // namespace forrobox
