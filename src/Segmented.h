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
} // namespace segmented

class Segmented final : public juce::Component
{
public:
    Segmented (ForroBoxLookAndFeel&, juce::StringArray labels, type::Style);

    void paint (juce::Graphics&) override;

    /** Which segment is lit. The owner writes this; a click does not, because
        what a selection MEANS differs per instance. */
    void setSelectedIndex (int);
    int  getSelectedIndex() const noexcept { return selectedIndex; }

    int getNumSegments() const noexcept { return labels.size(); }

    /** The segment's rectangle, in this component's coordinates. Public so a
        test can drive a real MouseEvent at one rather than guessing. */
    juce::Rectangle<int> segmentBounds (int index) const;

    /** Clicked. Unset means the control changes nothing, which is what an
        honest stub looks like. */
    std::function<void (int index)> onSegmentClicked;

    int preferredWidth() const;
    int preferredHeight() const;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    int indexAt (juce::Point<int>) const;

    ForroBoxLookAndFeel&   lnf;
    const juce::StringArray labels;
    const type::Style       style;

    int selectedIndex { 0 };
    int hoveredIndex { -1 };
    int pressedIndex { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Segmented)
};

} // namespace forrobox
