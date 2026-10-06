#include "UserGrooveList.h"

#include "SidePanel.h"
#include "Theme.h"
#include "Typography.h"

namespace forrobox
{

UserGrooveList::UserGrooveList (ForroBoxLookAndFeel& lookAndFeelToUse) : lnf (lookAndFeelToUse)
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (side::kUserListScrollbar);
    // A NEUTRAL thumb, readable on both themes: a token read here would go
    // stale on a theme switch, which repaints but rebuilds nothing.
    viewport.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, juce::Colours::grey.withAlpha (0.5f));
    viewport.getVerticalScrollBar().setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (viewport);
}

UserGrooveList::~UserGrooveList()
{
    // The viewport does not own `content`; detach it before either dies.
    viewport.setViewedComponent (nullptr, false);
}

void UserGrooveList::setBank (const std::vector<UserGroove>& bank, int generation)
{
    if (generation == builtFrom)
        return;

    builtFrom = generation;
    buttons.clear();

    for (size_t i = 0; i < bank.size(); ++i)
    {
        auto button = std::make_unique<ProfileButton> (lnf, static_cast<int> (i), bank[i]);

        button->onClick = [this, id = bank[i].id]
        {
            if (onGrooveClicked != nullptr)
                onGrooveClicked (id);
        };

        button->setActive (bank[i].id == activeId);
        content.addAndMakeVisible (*button);
        buttons.push_back (std::move (button));
    }

    layoutContent();
    scrollActiveIntoView();
    repaint();
}

void UserGrooveList::setActiveId (const juce::String& id)
{
    if (id == activeId)
        return;

    activeId = id;

    for (auto& button : buttons)
        button->setActive (button->getUserGrooveId() == activeId);

    layoutContent();
    scrollActiveIntoView();
}

void UserGrooveList::scrollActiveIntoView()
{
    // THE LIT ONE IN VIEW: a groove loaded from the cycler may sit below the
    // fold, and a list that hid the active entry would hide the answer.
    for (auto& button : buttons)
        if (button->isActive() && ! viewport.getViewArea().contains (button->getBounds()))
            viewport.setViewPosition (0, button->getY());
}

void UserGrooveList::layoutContent()
{
    // The buttons' natural heights, stacked with the regional gap. Narrower by
    // the scrollbar only when there is something to scroll.
    auto total = 0;

    for (const auto& button : buttons)
        total += ProfileButton::heightOf (button->isActive()) + side::kProfileGap;

    total = juce::jmax (0, total - side::kProfileGap);

    const auto scrolls = total > viewport.getHeight();
    const auto width = getWidth() - (scrolls ? side::kUserListScrollbar + side::kProfileGap : 0);

    auto y = 0;

    for (auto& button : buttons)
    {
        const auto height = ProfileButton::heightOf (button->isActive());
        button->setBounds (0, y, width, height);
        y += height + side::kProfileGap;
    }

    content.setSize (width, total);
}

void UserGrooveList::resized()
{
    viewport.setBounds (getLocalBounds());
    layoutContent();
    scrollActiveIntoView();
}

void UserGrooveList::paint (juce::Graphics& g)
{
    if (! buttons.empty())
        return;

    // EMPTY: what this tab is for and how to fill it, in the description's
    // face and the section labels' faint ink.
    g.setColour (lnf.token (theme::Token::fgFaint));

    const auto lineHeight = ProfileButton::descriptionLineHeight();
    auto line = getLocalBounds().withHeight (lineHeight).translated (0, side::kProfilePadY);

    for (const auto* text : { "Nenhum groove salvo ainda.", "Salve pelo menu de ajustes." })
    {
        type::drawTracked (g, type::Style::profileDescription, juce::String::fromUTF8 (text),
                           line.toFloat(), juce::Justification::centredLeft);
        line = line.translated (0, lineHeight);
    }
}

} // namespace forrobox
