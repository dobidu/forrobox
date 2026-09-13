/* ============================================================================
   FORRÓ BOX — ValueScreen

   The recessed readout: a `--screen` ground, a `--line` border, centred mono
   text, a minimum width, and the glow css:597 puts on exactly three elements —
   `.bpm`, `.pscreen` and `.gk-read`. All three are this plan's, so one
   component serves all three and `theme::kScreenGlowRadius` /
   `kScreenGlowOpacity` finally have a caller. 04-01 declared them and nothing
   has read them since; 02-04's rule is that a guarantee with no caller is not a
   guarantee.

   `.pat-screen` (04-03) is a screen WITHOUT that glow and is painted by Chassis
   rather than being a component. It is deliberately NOT retrofitted here: it is
   not in css:597's list, and making it a fourth caller would apply a glow the
   stylesheet does not give it.

   Holds no value. Whatever owns it calls setText, exactly as the Button holds
   no lit state and the Knob holds no range.
============================================================================ */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "LookAndFeel.h"
#include "Theme.h"
#include "Typography.h"

namespace forrobox
{

class ValueScreen final : public juce::Component
{
public:
    /** @param style     the type row for the main text
        @param minWidth  css `min-width`; the screen never draws narrower
        @param padX      horizontal padding, from the element's own rule
        @param padY      vertical padding */
    ValueScreen (ForroBoxLookAndFeel&, type::Style style, int minWidth, int padX, int padY);

    void paint (juce::Graphics&) override;

    void setText (juce::String);
    const juce::String& getText() const noexcept { return text; }

    /** A smaller trailing run, for the BPM field's ` BPM` suffix (css:178). Its
        own type row, because it is 9 px at 55% where the value is 22 px. */
    void setSuffix (juce::String, type::Style);

    /** The width this screen needs: its text at its tracking, plus padding and
        borders, never below `min-width`. */
    int preferredWidth() const;
    int preferredHeight() const;

    /** The height a screen needs for a type row and its padding, without
        building one — so a layout can reserve the box. Button::heightOf's
        shape; the header restated this expression for three screens it builds
        three lines later. Found by /simplify. */
    static int heightOf (type::Style, int padY);

    static constexpr int kBorderWidth = 1;   ///< css:174/216/226, every screen

    /** Where a glyph run's BASELINE sits below the box's centre, as a fraction
        of the type row's height. A line of text is centred on its x-height, so
        the baseline is roughly a third of the row below the middle — the same
        placement `drawTracked` produces, matched here because this component
        positions outlines itself. */
    static constexpr float kBaselineFromCentre = 0.35f;

private:
    ForroBoxLookAndFeel& lnf;
    const type::Style    style;
    const int            minWidth, padX, padY;

    juce::String text, suffix;
    type::Style  suffixStyle { type::Style::bpmSuffix };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ValueScreen)
};

} // namespace forrobox
