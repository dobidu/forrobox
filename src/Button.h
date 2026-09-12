/* ============================================================================
   FORRÓ BOX — the button family

   Three variants of one component, not three components: the base `.btn`, the
   channel strip's mute/solo pair, and the pattern cycler's arrows. They share
   a border, a radius, a hover lift and a press scale, and differ only in box
   model, type row and on-state colours — so the differences are DATA in one
   table and there is exactly one `paint`.

   The one-table shape is `ChassisLayout::knobSlots`'s, and for the same
   reason: 04-02 found the knob order hand-copied into two test arrays that
   agreed with each other, and 03-03 found `timbreChoices` ordered by nothing.
   A table both production and tests read cannot drift from itself.

   A custom Component rather than a juce::TextButton with a LookAndFeel
   override — the recorded Phase 4 decision. The lit state and the press scale
   are per-instance, and ForroBoxLookAndFeel stays thin.
============================================================================ */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "LookAndFeel.h"
#include "Theme.h"
#include "Typography.h"

namespace forrobox
{

class Button final : public juce::Component
{
public:
    enum class Variant
    {
        base,       ///< `.btn`  — css:131, hugs its label
        muteSolo,   ///< `.ms-btn` — css:336, flex:1, mono
        arrow,      ///< `.arrow-btn` — css:230, fixed 22x26 on --panel
    };

    /** Which colour the lit state paints. The base button and the two strip
        buttons each have their own, from three different CSS rules. */
    enum class OnStyle
    {
        active,     ///< `.btn.on`         — --active ground, --bg text
        mute,       ///< `.ms-btn.mute.on` — --danger ground, #fff text
        solo,       ///< `.ms-btn.solo.on` — --c-pandeiro ground, #1a1500 text
    };

    /** One variant's box model and type row.

        Every number is quoted from the stylesheet at its field, and every one
        is cross-checked by `scripts/verify-geometry.py` — including the press
        scales, which differ between variants for no reason but that the
        stylesheet says so. */
    struct VariantSpec
    {
        Variant     variant;
        int         padX;          ///< horizontal padding, 0 when the width is fixed
        int         padY;
        int         fixedWidth;    ///< 0 = hug the label
        int         fixedHeight;   ///< 0 = derive from the type row and padding
        float       pressScale;
        type::Style labelStyle;
        bool        groundIsPanel; ///< arrow sits on --panel; the others are transparent
    };

    static constexpr int   kBasePadX   = 9;      ///< css:138 .btn padding 5px 9px
    static constexpr int   kBasePadY   = 5;
    static constexpr float kBasePress  = 0.96f;  ///< css:142

    static constexpr int   kMuteSoloPadY  = 5;   ///< css:340 .ms-btn padding 5px 0
    static constexpr int   kMuteSoloGapPx = 5;   ///< css:335 .ms-row gap

    static constexpr int   kArrowWidth  = 22;    ///< css:231 .arrow-btn
    static constexpr int   kArrowHeight = 26;
    static constexpr float kArrowPress  = 0.92f; ///< css:235

    static constexpr int   kBorderWidth = 1;     ///< every variant, css:136/339/232

    static constexpr std::array<VariantSpec, 3> variantSpecs {{
        { Variant::base,     kBasePadX, kBasePadY,    0,           0,            kBasePress, type::Style::buttonLabel,    false },
        { Variant::muteSolo, 0,         kMuteSoloPadY, 0,          0,            kBasePress, type::Style::muteSoloLabel,  false },
        { Variant::arrow,    0,         0,            kArrowWidth, kArrowHeight, kArrowPress, type::Style::buttonLabel,   true  },
    }};

    /** Indexed by the enum, asserted once for the whole table — the shape
        Typography.h adopted after 04-02, rather than a jassert per lookup. */
    static constexpr bool variantsIndexedByEnum() noexcept
    {
        for (size_t i = 0; i < variantSpecs.size(); ++i)
            if (static_cast<size_t> (variantSpecs[i].variant) != i)
                return false;

        return true;
    }

    static constexpr const VariantSpec& specFor (Variant v) noexcept
    {
        return variantSpecs[static_cast<size_t> (v)];
    }

    Button (ForroBoxLookAndFeel&, Variant, juce::String label, OnStyle = OnStyle::active);

    void paint (juce::Graphics&) override;

    /** The width this button needs for its label, or its fixed width. */
    int preferredWidth() const;

    /** The height this button needs — fixed, or the type row plus padding and
        borders. */
    int preferredHeight() const;

    /** The lit state. A Button holds no value of its own: whatever owns it
        drives this from a parameter, exactly as KnobAttachment drives the
        knob's proportion. */
    void setOn (bool);
    bool isOn() const noexcept { return on; }

    /** Clicked. Nothing is assumed about what it does — a stub leaves this
        unset and the button then changes nothing, visibly and honestly. */
    std::function<void()> onClick;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    ForroBoxLookAndFeel& lnf;
    const Variant        variant;
    const juce::String   text;
    const OnStyle        onStyle;

    bool on { false };
    bool hovered { false };
    bool pressed { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Button)
};

static_assert (Button::variantsIndexedByEnum(),
               "Button::variantSpecs is no longer indexed by its own Variant enum, so specFor "
               "would return another variant's box model");

} // namespace forrobox
