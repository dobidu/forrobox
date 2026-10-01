/* ============================================================================
   FORRÓ BOX — the settings menu, as its own unit (12-01)

   The menu's ids, the model it builds and the dispatch of a chosen item, out
   of Chassis — so the tests NAME items instead of typing their numbers, and a
   second instance (Phase 13) can apply the store without a chassis to hand.
============================================================================ */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Settings.h"

namespace forrobox
{

class ForroBoxLookAndFeel;

struct SettingsMenu
{
    /** What applying a chosen item did. `unknown` is an id the menu never
        offered, and it writes NOTHING — every band bounds-checks itself. */
    enum class Result { dismissed, changed, about, unknown };

    /** The menu's model, every item ticked with its current value. Separate
        from showing it: a `PopupMenu` cannot be inspected once it is on screen,
        and a test that cannot read the menu can only assert that clicking did
        something. */
    static juce::PopupMenu build (const Settings&);

    /** Applies one result id to the store. Applying it to the UI is the
        caller's: `settings::applyTo` plus a repaint, on `changed`. */
    static Result apply (int resultId, Settings&);

    /** The id of each item, by its INDEX in that submenu — what a test names
        instead of a number. */
    static int themeItem (int index) noexcept;
    static int radiusItem (int index) noexcept;
    static int accentItem (int index) noexcept;
    static int stepsItem (int index) noexcept;
    static int fontItem (int index) noexcept;
    static int aboutItem() noexcept;

    /** The accent percentages the menu offers, by index. */
    static int accentStepPercent (int index) noexcept;
    static int numAccentSteps() noexcept;
};

namespace settings
{
/** Pushes every stored preference into the LookAndFeel and the type system.
    Repaints NOTHING: only the caller knows what is on screen. The editor calls
    it before the first paint; the chassis, after a menu change, then repaints. */
void applyTo (ForroBoxLookAndFeel&, const Settings&);
} // namespace settings

} // namespace forrobox
