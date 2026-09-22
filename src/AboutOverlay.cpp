#include "AboutOverlay.h"

#include "Theme.h"
#include "Typography.h"

namespace forrobox
{

namespace
{
/** UTF-8 through a DIRECT argument, which is what `verify-charset.py` requires:
    `juce::String (const char*)` reads Latin-1 and prints mojibake, and the gate
    caught three of these during 08-01 alone. */
juce::String utf8 (const char* text)
{
    return juce::String (juce::CharPointer_UTF8 (text));
}

/** The rectangle a link's TEXT occupies inside its row.

    ONE definition, read by the hit test AND by the underline that tells the user
    where the hit test is. They were two spellings of the same `jmin`, which is
    the failure `Layout`'s own docstring says that struct exists to prevent —
    solved for the row boxes and reintroduced for the widths inside them. If they
    drift, the underline advertises an extent that does not respond. /simplify. */
juce::Rectangle<int> linkBox (juce::Rectangle<int> row, juce::StringRef text)
{
    const auto width = juce::roundToInt (
        type::trackedWidth (type::Style::profileName, text));

    return row.withWidth (juce::jmin (width, row.getWidth()));
}
} // namespace

AboutOverlay::AboutOverlay (ForroBoxLookAndFeel& lookAndFeelToUse)
    : lnf (lookAndFeelToUse)
{
    setWantsKeyboardFocus (true);

    // The scrim swallows clicks that would otherwise reach the chassis beneath.
    // KitOverlay states the same thing for the same reason: a panel you can
    // click through is a panel that edits the grid you cannot see.
    setInterceptsMouseClicks (true, true);
}

juce::String about::displayUrl (juce::StringRef fullUrl)
{
    auto text = juce::String (fullUrl);

    for (const auto* scheme : { "https://", "http://" })
        if (text.startsWithIgnoreCase (scheme))
            text = text.substring (juce::String (scheme).length());

    return text.endsWithChar ('/') ? text.dropLastCharacters (1) : text;
}

void AboutOverlay::setOpen (bool shouldBeOpen)
{
    if (! shouldBeOpen)
    {
        entrancePoll.stopTimer();
        hoveredUrl = {};
        setMouseCursor (juce::MouseCursor::NormalCursor);
        setVisible (false);
        return;
    }

    progress = 0.0;
    setVisible (true);
    toFront (true);
    grabKeyboardFocus();

    entrancePoll.restart();
    entrancePoll.tick = [this] { poll(); };
    entrancePoll.startTimerHz (kUiPollHz);
}

void AboutOverlay::poll()
{
    // The CLAMPED overload, which `Surface.h:144` exists for and which every other
    // animation in this chassis uses. Unclamped, a message-thread stall makes
    // this the one entrance that snaps to its end instead of finishing over a
    // frame. /simplify.
    advanceEntrance (entrancePoll.secondsSinceLastTick (about::kEntranceSeconds));
}

void AboutOverlay::advanceEntrance (double seconds) noexcept
{
    if (progress >= 1.0)
        return;

    progress = juce::jlimit (0.0, 1.0, progress + seconds / about::kEntranceSeconds);

    if (progress >= 1.0)
        entrancePoll.stopTimer();

    repaint();
}

juce::Rectangle<int> AboutOverlay::panelBounds() const noexcept
{
    // Sized to its CONTENT, not to a number: two author rows, the repository
    // line and the licence line, each a text box the type scale decides.
    const auto rowHeight = type::boxHeight (type::Style::profileName);

    const auto rows = static_cast<int> (about::authors.size()) * 2   // name + url
                    + 2;                                            // repo + licence

    const auto contentHeight = rows * rowHeight
                             + (rows - 1) * about::kRowGap
                             + about::kBlockGap * 2
                             + type::boxHeight (type::Style::kitTitle);

    const auto height = contentHeight + about::kPanelPad * 2;

    return juce::Rectangle<int> (about::kPanelWidth, height)
               .withCentre (getLocalBounds().getCentre());
}

AboutOverlay::Layout AboutOverlay::layout() const noexcept
{
    Layout out;

    out.panel = panelBounds();

    auto content = out.panel.reduced (about::kPanelPad);

    const auto takeRow = [&content] (type::Style style)
    {
        return content.removeFromTop (type::boxHeight (style));
    };

    out.title = takeRow (type::Style::kitTitle);
    content.removeFromTop (about::kBlockGap);

    for (size_t i = 0; i < about::authors.size(); ++i)
    {
        out.names[i] = takeRow (type::Style::profileName);
        content.removeFromTop (about::kRowGap);

        out.urls[i] = takeRow (type::Style::profileName);
        content.removeFromTop (about::kRowGap);
    }

    content.removeFromTop (about::kBlockGap - about::kRowGap);

    out.repository = takeRow (type::Style::profileName);
    content.removeFromTop (about::kRowGap);

    out.licence = takeRow (type::Style::profileName);

    return out;
}

juce::String AboutOverlay::urlAt (juce::Point<int> point) const
{
    const auto l = layout();

    // NARROWED TO THE TEXT'S OWN WIDTH, not the full row. A row spans the
    // panel's content box, so a click far to the right of a short url would
    // otherwise open it — a link whose hit area is nowhere near the thing that
    // looks like a link.
    const auto hits = [&point] (juce::Rectangle<int> row, juce::StringRef text)
    {
        return linkBox (row, text).contains (point);
    };

    for (size_t i = 0; i < about::authors.size(); ++i)
        if (hits (l.urls[i], about::displayUrl (about::authors[i].url)))
            return juce::String (about::authors[i].url);

    if (hits (l.repository, about::displayUrl (about::repository)))
        return juce::String (about::repository);

    return {};
}

void AboutOverlay::paint (juce::Graphics& g)
{
    // `cubicBezierEase` is Surface.h's, hoisted there by 07-02; the control
    // points are the kit overlay's, so the chassis has ONE overlay entrance.
    const auto eased = static_cast<float> (
        cubicBezierEase (progress, kit::kEaseX1, kit::kEaseY1,
                                   kit::kEaseX2, kit::kEaseY2));

    // ── the scrim ──────────────────────────────────────────────────────────
    g.setColour (lnf.token (theme::Token::bg).withAlpha (eased * about::kScrimOpacity));
    g.fillAll();

    // ── the panel, rising as it fades in ───────────────────────────────────
    const auto rise = juce::roundToInt ((1.0f - eased)
                                        * static_cast<float> (kit::kEntranceOffset));

    const auto l = layout();
    const auto panel = l.panel.translated (0, rise);
    const auto radius = lnf.cornerRadius (1);

    g.setColour (lnf.token (theme::Token::panel).withAlpha (eased));
    g.fillRoundedRectangle (panel.toFloat(), radius);

    g.setColour (lnf.token (theme::Token::line).withAlpha (eased));
    g.drawRoundedRectangle (panel.toFloat().reduced (0.5f), radius, 1.0f);

    // Every row drawn from the SAME layout the hit test reads, offset by the
    // entrance rise.
    const auto drawRow = [&g, rise, eased] (juce::Rectangle<int> row, juce::StringRef text,
                                            juce::Colour colour, type::Style style)
    {
        g.setColour (colour.withAlpha (eased));
        type::drawTracked (g, style, text, row.translated (0, rise).toFloat(),
                           juce::Justification::centredLeft);
    };

    drawRow (l.title, utf8 ("FORR\xc3\x93 BOX"), lnf.token (theme::Token::fg),
             type::Style::kitTitle);

    for (size_t i = 0; i < about::authors.size(); ++i)
    {
        const auto& author = about::authors[i];

        drawRow (l.names[i], utf8 (author.name), lnf.token (theme::Token::fg),
                 type::Style::profileName);

        drawLink (g, l.urls[i], about::displayUrl (author.url), juce::String (author.url),
                  rise, eased);
    }

    drawLink (g, l.repository, about::displayUrl (about::repository),
              juce::String (about::repository), rise, eased);

    drawRow (l.licence, utf8 (about::licence), lnf.token (theme::Token::fgDim),
             type::Style::profileName);
}

void AboutOverlay::drawLink (juce::Graphics& g, juce::Rectangle<int> row, juce::StringRef text,
                             juce::StringRef fullUrl, int rise, float eased)
{
    // A LINK LOOKS LIKE ONE, and the hover state is the affordance: the accent
    // colour plus a rule under it. Without that the URLs read as the plain text
    // they were before, and a clickable thing nobody can tell is clickable is
    // the same as not having it.
    const auto hovered = hoveredUrl.isNotEmpty() && hoveredUrl == juce::String (fullUrl);

    // `--active` is this chassis's "you can interact with this" colour — the
    // knob's focus ring and the lit segment both use it. Not a new hover
    // language invented for the panel.
    const auto colour = hovered ? lnf.token (theme::Token::active)
                                : lnf.token (theme::Token::fgDim);

    const auto box = row.translated (0, rise);

    g.setColour (colour.withAlpha (eased));
    type::drawTracked (g, type::Style::profileName, text, box.toFloat(),
                       juce::Justification::centredLeft);

    if (! hovered)
        return;

    const auto underline = linkBox (box, text);

    g.fillRect (underline.getX(), underline.getBottom() - 1, underline.getWidth(), 1);
}

void AboutOverlay::mouseUp (const juce::MouseEvent& event)
{
    // A LINK OPENS AND DOES NOT DISMISS. Dismissing as well would close the
    // panel behind whatever the browser did or did not do, leaving no way to
    // tell the two apart.
    if (const auto url = urlAt (event.getPosition()); url.isNotEmpty())
    {
        // The URL STAYS ON SCREEN whether this succeeds or not, which is the
        // mitigation for the plan's own objection that "a link that silently
        // does nothing is worse than text". It is not instead of the text; it
        // is on top of it, so a host that blocks this leaves the address
        // readable and typeable.
        juce::URL (url).launchInDefaultBrowser();
        return;
    }

    // Anywhere else dismisses. There is no destructive action behind this panel
    // and nothing to confirm, so a click-off is the whole interaction — the same
    // thing the kit overlay's scrim does.
    setOpen (false);
}

void AboutOverlay::mouseMove (const juce::MouseEvent& event)
{
    const auto url = urlAt (event.getPosition());

    if (url == hoveredUrl)
        return;

    hoveredUrl = url;

    setMouseCursor (url.isNotEmpty() ? juce::MouseCursor::PointingHandCursor
                                     : juce::MouseCursor::NormalCursor);
    repaint();
}

void AboutOverlay::mouseExit (const juce::MouseEvent&)
{
    if (hoveredUrl.isEmpty())
        return;

    hoveredUrl = {};
    setMouseCursor (juce::MouseCursor::NormalCursor);
    repaint();
}

bool AboutOverlay::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        setOpen (false);
        return true;
    }

    return false;
}

} // namespace forrobox
