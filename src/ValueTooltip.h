/* ============================================================================
   FORRÓ BOX — the shared value tooltip

   `PLANNING.md:374`: mono 11 px, `--screen` ground, `--screen-fg` text, 1 px
   `--line-strong` border, radius `--r`, padding 2 x 7, positioned above-centre,
   fades in 120 ms.

   ONE instance for the whole editor, as `controls.js:11-27` has it — a module
   singleton shown and moved, not one per control. Twenty strip knobs plus the
   header's two plus the side panel's would otherwise be twenty-three idle
   Components each owning a fade timer.
============================================================================ */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "LookAndFeel.h"
#include "Typography.h"

namespace forrobox
{

class ValueTooltip final : public juce::Component,
                           private juce::Timer
{
public:
    explicit ValueTooltip (ForroBoxLookAndFeel&);

    /** `padding: 2px 7px` — PLANNING.md:375. */
    static constexpr int kPaddingX = 7;
    static constexpr int kPaddingY = 2;

    /** `fades in 120ms` — PLANNING.md:375. */
    static constexpr int kFadeMs = 120;

    /** Show `text` centred above `anchor`, which is in the tooltip's PARENT
        coordinates. Clamped to the parent so a knob at the top edge still
        shows its value. */
    void showFor (const juce::String& text, juce::Rectangle<int> anchorInParent);

    void hide();

    /** The current text, whether or not it is visible. Lets a test assert what
        a gesture reported without reading pixels out of a fading component. */
    juce::String getText() const { return content; }

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    ForroBoxLookAndFeel& lnf;
    juce::String         content;
    float                opacity { 0.0f };
    bool                 fadingIn { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ValueTooltip)
};

} // namespace forrobox
