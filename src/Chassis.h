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

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Button.h"
#include "Fader.h"
#include "HitVisualiser.h"
#include "KitOverlay.h"
#include "Surface.h"
#include "StepSnapshot.h"
#include "LookAndFeel.h"
#include "ParameterIDs.h"
#include "Knob.h"
#include "ProportionAttachment.h"
#include "ToggleAttachment.h"
#include "Typography.h"

class ForroBoxAudioProcessor;

namespace forrobox
{

class HeaderBar;
class FooterBar;
class SequencerGrid;

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

/** A CSS flex row with `align-items: center` is as tall as its TALLEST
    child. Not as tall as the child someone happened to write down.

    Two boxes in this stack were found short because each restated ONE
    child's size: `kPatternRowHeight` said 20 where the 26 px arrow beside
    the screen made it 26, and `kSubDotsRowHeight` said 8 where the 9 px
    label beside the circles made it 9. Both were patched as they were
    found, which left five more boxes with the same shape and no reason to
    believe the next one was right — `kMuteSoloHeight` in particular, whose
    buttons are sized to the ROW, so nothing could have noticed.

    So the law is written once and every content-sized box goes through it.
    `/simplify` found the pattern; fixing each box as it surfaced was the
    wrong altitude. */
template <typename... Heights>
constexpr int flexRow (int firstChild, Heights... otherChildren) noexcept
{
    // A fold rather than juce::jmax, which needs at least two arguments. A row
    // with ONE child still goes through here: the height is the max of its
    // children and the count is not the point.
    auto tallest = firstChild;
    ((tallest = static_cast<int> (otherChildren) > tallest ? static_cast<int> (otherChildren)
                                                           : tallest), ...);
    return tallest;
}

/** `align-items: center`: one box placed vertically in the middle of its row.

    The other half of the flex law `flexRow` records — that one gives the row its
    height, this one places a child in it. Both region layouts had written it out
    as an identical local lambda, applied 23 times between them, and it is NOT
    substitutable by `withSizeKeepingCentre` or `getCentreY`: those round
    differently for an odd row height against an even box, so a future author
    reaching for the JUCE idiom in the sequencer would move boxes by a pixel.
    Hoisted by /simplify at 04-05, for the reason flexRow's own comment gives —
    fixing each box as it surfaces is the wrong altitude. */
inline juce::Rectangle<int> centredInRow (juce::Rectangle<int> row,
                                          juce::Rectangle<int> box) noexcept
{
    // `inline`, not `constexpr`, unlike flexRow and textBox beside it:
    // juce::Rectangle::withY is not constexpr, so Clang rejects the function
    // outright with -Winvalid-constexpr while GCC accepts it. Nothing here
    // needs a constant expression — the two above do, because ChassisLayout's
    // static constants are built from them.
    return box.withY (row.getY() + (row.getHeight() - box.getHeight()) / 2);
}

/** `repeat(N, 1fr)` with a gap: the box for one cell of an evenly tiled row.

    Placed from EXACT FRACTIONAL edges and rounded, so the remainder is spread
    across the row instead of accumulating in the last cell. That is the whole
    law, and it is the part that is easy to get wrong: summing rounded widths
    leaves the last cell a pixel short of the right edge, which is what the
    strip and pad tiling tests each assert independently.

    Written out twice before this — `ChassisLayout::forBounds` for the five
    channel strips and `SequencerLayout::padBounds` for the sixteen pads — with
    each copy carrying a comment pointing at the other. A law that needs a
    comment naming its other home is a law that wants hoisting, which is the
    argument `centredInRow` above makes for itself. Found by /simplify at 05-01.

    Horizontal only: both callers tile across and neither tiles down. */
inline juce::Rectangle<int> tileAcross (juce::Rectangle<int> row, int index,
                                        int count, int gap) noexcept
{
    if (count <= 0 || ! juce::isPositiveAndBelow (index, count))
        return {};

    const auto gaps = static_cast<float> (gap * (count - 1));
    const auto each = (static_cast<float> (row.getWidth()) - gaps) / static_cast<float> (count);

    const auto left = static_cast<float> (row.getX())
                    + static_cast<float> (index) * (each + static_cast<float> (gap));

    return juce::Rectangle<int>::leftTopRightBottom (juce::roundToInt (left), row.getY(),
                                                     juce::roundToInt (left + each),
                                                     row.getBottom());
}

/** The CSS box model for a content-sized text row: the type scale's own px
    height, plus the declared padding and border.

    ASKS the type scale, the way `kKnobCellHeight` asks the knob. Rounded up
    rather than truncated: `kSubDotsRowHeight`'s hand-written ternary
    truncated while `Button::preferredHeight` rounded, so one law had two
    roundings. */
constexpr int textBox (type::Style style, int padY = 0, int border = 0) noexcept
{
    return type::boxHeight (style, padY, border);
}

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

    /// The LOAD button beside the 12.5 px sample name (css:290-299).
    static constexpr int kSampleSlotHeight = flexRow (Button::heightOf (Button::Variant::load),
                                                      textBox (type::Style::sampleName));

    static constexpr int kHitVisualiserMarginTop = 9;   ///< css:304 .hitviz
    static constexpr int kHitVisualiserHeight    = 14;

    static constexpr int kStripDividerHeight = 1;    ///< css:321 .strip-div
    static constexpr int kStripDividerMargin = 11;

    static constexpr int kKnobGridRowGap = 9;        ///< css:324 .knob-grid gap 9px 6px
    static constexpr int kKnobGridColGap = 6;
    static constexpr int kKnobGridCols   = 2;
    static constexpr int kStripKnobSize  = 32;       ///< PLANNING.md:286

    static constexpr int kPatternRowMarginTop = 2;   ///< css:328 .pattern-row
    static constexpr int kPatternRowGap       = 5;

    /// css:332 `.pat-screen { padding: 4px 6px }`. Named so the derivation
    /// above reads from the number rather than restating it in a comment — the
    /// comment said "4+6" while the sum said 8, and nothing cross-checked
    /// either, so the wrong comment was the only source a reader had.
    static constexpr int kPatternScreenPadY = 4;
    static constexpr int kPatternScreenPadX = 6;
    static constexpr int kPatternScreenBorder = 1;   ///< css:331 `border: 1px solid var(--line)`

    /// .pat-screen: the 10 px mono row plus its padding and border (css:329-333).
    /// css:332 declares `padding: 4px 6px`; 6 is the HORIZONTAL half.
    static constexpr int kPatternScreenHeight =
        textBox (type::Style::patternScreen, kPatternScreenPadY, kPatternScreenBorder);

    /** The row is as tall as its TALLEST child, and that is not the screen.

        `.pattern-row` is `display:flex; align-items:center` with three
        children: two `.arrow-btn`, fixed at 26 px by css:231, either side of
        the 20 px screen. A flex row takes the maximum, so the browser's row is
        26 and this constant was 20 — every box below the cycler sat 6 px high,
        and no check could see it because 04-02's stack assertions compare the
        derivation against the same constant it is built from.

        The fix is the one `kKnobCellHeight` already records: ASK the component
        rather than restate its geometry. That this plan's boundaries froze
        StripLayout is why it is a jmax and a comment rather than a silent 26 —
        confirmed with the user before the stack moved. */
    static constexpr int kPatternRowHeight = flexRow (kPatternScreenHeight, Button::kArrowHeight);

    static constexpr int kMuteSoloMarginTop = 8;     ///< css:335 .ms-row
    static constexpr int kMuteSoloGap       = 5;

    /// Two `.ms-btn`, which are the row's only children (css:335-341).
    static constexpr int kMuteSoloHeight =
        flexRow (Button::heightOf (Button::Variant::muteSolo));

    static constexpr int kGhostRowMarginTop  = 10;   ///< css:346 .ghost-row
    static constexpr int kGhostLabelGap      = 5;    ///< css:347 .gl margin-bottom

    /// `.gl` is a flex row of the 9 px caption and the 10 px mono readout (css:348-349).
    static constexpr int kGhostLabelHeight = flexRow (textBox (type::Style::stripMicroLabel),
                                                      textBox (type::Style::ghostValue));
    /// The fader's own box. `= fader::kHeight`, not a second 20: the two were
    /// the same law in two headers, and the only thing comparing them was a
    /// bespoke check in verify-geometry.py that this now makes unnecessary.
    static constexpr int kFaderHeight = fader::kHeight;

    static constexpr int kSubDotsMarginTop = 9;      ///< css:351 .subdots — STRIP 5 ONLY
    static constexpr int kSubDotSize       = 8;      ///< css:353 .subdot width
    static constexpr int kSubDotGap        = 5;
    static constexpr int kSubDotsLabelInset = 2;     ///< css:354 .subdots-label margin-left

    /** The `.subdots` row, which is NOT one dot tall.

        `display:flex; align-items:center` again (css:351), and its fifth child
        is a 9 px type row — taller than the four 8 px circles. The same law
        `kPatternRowHeight` records one box above, applied here for consistency
        with the ruling that settled it rather than left as the one place the
        newly-stated rule does not hold. Found by `/code-review` on 04-03. */
    static constexpr int kSubDotsRowHeight = flexRow (kSubDotSize, textBox (type::Style::stripMicroLabel));

    // ── the header, PLANNING.md:246-263 left to right ──────────────────────
    //
    // `display:flex; align-items:center; gap:12px; padding:0 16px` (css:150).
    // A flex row of SIX clusters whose heights all differ — the exact shape
    // that made two strip boxes short, so every box here goes through flexRow
    // and textBox and nothing restates a child's size.

    static constexpr int kHeaderPadX = 16;   ///< css:151 .header padding 0 16px
    static constexpr int kHeaderGap  = 12;   ///< css:150

    /// `gap: 6px` between the two transport buttons — css:188.
    static constexpr int kTransportGap = 6;

    /// `gap: 16px` inside the recessed group, and its own padding — css:201-202.
    static constexpr int kGlobalKnobsGap        = 16;
    static constexpr int kGlobalKnobsPadX       = 18;
    static constexpr int kGlobalKnobsPadTop     = 6;
    static constexpr int kGlobalKnobsPadBottom  = 5;
    static constexpr int kGlobalKnobsRadiusExtra = 3;   ///< css:206 `calc(var(--r) + 3px)`

    /// `width: 1px; height: 42px` — the divider between the two knobs, css:212.
    static constexpr int kGlobalKnobDividerWidth  = 1;
    static constexpr int kGlobalKnobDividerHeight = 42;

    /// `gap: 11px` knob→meta and `gap: 4px` within the meta — css:210-211.
    static constexpr int kGlobalKnobMetaGap  = 11;
    static constexpr int kGlobalKnobStackGap = 4;

    static constexpr int kGlobalKnobSize = 54;      ///< PLANNING.md:381

    /// `min-width: 46px`, `padding: 2px 10px` — the readout screen, css:216.
    static constexpr int kGlobalKnobReadMinWidth = 46;
    static constexpr int kGlobalKnobReadPadX = 10;
    static constexpr int kGlobalKnobReadPadY = 2;

    /** `radial-gradient(120% 160% at 50% -30%, <zabumba at 12%>, --sunken)` and
        `border: 1px color-mix(--c-zabumba 25%, --line-strong)` — css:204-207.

        The origin sits ABOVE the box, which the pad's did not. Same FillType
        technique; see Chassis::paintGlobalKnobGroup. */
    static constexpr float kGlobalKnobsRadiusX = 1.20f;
    static constexpr float kGlobalKnobsRadiusY = 1.60f;
    static constexpr float kGlobalKnobsOriginX = 0.50f;
    static constexpr float kGlobalKnobsOriginY = -0.30f;
    static constexpr float kGlobalKnobsTintPct = 12.0f;   ///< the accent's weight in the gradient
    static constexpr float kGlobalKnobsBorderPct = 25.0f; ///< and in the border
    static constexpr int   kGlobalKnobsGlowRadius = 18;   ///< `0 0 18px` — css:207
    static constexpr float kGlobalKnobsInsetAlpha = 0.40f;
    static constexpr float kGlobalKnobsInsetAlphaLight = 0.12f;  ///< css:209, its OWN shadow

    /// `gap: 6px` in the preset cluster and `min-width: 104px` on its screen — css:222, 227.
    static constexpr int kPresetGap = 6;
    static constexpr int kPresetScreenMinWidth = 104;
    static constexpr int kPresetScreenPadX = 8;    ///< css:227 padding 5px 8px
    static constexpr int kPresetScreenPadY = 5;

    /// `gap: 7px` between the STYLE label and its segments — css:238.
    static constexpr int kStyleGap = 7;

    /** The header's clusters, left to right, all reserved here for the reason
        `StripLayout` reserves the strip's: a rectangle only `paint` can see is
        a rectangle no test can check, and 04-01 learned that by discarding the
        strip's content rect. */
    struct HeaderLayout
    {
        juce::Rectangle<int> logoMark;
        juce::Rectangle<int> wordmark;
        juce::Rectangle<int> bpmField;
        juce::Rectangle<int> syncButton;
        juce::Rectangle<int> halfButton;
        juce::Rectangle<int> doubleButton;
        juce::Rectangle<int> playButton;
        juce::Rectangle<int> stopButton;
        juce::Rectangle<int> globalKnobs;      ///< the recessed group as a whole
        juce::Rectangle<int> swingKnob;
        juce::Rectangle<int> swingName, swingRead;
        juce::Rectangle<int> knobDivider;
        juce::Rectangle<int> cachacaKnob;
        juce::Rectangle<int> cachacaName, cachacaRead;
        juce::Rectangle<int> presetPrev, presetScreen, presetNext;
        juce::Rectangle<int> styleLabel, styleSegments;
    };

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

        /** The trigger LED, inside `headRow` and left of the index.

            04-02 reserved every box in the strip's interior stack — and missed
            this one, because it is inside the head row rather than a member of
            the stack. `.strip-head-r` is a flex row (css:275) holding the LED
            and the index with a 7 px gap, right-aligned; the LED is therefore
            one index-width plus one gap in from the right edge. Found by
            reading PLANNING.md:480 at 05-02 planning. */
        juce::Rectangle<int> trigLed;
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

    /** One knob cell: the dial plus whatever the knob itself needs below it.

        ASKS the knob. This used to re-add the gap and the label height under a
        comment claiming it asked — the fifth law-written-twice in this project,
        and the worst-guarded: the cell height and preferredHeight were each
        asserted against their own copy, so a change to what a knob needs below
        its dial would have moved the knob out of the cell reserved for it with
        every one of the ~350 geometry checks still green. */
    static constexpr int kKnobCellHeight = Knob::preferredHeight (kStripKnobSize, true);

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

    /** One knob slot: which channel parameter it drives and how it draws.

        THE table, read by both `Chassis::attachParameters` and the tests. It
        used to live inside attachParameters with the tests hand-copying the
        order into two local arrays — so reordering it moved every knob AND
        both expectations together, and the test agreed with itself. That is
        02-01's "keyed both sides off the same stale list", exactly. */
    struct KnobSlot
    {
        const char*     param;
        const char*     label;
        Knob::Polarity  polarity;
    };

    /** The sample each strip currently names — PLANNING.md:281 and
        app.js:229-231, which agree.

        ONE table beside `knobSlots`, for the reason that one exists: the tests
        read it too, so a name cannot be right in production and stale in a
        test. A v0.1 STUB — nothing loads a sample, and no parameter or state
        field backs these. */
    static const std::array<juce::String, static_cast<size_t> (kNumStrips)>& sampleNames();

    /** The pattern cycler's screen, the sub-dot row's label and the two arrow
        glyphs — app.js:100-102, 190-192 and 218-220. All three are stubs, so
        each is the one literal the prototype shows and not a formatter. */
    /** The stub literals, as juce::Strings built from EXPLICIT UTF-8.

        Not `const char*`. juce::String and juce::StringRef disagree about what
        a `const char*` is — `String::String (const char*)` goes through
        `CharPointer_ASCII` (`juce_String.cpp:308`, whose own comment recommends
        `String (CharPointer_UTF8 (...))`) while `StringRef::StringRef (const
        char*)` takes the bytes as UTF-8 (`juce_String.cpp:2178`). U+2039 handed
        to a Button rendered as "a<EUR>1/2" on the reference sheet for exactly
        that reason, and the fix was `fromUTF8` at the call site — which is
        load-bearing at two of the four sites, redundant at the other two, and
        indistinguishable at any of them.

        Declaring the type makes the ASCII overload unreachable, so no call site
        can forget. Found by /simplify.

        Each `\xNN` escape is still closed with a string break: "\xb7 CX" would
        otherwise read the C as a fifth hex digit, the trap
        PluginProcessor.cpp:686 records. */
    static const juce::String& patternScreenText();
    static const juce::String& subDotsLabel();
    static const juce::String& arrowPrev();   ///< U+2039
    static const juce::String& arrowNext();   ///< U+203A

    /// `.subdot { opacity: 0.85 }` — css:353.
    static constexpr float kSubDotOpacity = 0.85f;

    /// The four bateria pieces the sub-dots show — ids::lanes' tail.
    static constexpr int kNumSubDots = 4;

    static constexpr std::array<KnobSlot, 4> knobSlots {{
        { ids::vol,   "VOL",   Knob::Polarity::unipolar },
        // PITCH and PAN are the bipolar pair — controls.js via app.js:181,183.
        { ids::pitch, "PITCH", Knob::Polarity::bipolar },
        { ids::decay, "DECAY", Knob::Polarity::unipolar },
        { ids::pan,   "PAN",   Knob::Polarity::bipolar },
    }};

    /** The interior of one strip, derived the same way `paintStrip` paints it.

        Takes the channel because ONE box is channel-dependent — the bateria
        sub-dots row exists on that strip only. There was a one-argument
        overload whose only caller was this one; it was public, and returned a
        `subDots` that is correct for four strips and silently wrong for the
        fifth. */
    static StripLayout stripInteriorOf (juce::Rectangle<int> strip, int channelIndex) noexcept;

    /** The header's clusters, derived the same way `paintHeader` paints them. */
    static HeaderLayout headerInteriorOf (juce::Rectangle<int> header) noexcept;

    /** The four regional codes, from the SAME table Profiles.cpp and
        verify-profiles.py already cross-check against data.js. Four three-letter
        strings are exactly the kind of thing that gets retyped. */
    static juce::StringArray profileCodes();

    /** Which segment a persisted profile id lights, or 0 for an unknown one.

        Looked up in `ids::profileInfos`, the table `profileCodes` reads and
        `verify-profiles.py` cross-checks against data.js — so the order the
        segments are drawn in and the order they are matched in cannot
        disagree. */
    static int indexOfProfile (juce::StringRef profileId);

    /** The preset cycler's single label. A STUB: `PLANNING.md:841` lists eight
        and says a real preset system is the intended behaviour, so this does
        not cycle either — a label that changes while nothing else does is the
        dishonest kind of stub. */
    static const juce::String& presetStubLabel();

    /** SWING and CACHAÇA, in that order. Read by the layout to measure the meta
        column and by paint to draw it — one table, for the reason
        `profileCodes` is one. */
    static const std::array<juce::String, 2>& globalKnobNames();



    /** The layout for a bounds rectangle. Takes bounds rather than assuming
        1200×780 so a test can prove the derivation is proportional rather than
        hard-coded — and so a future non-design size fails visibly. */
    static ChassisLayout forBounds (juce::Rectangle<int>) noexcept;
};

class Chassis final : public juce::Component
{
public:
    explicit Chassis (ForroBoxLookAndFeel&);
    ~Chassis() override;

    /** Builds the twenty strip knobs and binds them to real parameters.

        A separate step rather than a constructor argument: the chassis is a
        surface, and every geometry test builds one with no processor at all.
        Calling this adds children; it changes no geometry, so the layout the
        tests assert is the layout the populated chassis uses.

        The knobs are children of the CHASSIS, not of the editor, because the
        editor's scale transform is applied to the chassis — a knob parented
        anywhere else would not scale with it. */
    void attachParameters (juce::AudioProcessorValueTreeState&, class ValueTooltip*);

    /** The two bars, for the tests that drive their polls and read their
        layouts. Never null — both exist from construction. */
    /** The visualiser poll and one channel's level, for the tests.

        CALLED, never waited for — 04-04's lesson, where three checks failed on
        MSVC's clock rather than on the code. The plugin drives these from a
        60 Hz timer; the tests drive them directly, so nothing in the suite
        depends on a timer firing. */
    void pollVisualisersForTest() { pollVisualisers(); }

    /** The step the visualisers last fired, for the tests. Should track the
        PLAYHEAD's step — the corrected one — not the processor's raw published
        step, which runs an output delay ahead of it. */
    int lastFiredStepForTest() const noexcept { return lastStepShown; }

    float hitVisualiserLevelForTest (int channel) const
    {
        return juce::isPositiveAndBelow (channel, (int) hitVisualisers.size())
                 ? hitVisualisers[(size_t) channel].getLevel()
                 : 0.0f;
    }

    HeaderBar& getHeaderBar() const noexcept { return *headerBar; }
    FooterBar& getFooterBar() const noexcept { return *footerBar; }
    SequencerGrid& getSequencerGrid() const noexcept { return *sequencerGrid; }

    /** The bateria kit panel. Never null — it exists from construction, hidden,
        the way the three bars exist from construction. */
    KitOverlay& getKitOverlay() const noexcept { return *kitOverlay; }

    /** Drive the header bar's poll directly. Forwards to
        `HeaderBar::refreshFromProcessor`, which is where the behaviour now
        lives; kept here because the tests reach the header through the chassis
        and the TIMER is a scheduling detail, not the behaviour. */
    void refreshHeaderFromProcessor();

    void paint (juce::Graphics&) override;

    /** The BATERIA strip's sub-dots row opens the kit panel — `PLANNING.md:518`.

        On the CHASSIS rather than as a Button, because the row is painted
        furniture (`paintSubDots`, 04-03) and the only interactive thing in it;
        wrapping it in a component would put a control inside a strip that is
        drawn, not composed, for one rectangle. */
    void mouseUp (const juce::MouseEvent&) override;
    void resized() override;

    /** The layout as last laid out. The tests read this, so the geometry they
        assert is the geometry that was painted. */
    const ChassisLayout& getLayout() const noexcept { return layout; }

private:
    void paintMatrix (juce::Graphics&, juce::Rectangle<int>) const;
    void paintStrip (juce::Graphics&, juce::Rectangle<int>, int channelIndex) const;
    void paintSidePanel (juce::Graphics&, juce::Rectangle<int>) const;

    ForroBoxLookAndFeel& lnf;
    ChassisLayout layout;

    /** One placed knob: the component, its attachment, and WHICH cell it goes
        in.

        The cell is stored rather than derived from the knob's index. `i / 4`
        and `i % 4` assumed exactly four knobs per channel in order, so a single
        skipped parameter — the `continue` in attachParameters, reachable in
        release where the jassert is compiled out — shifted every later knob
        into the wrong strip, and a second attachParameters call indexed
        stripLayouts out of bounds.

        Declared so the knobs outlive their attachments (destruction runs in
        reverse), though ~KnobAttachment no longer depends on that. */
    struct PlacedKnob
    {
        std::unique_ptr<Knob> knob;
        std::unique_ptr<class KnobAttachment> attachment;
        int channel { 0 };
        int slot { 0 };
    };

    std::vector<PlacedKnob> stripKnobs;

    /** One strip's non-knob controls: three honest stubs, two real toggles and
        one real fader.

        A stub is a Button with no attachment and no `onClick` — it draws, it
        hovers, and it changes nothing. `PLANNING.md` lists sample loading, the
        pattern cycler and the bateria sub-kit as post-v0.1, and 02-04's rule is
        that a guarantee with no caller is not a guarantee: none of them is
        given a parameter, a callback or a state field that nothing reads.

        Each attachment is declared AFTER the control it binds, so ~Chassis —
        where destruction runs in reverse — tears the binding down first.

        That is the ONLY path the ordering covers. Assignment does not run in
        reverse: an implicitly-defined move-assignment assigns in declaration
        order, so `controls = {}` frees each control while its attachment is
        still holding a reference. `attachParameters` resets the three
        attachments explicitly for exactly that reason. Found by /code-review
        on 04-03, and the comment that used to sit here claimed the ordering
        made them "obviously safe" on every path. */
    struct StripControls
    {
        std::unique_ptr<Button> load;          ///< STUB
        std::unique_ptr<Button> patternPrev;   ///< STUB
        std::unique_ptr<Button> patternNext;   ///< STUB
        std::unique_ptr<Button> mute;
        std::unique_ptr<Button> solo;
        std::unique_ptr<Fader>  ghost;

        std::unique_ptr<ToggleAttachment> muteAttachment;
        std::unique_ptr<ToggleAttachment> soloAttachment;
        std::unique_ptr<ProportionAttachment<Fader>> ghostAttachment;

    };

    std::array<StripControls, static_cast<size_t> (ChassisLayout::kNumStrips)> stripControls;

    /** One level per channel, driving both the head LED and the activity meter.

        Held by the CHASSIS rather than by each strip, because the strips are
        painted by `paintStrip` and have no component of their own — and because
        one poll must drive all five. Five timers would be five decays able to
        drift apart. */
    std::array<HitVisualiser, static_cast<size_t> (ChassisLayout::kNumStrips)> hitVisualisers;

    /** The processor, for the visualisers' mute/solo gate and the publication.
        Null in every geometry test, which builds a chassis with no processor at
        all — so every use is guarded rather than assumed. */
    ::ForroBoxAudioProcessor* attachedProcessor { nullptr };

    /** 60 fps, the same rate as the playhead: x0.82 per frame is a per-FRAME law
        (`app.js:253`), so the frame rate is part of the decay's meaning. */
    PollTimer visualiserPoll;

    /** The publication count last seen, so a hit is read ONCE. The snapshot
        holds the last step's velocities continuously; triggering off its
        contents would re-trigger every frame and the meter would never decay. */
    int lastPublicationSeen { 0 };

    /** What each step PLAYED, recorded as it is published and fired when the
        corrected position reaches it.

        Task 1 pulls the PLAYHEAD back by `outputDelaySamples()` so the sweep
        matches what is heard, but the step publication is not corrected — so an
        LED fired the moment a step is published flashes ahead of the line that
        is supposed to be reaching it.

        The first answer was a frame-count delay queue: hold each hit for
        `round(delaySeconds * pollHz)` frames. It worked, and it was one level
        too shallow — it re-derived in FRAMES a correction the processor already
        publishes exactly, in steps, so one quantity had two expressions in two
        units that could disagree by half a frame. It also coupled LED timing to
        the poll rate, while `HitVisualiser::advance` two functions away is
        explicitly hardened against dropped frames.

        `PluginProcessor.h` had already named the right shape — *"the cheaper
        answer is for the LED to fire when this position reaches the step"* — and
        said Task 3 did it, which it did not. Found by /simplify; the comment was
        right and the code was not.

        Now the position decides WHEN and the publication decides WHAT, so the
        LED and the playhead read one scalar and cannot disagree by
        construction. The sample rate, the frame arithmetic and the queue all
        go. */
    std::array<std::array<std::uint8_t, forrobox::State::kNumLanes>,
               static_cast<size_t> (forrobox::State::kMaxSteps)> playedVelocities {};

    /** The step the sweep last reached, so each is fired once. */
    int lastStepShown { -1 };

    void pollVisualisers();

    /** The header, which owns itself.

        Split out at 04-05 on /simplify's recording: two build/paint/refresh/
        resize triads in one class was a coincidence, and the footer was the
        third. The chassis places it and paints nothing of it.

        Its bounds are `layout.header`, which starts at the chassis's origin —
        so a header control's bounds read the same in either coordinate space
        and the tests that compare them against `layout.headerLayout` are
        unaffected by the move. */
    std::unique_ptr<HeaderBar> headerBar;

    /** The footer, which owns itself for the same reason — and whose geometry,
        unlike the header's, lives in its own file. `ChassisLayout` carries the
        header's because the tests read it and it predates the split. */
    std::unique_ptr<FooterBar> footerBar;

    /** The sequencer, which owns its own layout for the reason the other two do:
        it sits at y=528, so a child's parent-relative bounds alias into the
        header's boxes exactly as the footer's did. */
    std::unique_ptr<SequencerGrid> sequencerGrid;

    /** LAST child, so it paints over everything — css:554's `z-index: 40`. */
    std::unique_ptr<KitOverlay> kitOverlay;

    /** The strip's filled boxes. Separated from paintStrip only because that
        method was already the longest in the file and these six boxes are one
        plan's worth of content. */
    void paintSampleSlot (juce::Graphics&, const ChassisLayout::StripLayout&, int channel) const;
    void paintPatternCycler (juce::Graphics&, const ChassisLayout::StripLayout&) const;
    void paintGhostLabel (juce::Graphics&, const ChassisLayout::StripLayout&, int channel) const;
    void paintSubDots (juce::Graphics&, const ChassisLayout::StripLayout&) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Chassis)
};

} // namespace forrobox
