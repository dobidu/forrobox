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

    /** One strip's interior, in the same spirit as the region rects above: the
        head row, the accent bar and the box left for the controls.

        These were `removeFromTop` locals inside `paintStrip`, so the only
        rectangle a later plan or a test could see was the strip's outer bounds
        — and the tests re-added kStripPadTop + kHeadRowHeight + the bar margins
        by hand at three separate sites to find the bar and the head row. They
        agreed with the paint code by coincidence, not by construction:
        reordering the pads would have left them probing panel fill and passing.

        `controls` is 04-02/04-03/04-04's box. It used to be computed and then
        dropped on the floor with `ignoreUnused`, which made the plan's "reserve
        their boxes so those plans drop components into settled geometry"
        deliverable unreachable by the plans that need it. */
    struct StripLayout
    {
        juce::Rectangle<int> headRow;
        juce::Rectangle<int> accentBar;
        juce::Rectangle<int> controls;
    };

    juce::Rectangle<int> header;
    juce::Rectangle<int> main;
    juce::Rectangle<int> matrix;
    juce::Rectangle<int> sidePanel;
    juce::Rectangle<int> sequencer;
    juce::Rectangle<int> footer;
    std::array<juce::Rectangle<int>, kNumStrips> strips;
    std::array<StripLayout, kNumStrips> stripLayouts;

    /** The interior of one strip, derived the same way `paintStrip` paints it. */
    static StripLayout stripInteriorOf (juce::Rectangle<int> strip) noexcept;

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
