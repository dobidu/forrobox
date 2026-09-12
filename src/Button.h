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
        load,       ///< `.load-btn` — css:295, the sample slot's 9 px stub
        mini,       ///< `.mini-btn` — css:180, the BPM cluster's div-2 / x2 pair
        transport,  ///< `.tp-btn` — css:189, a fixed 34x34 ICON on --panel
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
        bool        groundIsPanel;     ///< arrow and transport sit on --panel
        bool        hoverLiftsBorder;  ///< hover moves the border to --fg-dim, not only the label

        /** Hover changes the GROUND to --sunken rather than the label's colour.

            A third hover law, and data for the reason `hoverLiftsBorder` is:
            /simplify removed a `variant == Variant::base` test from `paint`
            once already, and css:141, 300, 342, 235 and 195 are five rules that
            agree on nothing except that something changes. */
        bool        hoverDarkensGround;
    };

    static constexpr int   kBasePadX   = 9;      ///< css:138 .btn padding 5px 9px
    static constexpr int   kBasePadY   = 5;
    static constexpr float kBasePress  = 0.96f;  ///< css:142

    static constexpr int   kLoadPadX   = 7;      ///< css:298 .load-btn padding 4px 7px
    static constexpr int   kLoadPadY   = 4;

    /** No press transform at all.

        `.ms-btn` and `.load-btn` have no `:active` rule — only `.btn` (css:142)
        and `.arrow-btn` (css:236) do. Task 1 gave mute/solo the base button's
        0.96 because it shared everything else with it, which is an invented
        behaviour rather than a transcribed one, and exactly the kind of silent
        deviation the fader's missing wheel is guarded against. A scale of 1 is
        the identity, so this is the absence written down. */
    static constexpr float kNoPress = 1.0f;

    static constexpr int   kMiniPadX  = 6;      ///< css:183 .mini-btn padding 3px 6px
    static constexpr int   kMiniPadY  = 3;
    static constexpr float kMiniPress = 0.94f;  ///< css:186

    static constexpr int   kTransportSize  = 34;    ///< css:190 .tp-btn 34x34
    static constexpr float kTransportPress = 0.94f; ///< css:196
    static constexpr int   kTransportIcon  = 14;    ///< css:198 .tp-btn svg
    static constexpr float kTransportIconViewBox = 24.0f;  ///< app.js:71 the icons' own box

    /// `box-shadow: 0 0 12px <ganza at 55%>` on a playing transport button (css:637).
    static constexpr int   kTransportGlowRadius  = 12;
    static constexpr float kTransportGlowOpacity = 0.55f;

    static constexpr int   kMuteSoloPadY  = 5;   ///< css:340 .ms-btn padding 5px 0

    // No gap constant here. `.ms-row`'s gap is the ROW's property, and
    // ChassisLayout::kMuteSoloGap already carries it — this header held a second
    // copy that nothing read, so verify-geometry.py cross-checked one CSS
    // declaration twice and a change to it would have failed the build naming a
    // constant with no consumers. Found by /simplify.

    static constexpr int   kArrowWidth  = 22;    ///< css:231 .arrow-btn
    static constexpr int   kArrowHeight = 26;
    static constexpr float kArrowPress  = 0.92f; ///< css:235

    static constexpr int   kBorderWidth = 1;     ///< every variant, css:136/339/232

    static constexpr std::array<VariantSpec, 6> variantSpecs {{
        //  variant              padX          padY            fixedW           fixedH           press            label style                   panel  hoverBorder  hoverGround
        { Variant::base,      kBasePadX,    kBasePadY,      0,               0,               kBasePress,      type::Style::buttonLabel,     false, true,        false },
        { Variant::muteSolo,  0,            kMuteSoloPadY,  0,               0,               kNoPress,        type::Style::muteSoloLabel,   false, false,       false },
        { Variant::arrow,     0,            0,              kArrowWidth,     kArrowHeight,    kArrowPress,     type::Style::buttonLabel,     true,  false,       false },
        { Variant::load,      kLoadPadX,    kLoadPadY,      0,               0,               kNoPress,        type::Style::loadLabel,       false, true,        false },
        { Variant::mini,      kMiniPadX,    kMiniPadY,      0,               0,               kMiniPress,      type::Style::miniButtonLabel, false, true,        false },
        { Variant::transport, 0,            0,              kTransportSize,  kTransportSize,  kTransportPress, type::Style::buttonLabel,     true,  false,       true  },
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

    /** The height a variant needs, without building one.

        `preferredHeight()` is `heightOf (variant)` — ONE expression, so a
        layout that reserves a row for a button and the button itself cannot
        disagree. ChassisLayout::kMuteSoloHeight used to restate this sum by
        hand, and because M and S are sized to the ROW rather than to their own
        preferred height, nothing could have caught the two drifting apart. */
    static constexpr int heightOf (Variant v) noexcept
    {
        const auto& spec = specFor (v);

        if (spec.fixedHeight > 0)
            return spec.fixedHeight;

        return static_cast<int> (type::styleFor (spec.labelStyle).heightPx + 0.5f)
             + spec.padY * 2 + kBorderWidth * 2;
    }

    /** A transport button draws an ICON, not a label.

        A path in its own viewBox, scaled once to kTransportIcon — the knob's
        rule, for the same reason: the two icons are authored in a 24x24 box
        (app.js:71-72) and rendered at 14, so treating the coordinates as pixels
        would draw one correct icon at 24 px and a wrong one everywhere else. */
    struct Icon
    {
        juce::Path path;
        float      viewBox { kTransportIconViewBox };
    };

    /** How far a variant paints OUTSIDE its own box.

        The lit transport button's `0 0 12px` glow (css:637) is an outer
        box-shadow, and a Component's paint is clipped to its bounds — drawn at
        the exact 34x34 it would contribute nothing at all. Third instance of
        this shape: `StepPad::boundsForPadRect` reserves the pad's glow and
        `Fader::boundsForBox` reserves the thumb's overhang, both for the same
        reason and both found the same way.

        Zero for every other variant, so their bounds ARE their box. */
    static constexpr int glowMargin (Variant v) noexcept
    {
        return v == Variant::transport ? kTransportGlowRadius : 0;
    }

    /** The component bounds a variant needs for a box of that size. Layouts
        compute the css box and ask this, exactly as the strip does for the pad
        and the fader. */
    static juce::Rectangle<int> boundsForBox (Variant v, juce::Rectangle<int> box) noexcept
    {
        return box.expanded (glowMargin (v));
    }

    /** The button itself, inside its bounds — `boundsForBox`'s inverse. */
    juce::Rectangle<int> contentBox() const noexcept
    {
        return getLocalBounds().reduced (glowMargin (variant));
    }

    Button (ForroBoxLookAndFeel&, Variant, juce::String label, OnStyle = OnStyle::active);

    /** Draws `icon` centred instead of the label. The two are exclusive: a
        variant either has a label or a glyph, and nothing here needs both. */
    void setIcon (Icon);

    Variant getVariant() const noexcept { return variant; }

    void paint (juce::Graphics&) override;

    /** The width this button needs for its label, or its fixed width. */
    int preferredWidth() const;

    /** The height this button needs — fixed, or the type row plus padding and
        borders. */
    int preferredHeight() const;

    /** The label as the button will draw it. Exposed so a test can see what a
        non-ASCII literal actually became: U+2039 handed to juce::String through
        its `const char*` constructor arrives as three Latin-1 characters, and
        every rendering check in this suite measured ink that was happily
        present and wrong. */
    const juce::String& getText() const noexcept { return text; }

    /** The lit state. A Button holds no value of its own: whatever owns it
        drives this from a parameter, exactly as KnobAttachment drives the
        knob's proportion. */
    void setOn (bool);
    bool isOn() const noexcept { return on; }

    /** Clicked. Nothing is assumed about what it does — a stub leaves this
        unset and the button then changes nothing, visibly and honestly. */
    std::function<void()> onClick;

    /** The glow margin is transparent, so clicks in it must not be swallowed —
        the same reason StepPad overrides this. */
    bool hitTest (int x, int y) override { return contentBox().contains (x, y); }

    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    ForroBoxLookAndFeel& lnf;
    const Variant        variant;
    const juce::String   text;
    const OnStyle        onStyle;

    Icon icon;
    bool hasIcon { false };

    bool on { false };
    bool hovered { false };
    bool pressed { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Button)
};

static_assert (Button::variantsIndexedByEnum(),
               "Button::variantSpecs is no longer indexed by its own Variant enum, so specFor "
               "would return another variant's box model");

} // namespace forrobox
