#include "SidePanel.h"

#include "Chassis.h"
#include "MixBus.h"
#include "PluginProcessor.h"
#include "Profiles.h"
#include "Theme.h"
#include "Typography.h"

namespace forrobox
{

int SidePanelLayout::profileHeight (bool showsDescription) noexcept
{
    const auto name = textBox (type::Style::profileName);

    if (! showsDescription)
        return name + side::kProfilePadY * 2 + 2;   // + the 1 px border, both edges

    const auto line = static_cast<int> (type::styleFor (type::Style::profileDescription).heightPx
                                        * side::kDescriptionLineHeight + 0.5f);

    return name + side::kDescriptionMarginTop + line * 3 + side::kProfilePadY * 2 + 2;
}

int SidePanelLayout::timbreHeight() noexcept
{
    // A flex row of the two stacked labels against the LED — css:417's
    // `align-items: center`, so the row is as tall as its tallest child.
    const auto labels = textBox (type::Style::timbreName)
                      + textBox (type::Style::timbreSubLabel);

    return flexRow (labels, side::kTimbreLedSize) + side::kTimbrePadY * 2 + 2;
}

SidePanelLayout SidePanelLayout::forBounds (juce::Rectangle<int> region,
                                            int activeProfileIndex) noexcept
{
    SidePanelLayout out {};

    out.content = region.reduced (side::kPadX, side::kPadY);

    auto remaining = out.content;

    // ── REGIONAL PROFILES, with the CUSTOM tag on the same row ─────────────
    {
        auto row = remaining.removeFromTop (flexRow (textBox (type::Style::sectionLabel),
                                                     textBox (type::Style::customTag)));

        // `justify-content: space-between` — app.js:277's inline flex row.
        const auto tagWidth = juce::roundToInt (
            type::trackedWidth (type::Style::customTag, "CUSTOM"));

        out.customTag = centredInRow (row, row.removeFromRight (tagWidth)
                                              .withHeight (textBox (type::Style::customTag)));
        out.profilesLabel = centredInRow (row, row.withHeight (textBox (type::Style::sectionLabel)));

        remaining.removeFromTop (side::kSectionInnerGap);
    }

    for (size_t i = 0; i < out.profiles.size(); ++i)
    {
        const auto active = static_cast<int> (i) == activeProfileIndex;

        auto& row = out.profiles[i];

        row.bounds = remaining.removeFromTop (profileHeight (active));

        auto inner = row.bounds.reduced (side::kProfilePadX + 1, side::kProfilePadY + 1);

        row.name = inner.removeFromTop (textBox (type::Style::profileName));

        if (active)
        {
            // `float: right` on the ● — css:405. Taken off the NAME's row, so
            // the name keeps the rest of it.
            row.dot = row.name.removeFromRight (side::kActiveDotSize)
                              .withHeight (side::kActiveDotSize);
            row.dot = centredInRow (row.name, row.dot);

            inner.removeFromTop (side::kDescriptionMarginTop);
            row.description = inner;
        }

        if (i + 1 < out.profiles.size())
            remaining.removeFromTop (side::kProfileGap);
    }

    remaining.removeFromTop (side::kSectionGap);

    // ── TIMBRE / CONVOLUTION ───────────────────────────────────────────────
    out.timbreLabel = remaining.removeFromTop (textBox (type::Style::sectionLabel));
    remaining.removeFromTop (side::kSectionInnerGap);

    for (size_t i = 0; i < out.timbres.size(); ++i)
    {
        auto& row = out.timbres[i];

        row.bounds = remaining.removeFromTop (timbreHeight());

        auto inner = row.bounds.reduced (side::kTimbrePadX + 1, side::kTimbrePadY + 1);

        row.led = centredInRow (inner, inner.removeFromRight (side::kTimbreLedSize)
                                            .withHeight (side::kTimbreLedSize));

        const auto stacked = textBox (type::Style::timbreName)
                           + textBox (type::Style::timbreSubLabel);

        auto labels = centredInRow (inner, inner.withHeight (stacked));

        row.name = labels.removeFromTop (textBox (type::Style::timbreName));
        row.subLabel = labels;

        if (i + 1 < out.timbres.size())
            remaining.removeFromTop (side::kTimbreGap);
    }

    remaining.removeFromTop (side::kSectionInnerGap);

    // ── the MIX row ────────────────────────────────────────────────────────
    {
        const auto knobHeight = Knob::preferredHeight (side::kMixKnobSize, true);
        const auto buttonHeight = Button::heightOf (Button::Variant::base);

        auto row = remaining.removeFromTop (flexRow (knobHeight, buttonHeight));

        out.mixRow = row;
        out.mixKnob = centredInRow (row, row.removeFromLeft (side::kMixKnobSize)
                                            .withHeight (knobHeight));
        row.removeFromLeft (side::kMixGap);

        // `flex: 1` — css:433, so LOAD IR… takes whatever is left.
        out.loadIr = centredInRow (row, row.withHeight (buttonHeight));
    }

    // ── the bundle footer, pushed to the bottom ────────────────────────────
    //
    // `margin-top: auto` — css:436. DERIVED from what is left rather than
    // written as an offset, so a section above it changing height moves nothing
    // else and this stays on the floor.
    {
        const auto textHeight = textBox (type::Style::bundleText);
        const auto height = side::kBundlePadTop + flexRow (textHeight, side::kBundleDotSize);

        out.bundle = out.content.removeFromBottom (height);

        auto row = out.bundle.withTrimmedTop (side::kBundlePadTop);

        out.bundleDot = centredInRow (row, row.removeFromLeft (side::kBundleDotSize)
                                              .withHeight (side::kBundleDotSize));
        row.removeFromLeft (side::kBundleGap);
        out.bundleText = centredInRow (row, row.withHeight (textHeight));
    }

    return out;
}

SidePanel::SidePanel (ForroBoxLookAndFeel& lookAndFeelToUse) : lnf (lookAndFeelToUse)
{
    setOpaque (true);

    // A STUB, and the prototype's own. PLANNING.md:56 lists `LOAD IR…` beside
    // the LOAD buttons, SYNC and the preset cycler as non-functional there, so
    // it is built and left unwired rather than omitted — the region reads as the
    // design intends, and the absence of a callback is the honest statement.
    loadIrButton = std::make_unique<Button> (lnf, Button::Variant::base,
                                             juce::String::fromUTF8 ("LOAD IR\xe2\x80\xa6"));
    addAndMakeVisible (*loadIrButton);

    // In the parameter's own CHOICE order, which is `timbreSpecs`' order — the
    // same table MixBus reads its cutoff and drive from, so the row that lights
    // and the character that sounds cannot be two answers.
    for (size_t i = 0; i < timbreRows.size(); ++i)
    {
        timbreRows[i] = std::make_unique<TimbreRow> (lnf, static_cast<int> (i));
        addAndMakeVisible (*timbreRows[i]);
    }

    mixKnob = std::make_unique<Knob> (lnf, side::kMixKnobSize, Knob::Polarity::unipolar,
                                      theme::accent (theme::Accent::triangulo), "MIX");
    addAndMakeVisible (*mixKnob);
}

SidePanel::~SidePanel() = default;

void SidePanel::attachParameters (juce::AudioProcessorValueTreeState& state)
{
    processor = dynamic_cast<::ForroBoxAudioProcessor*> (&state.processor);

    if (auto* timbre = dynamic_cast<juce::RangedAudioParameter*> (state.getParameter (ids::timbre)))
    {
        // A plain ParameterAttachment, not `ChoiceButtonsAttachment`: that one
        // binds `Button*`, and these rows are their own control. The law is the
        // same and stated once here — the ROW lights from the parameter, and a
        // click writes the parameter and nothing else.
        timbreAttachment = std::make_unique<juce::ParameterAttachment> (
            *timbre,
            [this, timbre] (float value)
            {
                const auto chosen = juce::roundToInt (
                    timbre->convertFrom0to1 (timbre->convertTo0to1 (value)));

                for (auto& row : timbreRows)
                    if (row != nullptr)
                        row->setSelected (row->getIndex() == chosen);
            });

        for (auto& row : timbreRows)
            row->onClick = [this, index = row->getIndex()]
            {
                // Through the attachment, so the host sees a complete gesture —
                // the same bracketing every other control here uses.
                timbreAttachment->setValueAsCompleteGesture (static_cast<float> (index));
            };

        timbreAttachment->sendInitialUpdate();
    }

    if (auto* mix = dynamic_cast<juce::RangedAudioParameter*> (state.getParameter (ids::charMix)))
        mixAttachment = std::make_unique<KnobAttachment> (*mix, *mixKnob);

    refreshFromState();

    // The panel's own tick: the CUSTOM tag's fade, and following the stored
    // profile and dirty flag. Its own, as every other region here owns one.
    statePoll.tick = [this] { poll(); };
    statePoll.startTimerHz (kSidePanelPollHz);
}

void SidePanel::poll()
{
    refreshFromState();

    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const auto previous = lastPollSeconds;

    lastPollSeconds = now;

    if (previous <= 0.0)
        return;   // the first tick has no interval to report

    advanceCustomTag (juce::jlimit (0.0, side::kCustomTagFadeSeconds, now - previous));
}

void SidePanel::refreshFromState()
{
    if (processor == nullptr)
        return;

    juce::String stored;
    auto isDirty = false;

    {
        auto handle = processor->lockPatternState();

        stored = handle->activeProfile;
        isDirty = handle->dirty;
    }

    // BY ID, never by index. A project saved by a newer build may carry a
    // profile this one does not know, and -1 is the honest answer — `findProfile`
    // returns nullptr for exactly that reason rather than resolving to the wrong
    // groove, and a fallback of 0 here would light CAMPINA over a state that is
    // not campina.
    auto found = -1;

    for (size_t i = 0; i < ids::profileInfos.size(); ++i)
        if (stored == ids::profileInfos[i].id)
            found = static_cast<int> (i);

    const auto layoutChanged = found != activeProfile;

    activeProfile = found;
    dirty = isDirty;

    // The tag is at rest whenever it agrees with the flag; only a CHANGE
    // animates, which is what css:411's transition does.
    if (! dirty)
        tagOpacity = 0.0f;

    if (layoutChanged)
        resized();   // the active button is taller, so the whole column moves

    repaint();
}

void SidePanel::advanceCustomTag (double seconds) noexcept
{
    const auto target = dirty ? 1.0f : 0.0f;

    if (juce::approximatelyEqual (tagOpacity, target))
        return;

    const auto step = static_cast<float> (seconds / side::kCustomTagFadeSeconds);

    tagOpacity = target > tagOpacity ? juce::jmin (target, tagOpacity + step)
                                     : juce::jmax (target, tagOpacity - step);

    repaint (layout.customTag);
}

void SidePanel::resized()
{
    layout = SidePanelLayout::forBounds (getLocalBounds(), activeProfile);

    loadIrButton->setBounds (layout.loadIr);
    mixKnob->setBounds (layout.mixKnob.withHeight (
        Knob::preferredHeight (side::kMixKnobSize, true)));

    for (size_t i = 0; i < timbreRows.size() && i < layout.timbres.size(); ++i)
        timbreRows[i]->setBounds (layout.timbres[i].bounds);
}

void SidePanel::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds();

    g.setColour (lnf.token (theme::Token::raised));
    g.fillRect (area);

    // `border-left: 1px solid var(--line)` AND `inset 0 1px 0 <highlight>`
    // (css:387, css:600). An inset box-shadow is drawn inside the border box, so
    // the highlight starts one column right of the border rather than over it.
    g.setColour (lnf.token (theme::Token::line));
    g.fillRect (area.getX(), area.getY(), 1, area.getHeight());

    surface::raisedHighlight (g, area.withTrimmedLeft (1), lnf.shadows().raisedHighlight);

    const auto clip = g.getClipBounds();

    if (layout.profilesLabel.intersects (clip))
    {
        g.setColour (lnf.token (theme::Token::fgFaint));
        type::drawTracked (g, type::Style::sectionLabel, "REGIONAL PROFILES",
                           layout.profilesLabel.toFloat(), juce::Justification::centredLeft);
    }

    // `opacity: 0` at rest, `1` when dirty — css:411/413, faded rather than
    // switched.
    if (tagOpacity > 0.0f && layout.customTag.intersects (clip))
    {
        g.setColour (theme::accent (theme::Accent::pandeiro).withAlpha (tagOpacity));
        type::drawTracked (g, type::Style::customTag, "CUSTOM",
                           layout.customTag.toFloat(), juce::Justification::centredLeft);
    }

    paintProfiles (g, clip);

    if (layout.timbreLabel.intersects (clip))
    {
        g.setColour (lnf.token (theme::Token::fgFaint));
        type::drawTracked (g, type::Style::sectionLabel, "TIMBRE / CONVOLUTION",
                           layout.timbreLabel.toFloat(), juce::Justification::centredLeft);
    }

    paintBundle (g);
}

void SidePanel::paintProfiles (juce::Graphics& g, juce::Rectangle<int> clip) const
{
    const auto radius = lnf.cornerRadius();

    for (size_t i = 0; i < layout.profiles.size(); ++i)
    {
        const auto& row = layout.profiles[i];

        if (! row.bounds.intersects (clip))
            continue;

        const auto active = static_cast<int> (i) == activeProfile;
        const auto& info = ids::profileInfos[i];

        g.setColour (lnf.token (active ? theme::Token::active : theme::Token::panel));
        g.fillRoundedRectangle (row.bounds.toFloat(), radius);

        g.setColour (lnf.token (active ? theme::Token::active : theme::Token::line));
        g.drawRoundedRectangle (row.bounds.toFloat().reduced (0.5f), radius, 1.0f);

        // `--bg` on the active button, which is the ground it sits on — css:402.
        g.setColour (lnf.token (active ? theme::Token::bg : theme::Token::fg));
        type::drawTracked (g, type::Style::profileName,
                           juce::String (juce::CharPointer_UTF8 (info.displayName)),
                           row.name.toFloat(), juce::Justification::centredLeft);

        if (! active)
            continue;

        g.setColour (theme::accent (theme::Accent::zabumba));
        g.fillEllipse (row.dot.toFloat());

        // BLACK at 0.6 in dark, WHITE at 0.7 in light — css:403 and css:404, two
        // rules rather than one colour at one alpha. `--active` inverts between
        // the themes, so `--bg` at a single alpha read correctly in dark and
        // wrongly in light.
        const auto dark = lnf.getMode() == theme::Mode::dark;

        g.setColour ((dark ? juce::Colours::black : juce::Colours::white)
                         .withAlpha (dark ? side::kDescriptionAlpha
                                          : side::kDescriptionAlphaLight));

        auto lineBox = row.description.withHeight (
            juce::roundToInt (type::styleFor (type::Style::profileDescription).heightPx
                              * side::kDescriptionLineHeight));

        for (const auto* line : info.description)
        {
            type::drawTracked (g, type::Style::profileDescription,
                               juce::String (juce::CharPointer_UTF8 (line)),
                               lineBox.toFloat(), juce::Justification::centredLeft);

            lineBox = lineBox.translated (0, lineBox.getHeight());
        }
    }
}


void SidePanel::paintBundle (juce::Graphics& g) const
{
    // `border-top: 1px solid var(--line)` — css:437.
    g.setColour (lnf.token (theme::Token::line));
    g.fillRect (layout.bundle.getX(), layout.bundle.getY(), layout.bundle.getWidth(), 1);

    const auto glow = theme::accent (theme::Accent::ganza);

    juce::DropShadow (glow, juce::roundToInt (side::kTimbreLedGlowRadius), {})
        .drawForRectangle (g, layout.bundleDot);

    g.setColour (glow);
    g.fillEllipse (layout.bundleDot.toFloat());

    // `BUNDLE: ` dim, `MINIMAL` in `--fg` — css:441's `b` inside the mono run.
    const auto prefix = juce::String ("BUNDLE: ");
    const auto prefixWidth = juce::roundToInt (
        type::trackedWidth (type::Style::bundleText, prefix));

    g.setColour (lnf.token (theme::Token::fgDim));
    type::drawTracked (g, type::Style::bundleText, prefix,
                       layout.bundleText.toFloat(), juce::Justification::centredLeft);

    g.setColour (lnf.token (theme::Token::fg));
    type::drawTracked (g, type::Style::bundleText, "MINIMAL",
                       layout.bundleText.withTrimmedLeft (prefixWidth).toFloat(),
                       juce::Justification::centredLeft);
}

} // namespace forrobox
