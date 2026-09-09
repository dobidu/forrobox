/* ============================================================================
   FORRÓ BOX — the chassis

   A fixed 1200×780 Component. The editor owns one and applies a single scale
   transform to it, so every layout number in this file and in every later plan
   is a DESIGN pixel and no component does its own scaling arithmetic.

   04-01 builds the four regions and the five channel strips as surfaces only.
   The controls arrive later: knobs 04-02, pads and buttons 04-03, the header
   and footer wiring 04-04. Their boxes are reserved here so those plans drop
   components into settled geometry instead of re-flowing the strip.
============================================================================ */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "LookAndFeel.h"
#include "ParameterIDs.h"

namespace forrobox
{

/** Every region rectangle, in design px, derived once from the row heights.

    A struct rather than four `getHeaderBounds()`-style methods: the tests
    assert against the same values the paint code uses, and a computed rectangle
    that only paint can see is a rectangle no test can check. */
struct ChassisLayout
{
    // ── the four rows: header 72 / main 1fr / sequencer 196 / footer 56 ─────
    static constexpr int kWidth          = 1200;
    static constexpr int kHeight         =  780;
    static constexpr int kHeaderHeight   =   72;
    static constexpr int kSequencerHeight = 196;
    static constexpr int kFooterHeight   =   56;

    /** `main` is `1fr` — whatever the other three rows leave. Derived, never
        written down: a fifth constant would be a second source of truth for a
        value that is already determined. */
    static constexpr int kMainHeight = kHeight - kHeaderHeight - kSequencerHeight - kFooterHeight;

    // ── main splits into the matrix (1fr) and a 280 px side panel ──────────
    static constexpr int kSidePanelWidth = 280;
    static constexpr int kNumStrips      = 5;

    /** The 1 px inter-strip gap IS the divider — the matrix paints `--line` and
        the strips sit on top with gaps between them. Drawing borders instead
        would give 2 px seams wherever two strips meet. */
    static constexpr int kStripGap = 1;

    // ── inside a strip, from PLANNING.md's "Channel matrix" ────────────────
    static constexpr int kStripPadTop    = 12;
    static constexpr int kStripPadSide   = 11;
    static constexpr int kStripPadBottom = 10;
    static constexpr int kAccentBarHeight = 4;
    static constexpr int kAccentBarMarginTop    = 8;
    static constexpr int kAccentBarMarginBottom = 9;
    static constexpr int kAccentGlowRadius = 10;
    static constexpr int kHeadRowHeight = 16;

    /** `color-mix(in srgb, var(--panel) 88%, var(--c-zabumba))` — the anchor
        tint. 88% panel means 12% accent. */
    static constexpr float kAnchorAccentWeight = 0.12f;

    juce::Rectangle<int> header;
    juce::Rectangle<int> main;
    juce::Rectangle<int> matrix;
    juce::Rectangle<int> sidePanel;
    juce::Rectangle<int> sequencer;
    juce::Rectangle<int> footer;
    std::array<juce::Rectangle<int>, kNumStrips> strips;

    /** The layout for a bounds rectangle. Takes bounds rather than assuming
        1200×780 so a test can prove the derivation is proportional rather than
        hard-coded — and so a future non-design size fails visibly. */
    static ChassisLayout forBounds (juce::Rectangle<int>) noexcept;
};

class Chassis final : public juce::Component
{
public:
    explicit Chassis (ForroBoxLookAndFeel&);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** The layout as last laid out. The tests read this, so the geometry they
        assert is the geometry that was painted. */
    const ChassisLayout& getLayout() const noexcept { return layout; }

private:
    void paintHeader (juce::Graphics&, juce::Rectangle<int>) const;
    void paintMatrix (juce::Graphics&, juce::Rectangle<int>) const;
    void paintStrip (juce::Graphics&, juce::Rectangle<int>, int channelIndex) const;
    void paintSidePanel (juce::Graphics&, juce::Rectangle<int>) const;
    void paintSequencer (juce::Graphics&, juce::Rectangle<int>) const;
    void paintFooter (juce::Graphics&, juce::Rectangle<int>) const;

    /** `inset 0 1px 0 <highlight>` — the top edge of a raised panel. */
    void paintRaisedHighlight (juce::Graphics&, juce::Rectangle<int>) const;

    /** The inset well shadow, as a vertical gradient down from the top edge.
        A real Gaussian inner shadow is not worth a blur pass here: the design's
        `inset 0 2px 6px` reads as a short dark gradient at the top edge. */
    void paintWellShadow (juce::Graphics&, juce::Rectangle<int>, juce::Colour, float depth) const;

    ForroBoxLookAndFeel& lnf;
    ChassisLayout layout;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Chassis)
};

} // namespace forrobox
