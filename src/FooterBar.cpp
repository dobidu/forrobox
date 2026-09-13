#include "FooterBar.h"

#include "PluginProcessor.h"

#include "Surface.h"

namespace forrobox
{

const juce::StringArray& outputModeLabels()
{
    // Built from `ids::outputModes`, which the PROCESSOR declares the parameter
    // from. The segments and the choice list were two literal tables that agreed
    // by inspection until this; now the toggle cannot label a segment something
    // the parameter does not have, and the index it lights is the index a saved
    // project holds.
    static const juce::StringArray labels = []
    {
        juce::StringArray out;

        for (const auto* mode : ids::outputModes)
            out.add (mode);

        return out;
    }();

    return labels;
}

FooterLayout FooterLayout::forBounds (juce::Rectangle<int> bounds) noexcept
{
    FooterLayout out;

    const auto centred = [&bounds] (juce::Rectangle<int> box)
    {
        return centredInRow (bounds, box);
    };

    auto row = bounds.reduced (footer::kPadX, 0);

    // ── every group's size, before anything is placed ──────────────────────
    //
    // The auto margins need all four widths at once, so nothing can be taken
    // from either end until every group has been measured.
    const auto labelHeight = type::boxHeight (type::Style::footerLabel);

    const auto masterLabelWidth = juce::roundToInt (
        type::trackedWidth (type::Style::footerLabel, "MASTER"));
    const auto outputLabelWidth = juce::roundToInt (
        type::trackedWidth (type::Style::footerLabel, "OUTPUT"));

    const auto limiterWidth = Button::widthOf (Button::Variant::base, "LIMITER");
    const auto limiterHeight = Button::heightOf (Button::Variant::base);

    const auto toggleWidth = Segmented::widthOf (outputModeLabels(), type::Style::outToggleLabel,
                                                  Segmented::Variant::outToggle);
    const auto toggleHeight = Segmented::heightOf (type::Style::outToggleLabel,
                                                   Segmented::Variant::outToggle);

    const auto drag = DragMidiButton::metrics();

    const auto masterWidth = masterLabelWidth + footer::kGroupGap + footer::kMasterFaderWidth;
    const auto limiterGroupWidth = limiterWidth + footer::kGroupGap + grmeter::kWidth;
    const auto outputWidth = outputLabelWidth + footer::kGroupGap + toggleWidth;

    // ── the three auto margins ─────────────────────────────────────────────
    //
    // `.drag-midi` is `margin: 0 auto` and the OUTPUT group is
    // `margin-left: auto`, so the row has THREE auto margins and flexbox splits
    // the free space equally between them (CSS Flexbox 9.5). DRAG MIDI is
    // therefore NOT centred in the row, which is what PLANNING.md:334 calls it:
    // an extra third of the free space sits between it and OUTPUT. The running
    // prototype is the authority where the two disagree, and this is the
    // prototype's own layout rather than a reading of the sentence.
    const auto content = masterWidth + limiterGroupWidth + drag.width + outputWidth;
    const auto gaps = footer::kGap * 3;
    const auto free = juce::jmax (0, row.getWidth() - content - gaps);
    const auto autoMargin = free / footer::kNumAutoMargins;

    const auto takeLeft = [&row] (int width) { return row.removeFromLeft (width); };

    // ── 1. MASTER ──────────────────────────────────────────────────────────
    {
        auto group = centred (takeLeft (masterWidth)
                                  .withHeight (flexRow (labelHeight, fader::kHeight)));

        out.masterLabel = centred (group.removeFromLeft (masterLabelWidth)
                                        .withHeight (labelHeight));
        group.removeFromLeft (footer::kGroupGap);
        out.masterFader = centred (group.withHeight (fader::kHeight));
    }

    row.removeFromLeft (footer::kGap);

    // ── 2. LIMITER and its meter ───────────────────────────────────────────
    out.limiterGroup = centred (takeLeft (limiterGroupWidth)
                                    .withHeight (flexRow (limiterHeight, grmeter::kHeight)));
    {
        auto group = out.limiterGroup;

        out.limiterButton = centred (group.removeFromLeft (limiterWidth)
                                          .withHeight (limiterHeight));
        group.removeFromLeft (footer::kGroupGap);
        out.grMeter = centred (group.removeFromLeft (grmeter::kWidth)
                                    .withHeight (grmeter::kHeight));
    }

    row.removeFromLeft (footer::kGap);

    // ── 3. DRAG MIDI, after its auto margin ────────────────────────────────
    row.removeFromLeft (autoMargin);
    out.dragMidi = centred (takeLeft (drag.width).withHeight (drag.height));

    // ── 4. OUTPUT: its own auto margin, DRAG MIDI's right one, and the gap ──
    row.removeFromLeft (autoMargin + footer::kGap + autoMargin);

    out.outputGroup = centred (takeLeft (outputWidth)
                                   .withHeight (flexRow (labelHeight, toggleHeight)));
    {
        auto group = out.outputGroup;

        out.outputLabel = centred (group.removeFromLeft (outputLabelWidth)
                                        .withHeight (labelHeight));
        group.removeFromLeft (footer::kGroupGap);
        out.outputToggle = centred (group.removeFromLeft (toggleWidth)
                                         .withHeight (toggleHeight));
    }

    return out;
}

FooterBar::FooterBar (ForroBoxLookAndFeel& lookAndFeelToUse)
    : lnf (lookAndFeelToUse)
{
    setOpaque (true);
}

FooterBar::~FooterBar() = default;

void FooterBar::attachParameters (juce::AudioProcessorValueTreeState& apvts)
{
    buildFooterControls (apvts);

    // The meter is the ONE thing here with nothing to hang off: gain reduction
    // is not a parameter, it is a measurement the audio thread publishes.
    polledProcessor = dynamic_cast<::ForroBoxAudioProcessor*> (&apvts.processor);

    if (polledProcessor != nullptr)
    {
        footerPoll.tick = [this] { refreshFromProcessor (kPollSeconds); };
        footerPoll.startTimerHz (kFooterPollHz);
    }

    resized();
}

void FooterBar::refreshFromProcessor (float seconds)
{
    if (footerControls.grMeter == nullptr || polledProcessor == nullptr)
        return;

    // THE SINGLE READER. `takeGainReductionDb` is an exchange over a per-block
    // maximum, so every read both reports the peak since the last one and
    // clears it — two readers would each see a fraction of the peaks and both
    // would be wrong. This is the reader; see the accessor on the processor.
    footerControls.grMeter->setReductionDb (polledProcessor->takeGainReductionDb(), seconds);
}

void FooterBar::buildFooterControls (juce::AudioProcessorValueTreeState& apvts)
{
    footerControls = {};

    // ── MASTER ─────────────────────────────────────────────────────────────
    //
    // `--fg`, not an accent: the master is the one fader in the plugin that
    // belongs to no channel (app.js:408).
    footerControls.master = std::make_unique<Fader> (lnf, lnf.token (theme::Token::fg));

    if (auto* masterParameter = dynamic_cast<juce::RangedAudioParameter*> (
                                    apvts.getParameter (ids::master)))
    {
        footerControls.masterAttachment =
            std::make_unique<ProportionAttachment<Fader>> (*masterParameter,
                                                           *footerControls.master);
        footerControls.masterAttachment->sendInitialUpdate();
    }

    // The squared taper `gain = (value/100)^2` is the MixBus's and is already
    // implemented there. The fader shows the parameter's own 0..100 — a fader
    // drawn at the taper's position would disagree with the number the host
    // automates.

    // ── LIMITER ────────────────────────────────────────────────────────────
    footerControls.limiter = std::make_unique<Button> (lnf, Button::Variant::base, "LIMITER");

    if (auto* limiterParameter = dynamic_cast<juce::RangedAudioParameter*> (
                                     apvts.getParameter (ids::limiterOn)))
        footerControls.limiterAttachment =
            std::make_unique<ToggleAttachment> (*limiterParameter, *footerControls.limiter);

    footerControls.grMeter = std::make_unique<GainReductionMeter> (lnf);

    // ── DRAG MIDI ──────────────────────────────────────────────────────────
    //
    // A STUB, like LOAD and the preset arrows: built, shown, hovered, pressed,
    // and wired to nothing. Phase 7 owns performExternalDragDropOfFiles and the
    // SMF writer, and the 2.6s idle pulse goes with them.
    footerControls.dragMidi = std::make_unique<DragMidiButton> (lnf);

    // ── OUTPUT ─────────────────────────────────────────────────────────────
    footerControls.output = std::make_unique<Segmented> (lnf, outputModeLabels(),
                                                          type::Style::outToggleLabel,
                                                          Segmented::Variant::outToggle);

    // The lit segment FOLLOWS the parameter, through an attachment — not a
    // single read at build time.
    //
    // That is what it was, and /code-review caught it against a comment of mine
    // in Segmented.h claiming the control "still MOVES when the parameter
    // moves". It did not: `output_mode` is real, automatable and persisted, so a
    // host automating it, a project reopening or a host-side undo all move it,
    // and the footer would have gone on lighting whatever was selected when the
    // editor opened. Read-only is about INPUT; the display half still needs a
    // listener.
    if (auto* outputParameter = dynamic_cast<juce::RangedAudioParameter*> (
                                    apvts.getParameter (ids::outputMode)))
        footerControls.outputAttachment =
            std::make_unique<ChoiceAttachment> (*outputParameter, *footerControls.output);

    // READ-ONLY until 04-06 implements the routing. The parameter is real,
    // automatable and persisted today — what does not exist yet is the five
    // extra stereo buses behind MULTI-OUT, so a click that lit it would be a
    // control that looked like it worked. Dimmed and with the pointing hand
    // withdrawn, rather than silently ignoring the click.
    //
    // Deliberately NOT given an onSegmentClicked. A read-only control with a
    // callback nobody can reach is a guarantee with no caller, which 02-04
    // ruled is not a guarantee.
    footerControls.output->setReadOnly (true);

    for (auto* child : std::initializer_list<juce::Component*> {
             footerControls.master.get(), footerControls.limiter.get(),
             footerControls.grMeter.get(), footerControls.dragMidi.get(),
             footerControls.output.get() })
        addAndMakeVisible (*child);
}

void FooterBar::resized()
{
    layout = FooterLayout::forBounds (getLocalBounds());

    if (footerControls.master == nullptr)
        return;   // attachParameters has not run; the bar is a surface

    // The fader ASKS for the bounds its reserved box needs, so the thumb can
    // hang past the track's ends into the footer's own padding.
    footerControls.master->setBounds (Fader::boundsForBox (layout.masterFader));
    footerControls.limiter->setBounds (layout.limiterButton);
    footerControls.grMeter->setBounds (layout.grMeter);

    // DRAG MIDI asks for its bounds: its hover glow falls outside the box, and
    // a Component's paint is clipped to its own.
    footerControls.dragMidi->setBounds (DragMidiButton::boundsForBox (layout.dragMidi));
    footerControls.output->setBounds (layout.outputToggle);
}

void FooterBar::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds();

    g.setColour (lnf.token (theme::Token::raised));
    g.fillRect (area);

    g.setColour (lnf.token (theme::Token::line));
    g.fillRect (area.getX(), area.getY(), area.getWidth(), 1);

    // `border-top: 1px solid var(--line)` AND `inset 0 1px 0 <highlight>` — two
    // different rows. Painting the highlight first and the border over it put
    // both on row 0, so the footer's highlight never rendered at all.
    surface::raisedHighlight (g, area.withTrimmedTop (1), lnf.shadows().raisedHighlight);

    paintFooterText (g, g.getClipBounds());
}

void FooterBar::paintFooterText (juce::Graphics& g, juce::Rectangle<int> clip) const
{
    // Per LABEL, and deliberately not one union over both.
    //
    // The GR meter is not opaque, so its 56x6 repaint propagates here with the
    // clip set to the meter — and text layout happens before any clipped drawing
    // rejects it. Ungated, that cost 12.11 us of the footer's 13.36 us paint on
    // every meter tick, 30 times a second while the limiter works. Measured by
    // /simplify, which also measured why the header's union approach must not be
    // copied: MASTER at x=18 and OUTPUT at x=1024 union to a box 1039 px wide
    // that CONTAINS the meter, so it would never reject.
    g.setColour (lnf.token (theme::Token::fgFaint));

    if (layout.masterLabel.intersects (clip))
        type::drawTracked (g, type::Style::footerLabel, "MASTER", layout.masterLabel.toFloat(),
                           juce::Justification::centredLeft);

    if (layout.outputLabel.intersects (clip))
        type::drawTracked (g, type::Style::footerLabel, "OUTPUT", layout.outputLabel.toFloat(),
                           juce::Justification::centredLeft);
}

} // namespace forrobox
