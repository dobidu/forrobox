/* ============================================================================
   FORRÓ BOX — one regional profile button

   `.profile` — css:393-407: a bordered box on `--panel` carrying the groove's
   name. The ACTIVE one inverts to `--active`, puts its name in `--bg`, floats a
   `--c-zabumba` dot to the right, and reveals the three-line description that
   css:400 hides on the others.

   Its own control, as `TimbreRow` is for `.timbre` and `StepPad` for `.pad`.
   06-02 built these as four rectangles in a layout struct plus a paint loop,
   because their state comes from the pattern state rather than from a parameter
   — which is not a property of a control. Wiring 06-03's click onto that would
   have been a fifth hand-rolled container hit-test.
============================================================================ */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "LookAndFeel.h"
#include "ParameterIDs.h"

#include <functional>

namespace forrobox
{

class ProfileButton final : public juce::Component
{
public:
    /** `index` is the entry of `ids::profileInfos` this button names — the same
        table `ChassisLayout::indexOfProfile` resolves an id against, so the
        button and the stored state cannot disagree about which groove is which. */
    ProfileButton (ForroBoxLookAndFeel&, int index);

    /** Active means "the stored state IS this profile" — which `PLANNING.md:601`
        makes stricter than it sounds: an edited state stops being the profile it
        came from, so the highlight clears even though `activeProfile` still
        names it. The owner decides; this only draws it. */
    void setActive (bool);
    bool isActive() const noexcept { return active; }

    int getIndex() const noexcept { return index; }

    /** The height this button needs. Taller when active, because only then does
        css:403 reveal the description.

        ASKED of the control, the way `Button::heightOf`, `Knob::preferredHeight`
        and `TimbreRow::heightOf` are — the box model belongs to the class that
        paints it. */
    static int heightOf (bool showsDescription) noexcept;

    /** One line of the description — css:400's `line-height: 1.4`. Shared by
        `heightOf` and the painter so the reserved box and the drawn lines cannot
        round differently. */
    static int descriptionLineHeight() noexcept;

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

    std::function<void()> onClick;

private:
    ForroBoxLookAndFeel& lnf;
    const int index;
    bool active { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProfileButton)
};

} // namespace forrobox
