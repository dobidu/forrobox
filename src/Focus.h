/* ============================================================================
   FORRÓ BOX — keyboard focus, taken only where JUCE allows it
============================================================================ */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace forrobox
{

/** Grabs keyboard focus only when `component` is on screen — JUCE's own
    precondition (`grabKeyboardFocus` asserts `isShowing() || isOnDesktop()`,
    and does nothing otherwise). Headless, in the test suite, the grab asserted
    and achieved nothing: 6 assertions a Release run could not see (11-05). In a
    host these components are always showing, so nothing a user does changes.
    One helper rather than the same guard pasted at three call sites. */
inline void grabFocusIfVisible (juce::Component& component)
{
    if (component.isShowing() || component.isOnDesktop())
        component.grabKeyboardFocus();
}

} // namespace forrobox
