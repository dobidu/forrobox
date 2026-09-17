/* ============================================================================
   FORRÓ BOX — one timbre row

   `.timbre` — css:416-429: a bordered box on `--panel` carrying the character's
   name, its sub-label, and a 7 px LED that lights `--c-ganza` when the row is
   the selected one.

   Its own control, as `StepPad` is for `.pad` and `BpmField` for `.bpm`. Every
   distinct rule in this stylesheet that a user can click became a component of
   its own, and this is one: `Button` models `.btn` and paints a single label,
   which is neither the ground nor the two stacked labels this rule asks for.
============================================================================ */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "LookAndFeel.h"
#include "MixBus.h"

#include <functional>

namespace forrobox
{

class TimbreRow final : public juce::Component
{
public:
    /** `index` is the parameter's own CHOICE index, which is also the row's
        index in `timbreSpecs` — the table `MixBus` reads its cutoff and drive
        from, so the row cannot name one character and sound like another. */
    TimbreRow (ForroBoxLookAndFeel&, int index);

    /** Lit by the PARAMETER, never by the click — 04-03's law, which is what
        makes host automation move it with no editor gesture. */
    void setSelected (bool);
    bool isSelected() const noexcept { return selected; }

    int getIndex() const noexcept { return index; }

    /** The height this row needs, without building one.

        `Button::heightOf`, `Knob::preferredHeight`, `Segmented::heightOf` and
        `ValueScreen::heightOf` are the same pattern, and the last one's docstring
        names it: the owner reserves the box by ASKING the control. The box model
        was computed in `SidePanelLayout` instead — its padding, its border, its
        two type rows and its LED, all from outside the class that paints them. */
    static int heightOf() noexcept;

    /** The LED's box, for the tests — `Knob::dialBounds`'s precedent, which is
        where a control's interior belongs rather than in the owner's layout. */
    juce::Rectangle<int> ledBounds() const noexcept;

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

    std::function<void()> onClick;

private:
    ForroBoxLookAndFeel& lnf;
    const int index;
    bool selected { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimbreRow)
};

} // namespace forrobox
