/* ============================================================================
   FORRÓ BOX — LookAndFeel

   Deliberately thin. It carries the theme mode, the corner radius and the
   accent intensity, and it maps only the JUCE colour IDs that components
   actually consume.

   It does NOT carry the font families: components call type::fontFor directly,
   and getLabelFont below is only for the JUCE widgets that ask a LookAndFeel
   for a font. An accent accessor lived here briefly and was deleted — it took
   a theme::Accent and ignored it, returning zabumba for every input, because
   per-instrument accent is per-COMPONENT state. Call theme::accent, which
   needs no mode and so needs no wrapper.

   It is NOT where the drawing lives. The Knob and the step pad are custom
   Components (04-02, 04-03), because both need per-instance state a stateless
   LookAndFeel callback cannot carry — a pad's velocity and flash decay, a
   knob's bipolar flag and its instrument accent. Routing them through
   drawRotarySlider would mean smuggling that state through member variables set
   before each call, which is the mutable-member channel `/simplify` removed
   twice in Phase 2.
============================================================================ */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Theme.h"
#include "Typography.h"

namespace forrobox
{

class ForroBoxLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    explicit ForroBoxLookAndFeel (theme::Mode initialMode = theme::Mode::dark);

    /** The palette in force. Changing it repaints nothing by itself — the
        caller owns that, because only the caller knows what is on screen. */
    void setMode (theme::Mode) noexcept;
    theme::Mode getMode() const noexcept { return mode; }

    /** A token in the current mode. The one accessor components should use:
        reading `theme::colour (token, lnf.getMode())` at a call site works but
        duplicates the mode lookup everywhere. */
    juce::Colour token (theme::Token) const noexcept;

    theme::Shadows shadows() const noexcept { return theme::shadowsFor (mode); }

    /** `--r`, and the `calc(--r + N)` composites nested groups use. */
    float cornerRadius (int extra = 0) const noexcept
    {
        return juce::jmax (0.0f, radius + static_cast<float> (extra));
    }

    /** `--accent-i`. Scales accent glows and the saturation of value fills. */
    float accentIntensity() const noexcept { return intensity; }

    /** Both tweakables are user-facing in Phase 8's settings menu, so they are
        settable now rather than being constants that have to be dug out later. */
    void setCornerRadius (float newRadius) noexcept { radius = juce::jmax (0.0f, newRadius); }
    void setAccentIntensity (float newIntensity) noexcept { intensity = juce::jlimit (0.0f, 1.0f, newIntensity); }

    juce::Font getLabelFont (juce::Label&) override;

private:
    /** Re-applies the palette to the JUCE colour IDs the UI actually uses.
        Called from the constructor and from setMode, so the two cannot drift. */
    void applyColourScheme();

    theme::Mode  mode { theme::Mode::dark };
    float        radius { theme::kCornerRadius };
    float        intensity { theme::kAccentIntensity };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ForroBoxLookAndFeel)
};

} // namespace forrobox
