/* ============================================================================
   FORRÓ BOX — Segmented

   A radio group: one `--sunken` well holding N segments, exactly one lit.
   `STYLE` in the header (`CAM` / `CAR` / `PET` / `UNI`) and `OUTPUT` in the
   footer (`STEREO` / `MULTI-OUT`) are the same control with different labels,
   so this is one component and not two.

   It holds a selected index and no meaning. What a click DOES is the owner's —
   `STYLE` reflects the persisted profile and does nothing until Phase 6 wires
   the reload, and `OUTPUT` is 04-05's. A component that knew which parameter it
   drove could serve only one of them.

   css:240-252. The divider rule is `:last-child` and it is the one thing a loop
   over segments gets wrong, so it is asserted rather than assumed.
============================================================================ */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

#include "LookAndFeel.h"
#include "Theme.h"
#include "Typography.h"

namespace forrobox
{

namespace segmented
{
inline constexpr int kPadX = 9;    ///< css:248 .qs-btn padding 7px 9px
inline constexpr int kPadY = 7;
inline constexpr int kBorderWidth = 1;   ///< css:242 the group's border
inline constexpr int kDividerWidth = 1;  ///< css:247 border-right between segments
inline constexpr int kRadiusExtra = 1;   ///< css:242 `calc(var(--r) + 1px)`

/// `background: color-mix(in srgb, var(--fg) 8%, transparent)` on hover — css:251.
inline constexpr float kHoverGroundPct = 8.0f;

/// `inset 0 1px 2px rgba(0,0,0,0.3)` — css:243.
inline constexpr float kInsetAlpha = 0.30f;

/// `.out-toggle .ot { padding: 6px 10px }` — css:547.
inline constexpr int kOutPadX = 10;
inline constexpr int kOutPadY = 6;

/// `border-radius: var(--r)` with no `calc` — css:545, unlike the quick switch.
inline constexpr int kOutRadiusExtra = 0;
} // namespace segmented

class Segmented final : public juce::Component
{
public:
    /** The two box models, as DATA.

        `.quick-switch` (css:240-252) and `.out-toggle` (css:545-549) are the
        same control and NOT the same box: the out-toggle has no `--sunken`
        well, no `border-right` between its segments and no inset shadow, its
        padding is 6x10 rather than 7x9, and its radius is a bare `var(--r)`.
        Four differences is a variant, not a second component — Button's six
        rows record the same ruling, and its two booleans-as-data are this
        table's shape.

        What they share is everything that matters: one group, one lit segment,
        `--active` on `--bg` for it, the same hover, the same geometry law. */
    enum class Variant
    {
        quickSwitch,   ///< STYLE, in the header
        outToggle,     ///< OUTPUT, in the footer
    };

    struct VariantSpec
    {
        Variant variant;
        int     padX, padY;
        int     radiusExtra;

        /** The `--sunken` well behind the segments. A BOOL, because "no
            background declared at all" is an absence rather than a magnitude —
            the one difference of the three the stylesheet states that way. */
        bool    sunkenGround;

        /** `border-right` between segments — 0 for the out-toggle, which is
            `gap: 0` with no divider rule.

            A WIDTH, not a flag. It was a bool gating `segmented::kDividerWidth`,
            which put the number outside the table and three branches inside the
            code — and a zero-width fillRect is already a no-op, so the branches
            bought nothing. Same for the shadow below. /simplify at 04-05: six
            per-variant branches became one. */
        int     dividerWidth;

        /** `inset 0 1px 2px rgba(0,0,0,A)` — 0 for the out-toggle, which
            declares no inset shadow. A fully transparent fill is a no-op. */
        float   insetAlpha;
    };

    static constexpr std::array<VariantSpec, 2> variantSpecs {{
        //  variant                 padX               padY               radius                      well   divider                   inset
        { Variant::quickSwitch, segmented::kPadX,    segmented::kPadY,    segmented::kRadiusExtra,    true,  segmented::kDividerWidth, segmented::kInsetAlpha },
        { Variant::outToggle,   segmented::kOutPadX, segmented::kOutPadY, segmented::kOutRadiusExtra, false, 0,                        0.0f                   },
    }};

    static constexpr const VariantSpec& specFor (Variant v) noexcept
    {
        return variantSpecs[static_cast<size_t> (v)];
    }

    /** The size a set of labels needs, without building one.

        `preferredWidth()`/`preferredHeight()` return these, so a layout that
        reserves a box for a Segmented and the Segmented itself cannot
        disagree — Button::heightOf's pattern, and for the reason that one
        records: ChassisLayout had a verbatim second copy of both, and the only
        test touching it asserted the segments fall inside the control's own
        bounds, which ARE the reserved box. It could not fail. */
    static int widthOf (const juce::StringArray&, type::Style, Variant) noexcept;
    static int heightOf (type::Style, Variant) noexcept;

    Segmented (ForroBoxLookAndFeel&, juce::StringArray labels, type::Style, Variant);

    void paint (juce::Graphics&) override;

    /** Which segment is lit. The owner writes this; a click does not, because
        what a selection MEANS differs per instance. */
    void setSelectedIndex (int);
    int  getSelectedIndex() const noexcept { return selectedIndex; }

    int getNumSegments() const noexcept { return labels.size(); }

    /** One segment's text.

        Exposed because nothing could see it: a negative control that relabelled
        OUTPUT's segments while the PARAMETER kept its own list went undetected —
        the tests compared the segment COUNT and the lit index, and both survive
        a renamed label. What a user reads and what a saved project holds are
        then two lists that agree by inspection. */
    const juce::String& getLabel (int index) const noexcept { return labels.getReference (index); }

    /** The segment's rectangle, in this component's coordinates. Public so a
        test can drive a real MouseEvent at one rather than guessing. */
    juce::Rectangle<int> segmentBounds (int index) const;

    /** Clicked. Unset means the control changes nothing, which is what an
        honest stub looks like. */
    std::function<void (int index)> onSegmentClicked;

    int preferredWidth() const;
    int preferredHeight() const;

    /** Dim it and withdraw the pointing hand.

        OUTPUT is drawn and bound to `ids::output_mode` for DISPLAY in 04-05 and
        made live in 04-06, which implements the routing behind it. Until then a
        user clicking MULTI-OUT would get a control that looked like it worked,
        so it takes the same treatment `Button` and `BpmField` already carry: the
        pointing hand is the affordance that says "click me", and withdrawing it
        is what tells someone who clicks and gets nothing that the control is not
        broken.

        It still MOVES when the parameter moves, which is honest — the parameter
        is real, automatable and persisted today. That is the OWNER's job, not
        this flag's: read-only gates the mouse handlers and nothing else, and
        `FooterBar` binds the display half through a `ChoiceAttachment`. The
        sentence above was here before that attachment was, describing behaviour
        the code did not have — /code-review on 04-05. */
    void setReadOnly (bool);
    bool isReadOnly() const noexcept { return readOnly; }

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    int indexAt (juce::Point<int>) const;

    ForroBoxLookAndFeel&   lnf;
    const juce::StringArray labels;
    const Variant          variant { Variant::quickSwitch };
    bool                   readOnly { false };
    const type::Style       style;

    /** Each segment's x offset and width, computed ONCE.

        `labels` and `style` are both const, so the widths are immutable —
        `segmentBounds` used to re-measure every preceding label on each call,
        which `indexAt` then did per segment on every mouseMove, and `paint`
        again per segment. A font walk per glyph per label per frame. Found by
        /simplify. */
    std::vector<juce::Range<int>> spans;

    int selectedIndex { 0 };
    int hoveredIndex { -1 };
    int pressedIndex { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Segmented)
};

/** Every row sits at its own enum's index, so `specFor` cannot return another
    variant's box. The same compile-time guard `textStyles` and `Button`'s table
    carry, and for the reason 03-03's TIMBRE ordering bug recorded. */
constexpr bool segmentedSpecsIndexedByEnum() noexcept
{
    for (size_t i = 0; i < Segmented::variantSpecs.size(); ++i)
        if (static_cast<size_t> (Segmented::variantSpecs[i].variant) != i)
            return false;

    return true;
}

static_assert (segmentedSpecsIndexedByEnum(),
               "Segmented::variantSpecs is no longer indexed by its own Variant enum, so specFor "
               "would hand a control the other one's padding, radius and well");

} // namespace forrobox
