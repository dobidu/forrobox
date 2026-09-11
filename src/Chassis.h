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
#include "Typography.h"

namespace forrobox
{

/** `paintStrip` binds a strip to its colour with `static_cast<theme::Accent>
    (channelIndex)`, so `theme::accentSpecs` and `ids::channelInfos` must stay
    in the same order. Nothing else enforced that: the accent hexes are
    cross-checked against the CSS and the channel ids against data.js, but the
    two tables were never compared to each other — and the test binds them with
    the SAME cast, so it could not have seen a divergence either.

    Inserting or reordering a channel would repaint every strip in another
    instrument's colour with no check failing. This is the `timbreChoices`
    order bug from 03-03, one subsystem over, so it is a compile error instead.

    Compared via the css name, which is `"--c-" + the channel id` for all five
    rows — so the assertion also pins that naming convention. */
namespace detail
{
constexpr bool sameString (const char* a, const char* b) noexcept
{
    while (*a != '\0' && *a == *b)
    {
        ++a;
        ++b;
    }

    return *a == *b;
}

constexpr bool accentsFollowChannelOrder() noexcept
{
    if (theme::accentSpecs.size() != ids::channelInfos.size())
        return false;

    for (size_t i = 0; i < theme::accentSpecs.size(); ++i)
    {
        // Skip the "--c-" prefix, whose length is asserted below rather than
        // assumed, so a renamed convention fails here too.
        const char* cssName = theme::accentSpecs[i].cssName;

        if (! (cssName[0] == '-' && cssName[1] == '-' && cssName[2] == 'c' && cssName[3] == '-'))
            return false;

        if (! sameString (cssName + 4, ids::channelInfos[i].id))
            return false;

        // And the row sits at its own enum's index, so the cast is valid.
        if (static_cast<size_t> (theme::accentSpecs[i].accent) != i)
            return false;
    }

    return true;
}
} // namespace detail

static_assert (detail::accentsFollowChannelOrder(),
               "theme::accentSpecs and ids::channelInfos have diverged in order or naming — "
               "Chassis::paintStrip casts a channel index straight to a theme::Accent, so a "
               "strip would paint in another instrument's colour");

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

    /** Derived from the channel table, not a third copy of "5". */
    static constexpr int kNumStrips = static_cast<int> (ids::channelInfos.size());

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

    /** `box-shadow: 0 0 10px <accent at calc(var(--accent-i) * 35%)>`. Named
        rather than a bare 0.35f at the call site, like kAnchorAccentWeight. */
    static constexpr float kAccentGlowOpacity = 0.35f;
    static constexpr int kHeadRowHeight = 16;

    /** `color-mix(in srgb, var(--panel) 88%, var(--c-zabumba))` — the anchor
        tint. 88% panel means 12% accent. */
    static constexpr float kAnchorAccentWeight = 0.12f;

    /** `linear-gradient(180deg, <raised + 3% white>, <raised>)` on the header.
        Named for the same reason as the weight above: it was a bare 0.03f in
        Chassis.cpp and again in the test, so a change to one left the other
        asserting the old recipe. */
    static constexpr float kHeaderGradientWeight = 0.03f;

    // ── the rest of the strip stack, PLANNING.md:276-294 in order ──────────
    //
    // Every margin, gap, border and declared height below is quoted from
    // forrobox.css. The TEXT-row heights are the only derived numbers: the CSS
    // lets content size those rows, so each is computed as the type scale's own
    // px height plus the declared padding and border. That is a deliberate
    // deviation from "measured in a browser" and it is why AC-5 asserts the
    // stack tiles `controls` exactly rather than asserting browser pixels.

    static constexpr int kSampleSlotMarginTop = 2;    ///< css:288 .sample-slot
    static constexpr int kSampleSlotGap      = 7;
    /// The LOAD button is the tallest child: 9 px label + 4+4 padding + 1+1 border (css:295-299).
    static constexpr int kSampleSlotHeight   = 9 + 8 + 2;

    static constexpr int kHitVisualiserMarginTop = 9;   ///< css:304 .hitviz
    static constexpr int kHitVisualiserHeight    = 14;

    static constexpr int kStripDividerHeight = 1;    ///< css:321 .strip-div
    static constexpr int kStripDividerMargin = 11;

    static constexpr int kKnobGridRowGap = 9;        ///< css:324 .knob-grid gap 9px 6px
    static constexpr int kKnobGridColGap = 6;
    static constexpr int kKnobGridCols   = 2;
    static constexpr int kStripKnobSize  = 32;       ///< PLANNING.md:286
    static constexpr int kKnobLabelGap   = 3;        ///< css:357 .fb-knob gap

    static constexpr int kPatternRowMarginTop = 2;   ///< css:328 .pattern-row
    static constexpr int kPatternRowGap       = 5;
    /// .pat-screen: 10 px mono + 4+4 padding + 1+1 border (css:329-333).
    static constexpr int kPatternRowHeight    = 10 + 8 + 2;

    static constexpr int kMuteSoloMarginTop = 8;     ///< css:335 .ms-row
    static constexpr int kMuteSoloGap       = 5;
    /// .ms-btn: 11 px mono + 5+5 padding + 1+1 border (css:336-341).
    static constexpr int kMuteSoloHeight    = 11 + 10 + 2;

    static constexpr int kGhostRowMarginTop  = 10;   ///< css:346 .ghost-row
    static constexpr int kGhostLabelGap      = 5;    ///< css:347 .gl margin-bottom
    static constexpr int kGhostLabelHeight   = 10;   ///< the mono NN% readout is the taller child
    /// .fb-fader: 8+8 padding around a 4 px track (css:376-377).
    static constexpr int kFaderHeight        = 20;

    static constexpr int kSubDotsMarginTop = 9;      ///< css:351 .subdots — STRIP 5 ONLY
    static constexpr int kSubDotSize       = 8;
    static constexpr int kSubDotGap        = 5;
    static constexpr int kSubDotsLabelInset = 2;     ///< css:354 .subdots-label margin-left

    /** One strip's interior — every box `PLANNING.md:276-294` lists, in order.

        These were `removeFromTop` locals inside `paintStrip`, so the only
        rectangle a later plan or a test could see was the strip's outer bounds
        — and the tests re-added kStripPadTop + kHeadRowHeight + the bar margins
        by hand at three separate sites to find the bar and the head row. They
        agreed with the paint code by coincidence, not by construction:
        reordering the pads would have left them probing panel fill and passing.

        04-01 then computed the leftover `controls` rect and dropped it on the
        floor with `ignoreUnused`, which made its own "reserve their boxes so
        those plans drop components into settled geometry" deliverable
        unreachable by the plans that needed it. So the WHOLE stack is reserved
        here, once, and filled over three plans:

            headRow        04-01  name + index
            accentBar      04-01
            sampleSlot     04-03  sample name + LOAD (a v0.1 stub)
            hitVisualiser  Ph. 5  activity meter
            dividerTop     04-02
            knobGrid       04-02  VOL / PITCH / DECAY / PAN
            dividerBottom  04-02
            patternCycler  04-03  < PAT 01 >
            muteSolo       04-03  M | S
            ghostLabel     04-03  "Ghost Prob" + NN%
            ghostFader     04-03
            subDots        04-03  four 8 px circles — STRIP 5 ONLY, empty elsewhere

        A box being empty is meaningful: `subDots` is empty on strips 1-4 because
        the row does not exist there, and a present-but-wrong rect on four strips
        would be worse than an absent one. */
    struct StripLayout
    {
        juce::Rectangle<int> headRow;
        juce::Rectangle<int> accentBar;
        juce::Rectangle<int> sampleSlot;
        juce::Rectangle<int> hitVisualiser;
        juce::Rectangle<int> dividerTop;
        juce::Rectangle<int> knobGrid;
        juce::Rectangle<int> dividerBottom;
        juce::Rectangle<int> patternCycler;
        juce::Rectangle<int> muteSolo;
        juce::Rectangle<int> ghostLabel;
        juce::Rectangle<int> ghostFader;
        juce::Rectangle<int> subDots;

        /** Everything below the accent bar — the box the stack above tiles.
            Kept so the tiling assertion has one rectangle to sum against. */
        juce::Rectangle<int> controls;

        /** The four 32 px knob cells inside `knobGrid`, in VOL / PITCH / DECAY /
            PAN order: row-major across two columns, each cell holding the dial
            plus its micro-label. Read this rather than re-deriving the gaps. */
        std::array<juce::Rectangle<int>, 4> knobCells;
    };

    /** The micro-label's own row height, taken FROM the type scale rather than
        retyped: `Style::knobMicroLabel` is 9 px (Typography.h:111). A literal
        here would be a second copy of a table value, which is the shape that
        produced 04-01's tracking bug. */
    static constexpr int kKnobLabelHeight =
        static_cast<int> (type::textStyles[static_cast<size_t> (type::Style::knobMicroLabel)].heightPx);

    static_assert (type::textStyles[static_cast<size_t> (type::Style::knobMicroLabel)].style
                       == type::Style::knobMicroLabel,
                   "textStyles is no longer indexed by its own enum, so kKnobLabelHeight is reading "
                   "some other row's height");

    /** One knob cell: the 32 px dial, the 3 px gap and the micro-label row. */
    static constexpr int kKnobCellHeight = kStripKnobSize + kKnobLabelGap + kKnobLabelHeight;

    /** Two rows of cells with one row gap between them. */
    static constexpr int kKnobGridHeight = 2 * kKnobCellHeight + kKnobGridRowGap;

    juce::Rectangle<int> header;
    juce::Rectangle<int> main;
    juce::Rectangle<int> matrix;
    juce::Rectangle<int> sidePanel;
    juce::Rectangle<int> sequencer;
    juce::Rectangle<int> footer;
    std::array<juce::Rectangle<int>, kNumStrips> strips;
    std::array<StripLayout, kNumStrips> stripLayouts;

    /** The interior of one strip, derived the same way `paintStrip` paints it.

        The channel index is needed because ONE box is channel-dependent: the
        bateria sub-dots row exists on that strip only. The one-argument form
        leaves `subDots` empty. */
    static StripLayout stripInteriorOf (juce::Rectangle<int> strip) noexcept;
    static StripLayout stripInteriorOf (juce::Rectangle<int> strip, int channelIndex) noexcept;

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
    void paintRaisedHighlight (juce::Graphics&, juce::Rectangle<int>, juce::Colour) const;

    /** The inset well shadow, as a vertical gradient down from the top edge.
        A real Gaussian inner shadow is not worth a blur pass here: the design's
        `inset 0 2px 6px` reads as a short dark gradient at the top edge. */
    void paintWellShadow (juce::Graphics&, juce::Rectangle<int>, juce::Colour, float depth) const;

    ForroBoxLookAndFeel& lnf;
    ChassisLayout layout;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Chassis)
};

} // namespace forrobox
