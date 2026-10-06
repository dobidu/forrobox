/* ============================================================================
   FORRÓ BOX — the MEUS tab's list of the user's grooves (18-02)

   One `ProfileButton` per groove, in the bank's order, in the box the four
   regional entries occupy under the REGIONAIS tab — the user's decision at the
   18-02 checkpoint: their grooves are buttons like CARUARU and CAMPINA GRANDE,
   marked as theirs, and the column below does not move. The library is
   unlimited, so the list SCROLLS when it outgrows the box.

   Holds no state of its own beyond what it was last told: the side panel
   gives it the bank and the active id on its poll, as it tells the regional
   buttons which one is lit.
============================================================================ */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "LookAndFeel.h"
#include "ProfileButton.h"
#include "UserGrooves.h"

#include <functional>
#include <memory>
#include <vector>

namespace forrobox
{

class UserGrooveList final : public juce::Component
{
public:
    explicit UserGrooveList (ForroBoxLookAndFeel&);
    ~UserGrooveList() override;

    /** The bank to show, and the library scan it came from. Rebuilds only
        when the scan is a new one: the side panel calls this on every poll,
        and comparing a generation costs nothing where re-reading every groove
        did. */
    void setBank (const std::vector<UserGroove>&, int generation);

    /** The groove the state IS (pristine), or empty for none. That button is
        lit and tall, and scrolled into view when it changes. */
    void setActiveId (const juce::String&);

    /** A click on a groove's button, by id. */
    std::function<void (const juce::String& id)> onGrooveClicked;

    int getNumButtons() const noexcept { return static_cast<int> (buttons.size()); }
    ProfileButton& getButton (int index) const { return *buttons[static_cast<size_t> (index)]; }

    juce::Viewport& getViewport() noexcept { return viewport; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void layoutContent();

    /** Brings the lit button into view if it is not. After a new active id,
        AND after a rebuild or a resize, which can move the lit tile without
        changing its id — a rename re-sorts the bank. /code-review. */
    void scrollActiveIntoView();

    ForroBoxLookAndFeel& lnf;
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<ProfileButton>> buttons;

    /** The library scan the buttons were built from; -1 before the first. */
    int builtFrom { -1 };
    juce::String activeId;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UserGrooveList)
};

} // namespace forrobox
