#include "HeaderBar.h"

#include "PluginProcessor.h"

#include "KnobAttachment.h"
#include "Surface.h"

namespace forrobox
{

HeaderBar::HeaderBar (ForroBoxLookAndFeel& lookAndFeelToUse)
    : lnf (lookAndFeelToUse)
{
    // Opaque for the same reason Chassis is: the bar fills every pixel of its
    // bounds with the header gradient, so JUCE can skip painting what is behind
    // it. A region that failed to paint would show as the chassis's --bg.
    setOpaque (true);
}

HeaderBar::~HeaderBar() = default;

void HeaderBar::attachParameters (juce::AudioProcessorValueTreeState& apvts)
{
    buildHeaderControls (apvts);

    // The poll exists for the two things with no attachment to hang them on:
    // `playing` is an atomic outside the parameter surface, and the host's
    // tempo is published from processBlock. Nothing else rides along — the two
    // global readouts hang off the knob's own onProportionChanged, which is
    // what /simplify took them off this timer to do at 04-04.
    polledProcessor = dynamic_cast<::ForroBoxAudioProcessor*> (&apvts.processor);
    polledApvts = &apvts;

    if (polledProcessor != nullptr)
    {
        headerPoll.tick = [this] { refreshFromProcessor(); };
        headerPoll.startTimerHz (kHeaderPollHz);
        refreshFromProcessor();
    }

    resized();
}

void HeaderBar::refreshFromProcessor()
{
    auto& header = headerControls;

    if (header.play == nullptr || polledProcessor == nullptr || polledApvts == nullptr)
        return;

    auto& processor = *polledProcessor;
    auto& apvts = *polledApvts;

    // ── the transport ──────────────────────────────────────────────────────
    //
    // While SYNC is on the HOST's transport is the only one that matters, so
    // the button shows the host's state and refuses clicks — a Play button that
    // still responded would be lying about what it controls. The same treatment
    // the BPM field gets under the same condition.
    const auto* syncValue = apvts.getRawParameterValue (ids::sync);
    const auto synced = syncValue != nullptr
                     && syncValue->load (std::memory_order_relaxed) > 0.5f;

    header.play->setOn (synced ? processor.isHostTransportRolling() : processor.isPlaying());
    header.play->setReadOnly (synced);
    header.stop->setReadOnly (synced);

    // ── SYNC: the field shows the HOST's tempo and refuses every gesture ────
    if (header.bpmAttachment != nullptr)
        header.bpmAttachment->setSyncedToHost (synced, processor.getHostBpm());

    // ── STYLE: the persisted profile, which has no listener either ─────────
    //
    // `activeProfile` lives in the ValueTree state, not in the APVTS, so there
    // is no parameter to attach to and this poll is its only path. Asked of the
    // state rather than cached beside it, the rule the ghost readout ended up
    // with — and `setSelectedIndex` already early-outs when the index has not
    // changed, so this costs a compare per tick.
    // ONE predicate with the side panel's — an EDITED state is no longer the
    // profile it names, so the lit segment clears while `activeProfile` still
    // holds the id (`app.js:555`, `PLANNING.md:601-602`). This read
    // `indexOfProfile` alone and so kept a segment lit over a state that had
    // stopped being that groove.
    if (header.style != nullptr)
        header.style->setSelectedIndex (processor.selectedProfileIndex());

    // The two global readouts are NOT polled: they hang off the knob's own
    // onProportionChanged, so this tick is exactly the things with no listener
    // to hang off — the transport's atomic, the host's tempo, and the profile.
}

void HeaderBar::buildHeaderControls (juce::AudioProcessorValueTreeState& apvts)
{
    auto& header = headerControls;

    header = {};

    header.logo = std::make_unique<LogoMark> (lnf);

    // ── the BPM cluster ────────────────────────────────────────────────────
    header.bpm = std::make_unique<BpmField> (lnf);

    if (auto* bpmParameter = dynamic_cast<juce::RangedAudioParameter*> (
                                 apvts.getParameter (ids::bpm)))
        header.bpmAttachment = std::make_unique<BpmAttachment> (*bpmParameter, *header.bpm);

    header.sync = std::make_unique<Button> (lnf, Button::Variant::base, "SYNC");

    if (auto* syncParameter = dynamic_cast<juce::RangedAudioParameter*> (
                                  apvts.getParameter (ids::sync)))
        header.syncAttachment = std::make_unique<ToggleAttachment> (*syncParameter, *header.sync);

    // div-2 and x2 halve and double, clamped — PLANNING.md:399. Not stubs: the
    // BPM parameter through a different gesture, and through the ATTACHMENT,
    // which is the only thing here holding the parameter.
    auto* bpmAttachment = header.bpmAttachment.get();

    const auto scaleBpm = [bpmAttachment] (float factor)
    {
        return [bpmAttachment, factor]
        {
            if (bpmAttachment != nullptr)
                bpmAttachment->scaleBy (factor);
        };
    };

    header.half = std::make_unique<Button> (lnf, Button::Variant::mini,
                                            juce::String (juce::CharPointer_UTF8 ("÷2")));
    header.half->onClick = scaleBpm (0.5f);

    header.doubleUp = std::make_unique<Button> (lnf, Button::Variant::mini,
                                                juce::String (juce::CharPointer_UTF8 ("×2")));
    header.doubleUp->onClick = scaleBpm (2.0f);

    // ── transport ──────────────────────────────────────────────────────────
    //
    // The only controls here bound to something that is NOT a parameter.
    // `playing` is an atomic on the processor, deliberately outside both the
    // parameter surface and persisted state, so there is no attachment to hang
    // these on and the lit state is polled instead.
    header.play = std::make_unique<Button> (lnf, Button::Variant::transport, "");
    header.stop = std::make_unique<Button> (lnf, Button::Variant::transport, "");

    {
        // `M7 5v14l12-7z` — app.js:71, in the icons' own 24x24 box.
        juce::Path play;
        play.startNewSubPath (7.0f, 5.0f);
        play.lineTo (7.0f, 19.0f);
        play.lineTo (19.0f, 12.0f);
        play.closeSubPath();
        header.play->setIcon ({ play, Button::kTransportIconViewBox });

        // `<rect x="6" y="6" width="12" height="12"/>` — app.js:72.
        juce::Path stop;
        stop.addRectangle (6.0f, 6.0f, 12.0f, 12.0f);
        header.stop->setIcon ({ stop, Button::kTransportIconViewBox });
    }

    if (auto* processor = dynamic_cast<::ForroBoxAudioProcessor*> (&apvts.processor))
    {
        // Play TOGGLES — `PLANNING.md:539` calls it "Play/Stop", and app.js
        // binds one handler to the button and to Space. Stop only stops, so a
        // second press on it is not a start.
        header.play->onClick = [processor] { processor->setPlaying (! processor->isPlaying()); };
        header.stop->onClick = [processor] { processor->setPlaying (false); };
    }

    // ── the two signature knobs ────────────────────────────────────────────
    const auto buildGlobalKnob = [this, &apvts] (const char* parameterId, juce::Colour colour,
                                                 std::unique_ptr<Knob>& knob,
                                                 std::unique_ptr<ValueScreen>& readout,
                                                 std::unique_ptr<KnobAttachment>& attachment)
    {
        // No micro-label under the dial: the header's knobs carry their name to
        // the RIGHT, in the meta stack, not below (css:210-211). The strip's do.
        knob = std::make_unique<Knob> (lnf, ChassisLayout::kGlobalKnobSize,
                                       Knob::Polarity::unipolar, colour, juce::String());

        readout = std::make_unique<ValueScreen> (lnf, type::Style::globalKnobReadout,
                                                 ChassisLayout::kGlobalKnobReadMinWidth,
                                                 ChassisLayout::kGlobalKnobReadPadX,
                                                 ChassisLayout::kGlobalKnobReadPadY);

        if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (
                                  apvts.getParameter (parameterId)))
        {
            attachment = std::make_unique<KnobAttachment> (*parameter, *knob);

            // The readout is the PARAMETER's own text, fired by the same
            // attachment that positions the dial — Knob's `onProportionChanged`,
            // the seam Fader has carried since 04-03. It was a 30 Hz poll until
            // /simplify pointed out that the only thing making these two
            // readouts different from the ghost readout was a plan boundary.
            auto* screen = readout.get();

            knob->onProportionChanged = [parameter, screen] (float)
            {
                screen->setText (parameter->getCurrentValueAsText());
            };

            readout->setText (parameter->getCurrentValueAsText());
        }

    };

    buildGlobalKnob (ids::swing, lnf.token (theme::Token::fg),
                     header.swing, header.swingRead, header.swingAttachment);

    // `CACHAÇA`'s value arc is the accent where SWING's is neutral —
    // PLANNING.md:390, and the one thing that distinguishes the pair visually.
    buildGlobalKnob (ids::cachaca, theme::accent (theme::Accent::zabumba),
                     header.cachaca, header.cachacaRead, header.cachacaAttachment);

    // ── the right cluster: two stubs ───────────────────────────────────────
    header.presetPrev = std::make_unique<Button> (lnf, Button::Variant::arrow,
                                                  ChassisLayout::arrowPrev());
    header.presetNext = std::make_unique<Button> (lnf, Button::Variant::arrow,
                                                  ChassisLayout::arrowNext());

    header.presetScreen = std::make_unique<ValueScreen> (lnf, type::Style::presetScreen,
                                                         ChassisLayout::kPresetScreenMinWidth,
                                                         ChassisLayout::kPresetScreenPadX,
                                                         ChassisLayout::kPresetScreenPadY);
    header.presetScreen->setText (ChassisLayout::presetStubLabel());

    header.style = std::make_unique<Segmented> (lnf, ChassisLayout::profileCodes(),
                                                type::Style::quickSwitchCode,
                                                Segmented::Variant::quickSwitch);

    // The lit segment is the PERSISTED profile, asked of the state rather than
    // stored again here — the same rule the ghost readout ended up with. A
    // `selectedProfile` field on Chassis would be a second copy of something
    // the state already holds.
    //
    // And clicking LOADS, as of 06-03 — the plan this comment was waiting for.
    // Through the PROCESSOR's `loadProfile`, which the side panel's list calls
    // too: two entry points and one law, so they cannot load the same profile
    // into two different states.
    header.style->onSegmentClicked = [this] (int index)
    {
        if (polledProcessor == nullptr
            || ! juce::isPositiveAndBelow (index, (int) allProfiles().size()))
            return;

        polledProcessor->loadProfile (allProfiles()[(size_t) index]);
        refreshFromProcessor();

        if (onProfileLoaded != nullptr)
            onProfileLoaded();
    };
    // Its lit segment is seeded by the POLL, not here — see
    // refreshFromProcessor. `activeProfile` is persisted state rather than a
    // parameter, so no attachment can carry it, and reading it once at build
    // time is the exact bug /code-review found on the footer's OUTPUT toggle in
    // this same plan: the control would light whatever profile was active when
    // the editor opened and never move again. Phase 6's headline deliverable is
    // the profile reload, so this is the control that would have shown it wrong.

    // No hand-counted size: a forgotten entry should be an invisible child, not
    // a compile error about the number 15.
    for (auto* child : std::initializer_list<juce::Component*> {
             header.logo.get(), header.bpm.get(), header.sync.get(), header.half.get(),
             header.doubleUp.get(), header.play.get(), header.stop.get(),
             header.swing.get(), header.cachaca.get(), header.swingRead.get(),
             header.cachacaRead.get(), header.presetPrev.get(), header.presetNext.get(),
             header.presetScreen.get(), header.style.get() })
        addAndMakeVisible (*child);
}

void HeaderBar::resized()
{
    // The bar's own clusters, from its OWN local bounds, and the only copy —
    // read back through getLayout(). Everything that compares a header control's
    // position against a box is then working in ONE coordinate space, which is
    // the thing that was not true when Chassis held a second copy.
    headerLayout = ChassisLayout::headerInteriorOf (getLocalBounds());

    if (headerControls.logo == nullptr)
        return;   // attachParameters has not run; the bar is a surface

    const auto& h = headerLayout;
    auto& c = headerControls;

    c.logo->setBounds (h.logoMark);
    c.bpm->setBounds (h.bpmField);
    c.sync->setBounds (h.syncButton);
    c.half->setBounds (h.halfButton);
    c.doubleUp->setBounds (h.doubleButton);

    // The transport buttons ASK for their bounds: a lit play button's glow
    // falls outside its 34x34 box, and a Component's paint is clipped to
    // its bounds.
    c.play->setBounds (Button::boundsForBox (Button::Variant::transport, h.playButton));
    c.stop->setBounds (Button::boundsForBox (Button::Variant::transport, h.stopButton));

    c.swing->setBounds (h.swingKnob);
    c.cachaca->setBounds (h.cachacaKnob);
    c.swingRead->setBounds (h.swingRead);
    c.cachacaRead->setBounds (h.cachacaRead);

    c.presetPrev->setBounds (h.presetPrev);
    c.presetScreen->setBounds (h.presetScreen);
    c.presetNext->setBounds (h.presetNext);
    c.style->setBounds (h.styleSegments);
}

void HeaderBar::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds();

    // `linear-gradient(180deg, <raised + 3% white>, <raised>)` plus the top
    // highlight and a 1 px bottom border.
    const auto raised = lnf.token (theme::Token::raised);

    g.setGradientFill (juce::ColourGradient::vertical (
        theme::mix (raised, juce::Colours::white, ChassisLayout::kHeaderGradientWeight), static_cast<float> (area.getY()),
        raised, static_cast<float> (area.getBottom())));
    g.fillRect (area);

    surface::raisedHighlight (g, area, lnf.shadows().headerHighlight);

    g.setColour (lnf.token (theme::Token::line));
    g.fillRect (area.getX(), area.getBottom() - 1, area.getWidth(), 1);

    // The header's own content: the recessed group behind the two knobs, and
    // the text that is not a component. Everything else there paints itself as
    // a child, which is what gives it hover and press for free.
    //
    // Clip-checked. These two were once gated only on "has attachParameters
    // run", so a repaint touching ANY header pixel paid for the group's 18 px
    // DropShadow, its radial gradient and five tracked text runs. Measured: a
    // 1x1 px header repaint cost 55 us where the entire footer row costs 30.
    // Found by /simplify. Still worth it now the bar is its own component: JUCE
    // clips a child to its bounds but does not narrow the clip further.
    if (headerControls.logo != nullptr)
    {
        const auto clip = g.getClipBounds();
        const auto& h = headerLayout;

        if (h.globalKnobs.intersects (clip))
            paintGlobalKnobGroup (g);

        // Per RUN, not one union over all four.
        //
        // The union of the wordmark, the two knob names and the STYLE label is
        // 62,19 978x25 — it spans the whole bar, so it intersects essentially
        // any repaint and the gate never rejected anything. Measured by
        // /simplify: 44.70 us of glyph layout on EVERY header repaint, knob-drag
        // frames included, under a comment claiming the text was being skipped.
        // The `globalKnobs` gate above it does fire, which is where the 55 us
        // that comment cites actually came from.
        paintHeaderText (g, clip);
    }
}

void HeaderBar::paintGlobalKnobGroup (juce::Graphics& g) const
{
    const auto group = headerLayout.globalKnobs;

    if (group.isEmpty())
        return;

    const auto area = group.toFloat();
    const auto radius = lnf.cornerRadius (ChassisLayout::kGlobalKnobsRadiusExtra);
    const auto accent = theme::accent (theme::Accent::zabumba);
    const auto sunken = lnf.token (theme::Token::sunken);
    const auto dark = lnf.getMode() == theme::Mode::dark;

    // `0 0 18px color-mix(in srgb, var(--c-zabumba) 12%, transparent)` — an
    // OUTER glow, so it is drawn first. Dark theme ONLY: css:209 gives the light
    // theme its own shadow with no outer glow at all, which is two rows and not
    // one row at a different alpha — theme::Shadows' lesson from 04-01, and
    // StepPad's again in 04-03.
    //
    // 18 px is taller than the 3 px this group leaves above and below itself in
    // a 72 px header, so the glow reaches past the bar. It is CLIPPED there, and
    // that is the one pixel change the 04-05 split made: while the header was
    // painted by Chassis, between its own region painters, the glow bled onto
    // eleven rows of the two 1 px inter-strip gutters and paintMatrix's --line
    // fill — alpha 0x17 — composited over it. 22 pixels, max channel delta 5,
    // dark theme only. Measured against a build of the committed tree and
    // agreed rather than accepted silently; see 04-05-PLAN.md AC-1.
    if (dark)
        juce::DropShadow (accent.withAlpha (ChassisLayout::kGlobalKnobsTintPct / 100.0f),
                          ChassisLayout::kGlobalKnobsGlowRadius, {})
            .drawForRectangle (g, group);

    // ── the ellipse, with its origin ABOVE the box ─────────────────────────
    //
    // `radial-gradient(120% 160% at 50% -30%, <zabumba at 12%>, --sunken)` —
    // css:204. Built exactly as StepPad::paintLit does: a CIRCULAR gradient of
    // radius ry handed to a juce::FillType carrying an x-scale of rx/ry, so the
    // stretch reaches the gradient and not the shape. Graphics::addTransform
    // would distort the group's rounded rectangle and its border with it.
    //
    // What differs from the pad: the origin is at -30% of the height, OUTSIDE
    // the box. Nothing about the technique changes — a gradient's centre is
    // just a point — but it is why the group reads as lit from above rather
    // than filled.
    const auto rx = ChassisLayout::kGlobalKnobsRadiusX * area.getWidth();
    const auto ry = ChassisLayout::kGlobalKnobsRadiusY * area.getHeight();

    const juce::Point<float> origin {
        area.getX() + ChassisLayout::kGlobalKnobsOriginX * area.getWidth(),
        area.getY() + ChassisLayout::kGlobalKnobsOriginY * area.getHeight(),
    };

    const auto tint = theme::mix (sunken, accent,
                                  theme::mixWeight (100.0f - ChassisLayout::kGlobalKnobsTintPct,
                                                    ChassisLayout::kGlobalKnobsTintPct));

    juce::ColourGradient gradient (tint, origin, sunken, origin.translated (0.0f, ry), true);

    juce::FillType fill (gradient);
    fill.transform = juce::AffineTransform::scale (rx / ry, 1.0f, origin.x, origin.y);

    g.setFillType (fill);
    g.fillRoundedRectangle (area, radius);
    g.setFillType (juce::FillType());

    // `inset 0 1px 3px rgba(0,0,0,0.4)`, and the light theme's own
    // `inset 0 1px 2px rgba(0,0,0,0.12)`.
    g.setColour (juce::Colour::fromFloatRGBA (0.0f, 0.0f, 0.0f,
                                              dark ? ChassisLayout::kGlobalKnobsInsetAlpha
                                                   : ChassisLayout::kGlobalKnobsInsetAlphaLight));
    g.fillRect (area.withHeight (1.0f).reduced (radius * 0.5f, 0.0f));

    // `border: 1px color-mix(in srgb, var(--c-zabumba) 25%, var(--line-strong))`.
    g.setColour (theme::mix (lnf.token (theme::Token::lineStrong), accent,
                             theme::mixWeight (100.0f - ChassisLayout::kGlobalKnobsBorderPct,
                                               ChassisLayout::kGlobalKnobsBorderPct)));
    g.drawRoundedRectangle (area.reduced (0.5f), radius, 1.0f);

    // The 1 x 42 divider between the two knobs (css:212).
    g.setColour (lnf.token (theme::Token::lineStrong));
    g.fillRect (headerLayout.knobDivider);
}

void HeaderBar::paintHeaderText (juce::Graphics& g, juce::Rectangle<int> clip) const
{
    const auto& h = headerLayout;

    const auto visible = [&clip] (juce::Rectangle<int> box) { return box.intersects (clip); };

    // The wordmark: `FORRÓ` + a `·` in the ACCENT + `BOX`, one run, so the dot
    // has to be drawn in three pieces rather than as one coloured string.
    if (visible (h.wordmark))
    {
        static const juce::String first { juce::CharPointer_UTF8 ("FORRÓ") };
        static const juce::String dot   { juce::CharPointer_UTF8 ("·") };
        static const juce::String last  { "BOX" };

        auto x = static_cast<float> (h.wordmark.getX());
        const auto row = h.wordmark.toFloat();

        const auto run = [&] (const juce::String& text, juce::Colour colour)
        {
            const auto width = type::trackedWidth (type::Style::wordmark, text);

            g.setColour (colour);
            type::drawTracked (g, type::Style::wordmark, text,
                               row.withX (x).withWidth (width),
                               juce::Justification::centredLeft);
            x += width;
        };

        run (first, lnf.token (theme::Token::fg));
        run (dot, theme::accent (theme::Accent::zabumba));
        run (last, lnf.token (theme::Token::fg));
    }

    // The two knob names, to the RIGHT of each dial rather than under it —
    // css:210-211, and the reason the header's knobs carry no micro-label.
    g.setColour (lnf.token (theme::Token::fgDim));

    if (visible (h.swingName))
        type::drawTracked (g, type::Style::globalKnobName, ChassisLayout::globalKnobNames()[0],
                           h.swingName.toFloat(), juce::Justification::centredLeft);

    if (visible (h.cachacaName))
        type::drawTracked (g, type::Style::globalKnobName, ChassisLayout::globalKnobNames()[1],
                           h.cachacaName.toFloat(), juce::Justification::centredLeft);

    // `STYLE`, the micro-label beside the segments.
    if (visible (h.styleLabel))
    {
        g.setColour (lnf.token (theme::Token::fgFaint));
        type::drawTracked (g, type::Style::styleLabel, "STYLE", h.styleLabel.toFloat(),
                           juce::Justification::centredLeft);
    }
}

} // namespace forrobox
