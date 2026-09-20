/* ============================================================================
   FORRÓ BOX — the shape `ProfileButton` and `TimbreRow` both are

   A tile in a list: it knows its index, it lights when chosen, and clicking it
   asks its owner to choose it. Everything else — what it paints, how tall it
   is, what "chosen" means — belongs to the class that derives from it.

   Extracted at 06-05 because the two were not similar, they were the SAME:
   identical constructors, identical setters, and `mouseUp` bodies that were
   byte-identical apart from one clause of comment.

   THE RIGHT-CLICK GUARD IS WHY THIS IS A BASE RATHER THAN A CONVENTION. It is
   missing-by-default, and it went missing twice: `/code-review` found a
   right-click reloading the whole plugin state through `ProfileButton` in one
   plan, and writing the character parameter through `TimbreRow` in another,
   while `Button::mouseDown` had stated the rule all along. A law that three
   classes restate is a law two of them will ship without.
============================================================================ */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "LookAndFeel.h"
#include "Surface.h"

#include <functional>

namespace forrobox
{

class SelectableTile : public juce::Component
{
public:
    SelectableTile (ForroBoxLookAndFeel& lookAndFeelToUse, int indexToUse)
        : lnf (lookAndFeelToUse), index (indexToUse)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    ~SelectableTile() override = default;

    /** Which entry of its owner's table this tile names. */
    int getIndex() const noexcept { return index; }

    /** Lit or not. The OWNER decides — a tile never chooses itself, which is
        what lets host automation move `TimbreRow` with no editor gesture and
        lets `ProfileButton` clear while `activeProfile` still names it.

        `ProfileButton` spells this `setActive`/`isActive`, because its
        predicate is stricter than "selected": `PLANNING.md:601` makes an edited
        state stop being the profile it came from. Two names for two meanings
        over one mechanism. */
    void setSelected (bool shouldBeSelected)
    {
        if (selected == shouldBeSelected)
            return;

        selected = shouldBeSelected;
        repaint();
    }

    bool isSelected() const noexcept { return selected; }

    std::function<void()> onClick;

    /** RIGHT-CLICK BELONGS TO THE HOST — `Button::mouseDown` states the rule and
        neither of the two controls that now derive from this followed it.
        Without the guard a right-click reloaded the whole state through one and
        wrote the character parameter through the other, and both swallowed the
        automation menu the host was opening. /code-review, twice.

        FINAL, so a derived tile cannot override it and re-open the hole. Public
        rather than private, which is where this first landed: `Component`
        declares it public and the suite calls it directly to deliver a click,
        so hiding it broke three call sites for no gain — `final` is the part
        that does the guarding.

        The RULE is `isPlainClickInside` in Surface.h; this file used to spell
        it out and so did HitZone, byte for byte. */
    void mouseUp (const juce::MouseEvent& event) final
    {
        if (isPlainClickInside (event, getLocalBounds()) && onClick != nullptr)
            onClick();
    }

protected:
    ForroBoxLookAndFeel& lnf;
    const int index;

private:
    bool selected { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SelectableTile)
};

} // namespace forrobox
