#include "SidePanel.h"

#include "Chassis.h"
#include "MixBus.h"
#include "PluginProcessor.h"
#include "Profiles.h"
#include "Theme.h"
#include "TimbreRow.h"
#include "Typography.h"

namespace forrobox
{
/** `BUNDLE: ` — the dim half of css:441's mono run. */
const juce::String& bundleLabelText()
{
    static const juce::String text ("BUNDLE: ");

    return text;
}

int SidePanelLayout::profileHeight (bool showsDescription) noexcept
{
    // ASKED of the control that paints it — the box model moved with the paint.
    return ProfileButton::heightOf (showsDescription);
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

        row.bounds = remaining.removeFromTop (TimbreRow::heightOf());

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

        // Off `remaining`, not off `content`: `content` is published as "the
        // region minus its padding" and a caller reads it, so eating its bottom
        // edge here made the field disagree with its own name.
        out.bundle = remaining.removeFromBottom (height);

        auto row = out.bundle.withTrimmedTop (side::kBundlePadTop);

        out.bundleDot = centredInRow (row, row.removeFromLeft (side::kBundleDotSize)
                                              .withHeight (side::kBundleDotSize));
        row.removeFromLeft (side::kBundleGap);

        auto text = centredInRow (row, row.withHeight (textHeight));

        out.bundleLabel = text;
        out.bundleValue = text.withTrimmedLeft (juce::roundToInt (
            type::trackedWidth (type::Style::bundleText, bundleLabelText())));
    }

    return out;
}

SidePanel::SidePanel (ForroBoxLookAndFeel& lookAndFeelToUse) : lnf (lookAndFeelToUse)
{
    setOpaque (true);

    // REAL SINCE 09-07. It was built and left unwired through Phases 6-8 because
    // `PLANNING.md:56` lists it beside the LOAD buttons, SYNC and the preset
    // cycler as non-functional in the prototype — the region read as the design
    // intends and the absent callback was the honest statement. `PLANNING.md:841`
    // says what it should do, and now it does it.
    loadIrButton = std::make_unique<Button> (lnf, Button::Variant::base,
                                             juce::String::fromUTF8 ("LOAD IR…"));
    loadIrButton->onClick = [this] { chooseImpulseResponse(); };
    addAndMakeVisible (*loadIrButton);

    // In `ids::profileInfos` order — the table `ChassisLayout::indexOfProfile`
    // resolves a stored id against, so a reordered list cannot mislabel one.
    for (size_t i = 0; i < profileButtons.size(); ++i)
    {
        profileButtons[i] = std::make_unique<ProfileButton> (lnf, static_cast<int> (i));
        addAndMakeVisible (*profileButtons[i]);
    }


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

void SidePanel::chooseImpulseResponse()
{
    if (processor == nullptr || irChooser != nullptr)
        return;

    // launchAsync, NEVER browseForFileToOpen. `JUCE_MODAL_LOOPS_PERMITTED=1` is
    // set on the TEST target only and deliberately — CMakeLists.txt says so
    // beside it: "modal loops in a plugin are exactly what the default forbids".
    // A modal call here compiles and deadlocks a host. 07-02 met this with the
    // MIDI export; the same rule, the same shape.
    irChooser = std::make_unique<juce::FileChooser> (
        juce::String::fromUTF8 ("Load impulse response"),
        juce::File::getSpecialLocation (juce::File::userMusicDirectory),
        juce::String ("*.wav;*.aiff;*.aif;*.flac"));

    irChooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<SidePanel> (this)] (const juce::FileChooser& fc)
        {
            auto* panel = safe.getComponent();

            if (panel == nullptr)
                return;

            const auto file = fc.getResult();

            // Released so the next click can open a dialog again, and through a
            // SafePointer because the editor may have closed while it was up.
            panel->irChooser.reset();

            // An empty result is a dismissed dialog, which must change nothing.
            if (file == juce::File() || panel->processor == nullptr)
                return;

            // TOLD, not discarded. 07-02 met this one control over and said so:
            // without a message "the dialog simply closes and the user believes
            // the export was written". Here they would believe the IR loaded,
            // and both failure modes — unreadable and undecodable — look
            // identical to silence. /code-review.
            if (! panel->processor->loadImpulseResponse (file))
                juce::NativeMessageBox::showAsync (
                    juce::MessageBoxOptions()
                        .withIconType (juce::MessageBoxIconType::WarningIcon)
                        .withTitle (juce::String::fromUTF8 ("Could not load impulse response"))
                        .withMessage (file.getFileName()
                                        + juce::String::fromUTF8 (
                                            " could not be read as audio. The convolution is "
                                            "unchanged."))
                        .withButton ("OK"),
                    nullptr);
        });
}

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
            [this] (float value)
            {
                // `ParameterAttachment` hands the callback a DENORMALISED value,
                // which `ChoiceButtonsAttachment.cpp` records in its own comment
                // — so the convertTo/convertFrom pair this used to do cancelled.
                const auto chosen = juce::roundToInt (value);

                for (auto& row : timbreRows)
                {
                    row->setSelected (row->getIndex() == chosen);

                    // css:426's blink is `.timbre.ciclo.active` — it starts and
                    // stops with the SELECTION, so the row is told here rather
                    // than polling a parameter of its own. The row's own
                    // `KeyframeLoop` holds the clock; a poll beside it here
                    // would be a second driver, which is what the chassis's
                    // flicker had and what made its 4 s cycle run in 2 s.
                    row->selectionChanged();
                }
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

    for (auto& button : profileButtons)
        button->onClick = [this, index = button->getIndex()]
        {
            if (processor == nullptr)
                return;

            // The PROCESSOR's reload, which the header's STYLE control calls
            // too. Two entry points, one law — they must not be able to load the
            // same profile into two different states.
            processor->loadProfile (allProfiles()[static_cast<size_t> (index)]);

            refreshFromState();

            if (onProfileLoaded != nullptr)
                onProfileLoaded();
        };

    refreshFromState();

    // The panel's own tick: the CUSTOM tag's fade, and following the stored
    // profile and dirty flag. Its own, as every other region here owns one.
    statePoll.tick = [this] { poll(); };
    statePoll.startTimerHz (kUiPollHz);
}

void SidePanel::poll()
{
    refreshFromState();

    advanceCustomTag (statePoll.secondsSinceLastTick (side::kCustomTagFadeSeconds));
}

void SidePanel::refreshFromState()
{
    if (processor == nullptr)
        return;


    // ONE predicate, on the processor, which the header's STYLE control reads
    // too — an edited state is no longer the profile it names, so the highlight
    // clears even while `activeProfile` still holds the id (`app.js:555`,
    // `PLANNING.md:601`). -1 also covers a profile this build does not know,
    // which is what a project saved by a newer one carries.
    // ONE lock, both facts. This took the pattern lock twice per 30 Hz tick —
    // once inside `selectedProfileIndex` and again to recover the `dirty` that
    // call had collapsed into its -1. And because `dirty` is the steady state
    // after any edit, the second take fired on nearly every tick, which is the
    // cost the comment here used to claim it had avoided. /simplify.
    const auto selection = processor->profileSelection();

    const auto found = selection.index;
    const auto isDirty = selection.dirty;

    const auto layoutChanged = found != activeProfile;
    const auto dirtyChanged = isDirty != dirty;

    activeProfile = found;
    dirty = isDirty;

    for (size_t i = 0; i < profileButtons.size(); ++i)
        profileButtons[i]->setActive (static_cast<int> (i) == activeProfile);

    if (layoutChanged)
        resized();   // the active button is taller, so the whole column moves

    // ONLY ON CHANGE. This repainted unconditionally, which at 30 Hz meant
    // invalidating an opaque 280-px column with four painted buttons and three
    // children for the whole session, in the one region that is otherwise
    // static. `advanceCustomTag` repaints its own box while the tag is moving.
    if (layoutChanged || dirtyChanged)
        repaint();
}

void SidePanel::advanceCustomTag (double seconds) noexcept
{
    // BOTH WAYS. `refreshFromState` used to snap the opacity to 0 whenever the
    // flag was clear, and it runs first in `poll()` — so the fade-out arm below
    // could never execute in the plugin, while the comment beside the snap cited
    // css:411's transition, which is symmetric. The test passed on the snap
    // rather than on the fade. /simplify.
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
    mixKnob->setBounds (layout.mixKnob);

    for (size_t i = 0; i < profileButtons.size(); ++i)
        profileButtons[i]->setBounds (layout.profiles[i].bounds);

    for (size_t i = 0; i < timbreRows.size(); ++i)
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

    if (layout.timbreLabel.intersects (clip))
    {
        g.setColour (lnf.token (theme::Token::fgFaint));
        type::drawTracked (g, type::Style::sectionLabel, "TIMBRE / CONVOLUTION",
                           layout.timbreLabel.toFloat(), juce::Justification::centredLeft);
    }

    paintBundle (g, clip);
}


void SidePanel::paintBundle (juce::Graphics& g, juce::Rectangle<int> clip) const
{
    // The ONE painter here that was not culled. A clip touching nothing else
    // still cost 22 us and 460 allocations, because text layout happens before
    // clipping rejects it — the lesson `KitOverlay::paintPanel` and
    // `Chassis::paintStrip` each record. Measured by /simplify.
    if (! layout.bundle.intersects (clip))
        return;

    // `border-top: 1px solid var(--line)` — css:437.
    g.setColour (lnf.token (theme::Token::line));
    g.fillRect (layout.bundle.getX(), layout.bundle.getY(), layout.bundle.getWidth(), 1);

    surface::glowDot (g, layout.bundleDot, theme::accent (theme::Accent::ganza),
                      juce::roundToInt (side::kBundleDotGlowRadius));

    // `BUNDLE: ` dim, `MINIMAL` in `--fg` — css:441's `b` inside the mono run.
    // The split is the LAYOUT's, measured once.
    g.setColour (lnf.token (theme::Token::fgDim));
    type::drawTracked (g, type::Style::bundleText, bundleLabelText(),
                       layout.bundleLabel.toFloat(), juce::Justification::centredLeft);

    g.setColour (lnf.token (theme::Token::fg));
    type::drawTracked (g, type::Style::bundleText, "MINIMAL",
                       layout.bundleValue.toFloat(), juce::Justification::centredLeft);
}

} // namespace forrobox
