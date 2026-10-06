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
    enum class Result { dismissed, changed, about, unknown,
                        // The GROOVES band (18-02): no Settings write — the
                        // chassis acts on the user groove library.
                        saveGrooveAs, saveGrooveOver, renameGroove, deleteGroove };

    /** The menu's model, every item ticked with its current value. Separate
        from showing it: a `PopupMenu` cannot be inspected once it is on screen,
        and a test that cannot read the menu can only assert that clicking did
        something. */
    static juce::PopupMenu build (const Settings&);

    /** With the GROOVES band. `activeUserGroove` is the name of the user groove
        the state plays, or empty: Save over / Rename / Delete name it and are
        enabled only then. Save as is always enabled. */
    static juce::PopupMenu build (const Settings&, const juce::String& activeUserGroove);

    /** Applies one result id to the store. The UI follows through the store's
        notification: every open `Chassis` runs `applySettings`. */
    static Result apply (int resultId, Settings&);

    /** The id of each item, by its INDEX in that submenu — what a test names
        instead of a number. */
    static int themeItem (int index) noexcept;
    static int radiusItem (int index) noexcept;
    static int accentItem (int index) noexcept;
    static int stepsItem (int index) noexcept;
    static int fontItem (int index) noexcept;
    static int aboutItem() noexcept;
    static int saveGrooveAsItem() noexcept;
    static int saveGrooveOverItem() noexcept;
    static int renameGrooveItem() noexcept;
    static int deleteGrooveItem() noexcept;

    /** The accent percentages the menu offers, by index. */
    static int accentStepPercent (int index) noexcept;
    static int numAccentSteps() noexcept;
};

namespace settings
{
/** Pushes the per-instance preferences — theme, corner radius, accent — into
    one LookAndFeel. Repaints NOTHING: only the caller knows what is on screen,
    and its only caller is `Chassis::applySettings`. The display font is
    process-global and the store sets it (`Settings::notify`). */
void applyTo (ForroBoxLookAndFeel&, const Settings::Snapshot&);
} // namespace settings

} // namespace forrobox
