#include "Chassis.h"

#include "PluginProcessor.h"

#include "KnobAttachment.h"
#include "ValueTooltip.h"

namespace forrobox
{

juce::StringArray ChassisLayout::profileCodes()
{
    // From ids::profileInfos, which verify-profiles.py already cross-checks
    // against data.js on every build. Four three-letter strings are exactly the
    // kind of thing that gets retyped, and 02-01's lesson is that a second copy
    // agrees with itself rather than with the source.
    juce::StringArray codes;

    for (const auto& info : ids::profileInfos)
        codes.add (juce::String (juce::CharPointer_UTF8 (info.code)));

    return codes;
}

int ChassisLayout::indexOfProfile (juce::StringRef profileId)
{
    for (size_t i = 0; i < ids::profileInfos.size(); ++i)
        if (profileId == juce::StringRef (ids::profileInfos[i].id))
            return static_cast<int> (i);

    // An unknown id is the default's segment, not an unlit control: a saved
    // project naming a profile this build does not have should still show
    // something coherent.
    return 0;
}

const std::array<juce::String, 2>& ChassisLayout::globalKnobNames()
{
    // ONE table, read by headerInteriorOf to MEASURE the meta column and by
    // paintHeaderText to DRAW it. `CACHAÇA` used to exist three times — passed
    // into buildGlobalKnob and discarded with ignoreUnused, measured as its own
    // UTF-8 literal, and drawn as a third — and Button.h already records that a
    // mis-encoded literal renders as ink that is present and WRONG, which no
    // test here can catch. Found by /simplify.
    static const std::array<juce::String, 2> names {{
        juce::String (juce::CharPointer_UTF8 ("SWING")),
        juce::String (juce::CharPointer_UTF8 ("CACHA\xc3\x87" "A")),
    }};

    return names;
}

const juce::String& ChassisLayout::presetStubLabel()
{
    static const juce::String label { juce::CharPointer_UTF8 ("P\xc3\x89" "-DE-SERRA 01") };
    return label;
}

ChassisLayout::HeaderLayout ChassisLayout::headerInteriorOf (juce::Rectangle<int> header) noexcept
{
    HeaderLayout out;

    // `align-items: center` throughout, so every cluster is vertically centred
    // in the row rather than filling it. The row's height is the header's, and
    // each child takes its own — which is what `flexRow` means, and why nothing
    // below stretches anything to 72 px.
    const auto centred = [&header] (juce::Rectangle<int> box)
    {
        return box.withY (header.getY() + (header.getHeight() - box.getHeight()) / 2);
    };

    auto row = header.reduced (kHeaderPadX, 0);

    const auto takeLeft = [&row] (int width) { return row.removeFromLeft (width); };

    // ── 1. the logo lockup: mark + wordmark, gap 10 ────────────────────────
    out.logoMark = centred (takeLeft (logo::kWidth).withHeight (logo::kHeight));
    row.removeFromLeft (logo::kLockupGap);

    const auto wordmarkWidth = juce::roundToInt (
        type::trackedWidth (type::Style::wordmark,
                            juce::String (juce::CharPointer_UTF8 ("FORR\xc3\x93" "\xc2\xb7" "BOX"))));

    out.wordmark = centred (takeLeft (wordmarkWidth)
                                .withHeight (textBox (type::Style::wordmark)));
    row.removeFromLeft (kHeaderGap);

    // ── 2. the BPM cluster: field, SYNC, then div-2 / x2 ───────────────────
    const auto bpmHeight = ValueScreen::heightOf (type::Style::bpmReadout, bpmfield::kPadY);

    out.bpmField = centred (takeLeft (bpmfield::kMinWidth).withHeight (bpmHeight));
    row.removeFromLeft (bpmfield::kClusterGap);

    const auto syncWidth = Button::widthOf (Button::Variant::base, "SYNC");

    out.syncButton = centred (takeLeft (syncWidth)
                                  .withHeight (Button::heightOf (Button::Variant::base)));
    row.removeFromLeft (bpmfield::kClusterGap);

    const auto miniHeight = Button::heightOf (Button::Variant::mini);
    const auto miniWidth = [] (const char* label)
    {
        return Button::widthOf (Button::Variant::mini,
                                juce::String (juce::CharPointer_UTF8 (label)));
    };

    out.halfButton = centred (takeLeft (miniWidth ("\xc3\xb7" "2")).withHeight (miniHeight));
    row.removeFromLeft (bpmfield::kMiniGap);
    out.doubleButton = centred (takeLeft (miniWidth ("\xc3\x97" "2")).withHeight (miniHeight));
    row.removeFromLeft (kHeaderGap);

    // ── 3. transport ───────────────────────────────────────────────────────
    out.playButton = centred (takeLeft (Button::kTransportSize)
                                  .withHeight (Button::kTransportSize));
    row.removeFromLeft (kTransportGap);
    out.stopButton = centred (takeLeft (Button::kTransportSize)
                                  .withHeight (Button::kTransportSize));

    // ── 7. the right cluster, placed from the RIGHT edge ───────────────────
    //
    // Taken before the global knobs, because the group is `margin-left: auto`
    // between two flexible spacers — it is centred in what the two clusters
    // leave, so both ends have to be known first.
    // ASKED of the control, not restated here.
    const auto styleSegmentsWidth = Segmented::widthOf (profileCodes(),
                                                        type::Style::quickSwitchCode);
    const auto styleSegmentsHeight = Segmented::heightOf (type::Style::quickSwitchCode);

    out.styleSegments = centred (row.removeFromRight (styleSegmentsWidth)
                                     .withHeight (styleSegmentsHeight));
    row.removeFromRight (kStyleGap);

    const auto styleLabelWidth = juce::roundToInt (
        type::trackedWidth (type::Style::styleLabel, "STYLE"));

    out.styleLabel = centred (row.removeFromRight (styleLabelWidth)
                                  .withHeight (textBox (type::Style::styleLabel)));
    row.removeFromRight (kHeaderGap);

    const auto presetScreenHeight = ValueScreen::heightOf (type::Style::presetScreen,
                                                           kPresetScreenPadY);
    const auto arrowHeight = Button::heightOf (Button::Variant::arrow);

    out.presetNext = centred (row.removeFromRight (Button::kArrowWidth).withHeight (arrowHeight));
    row.removeFromRight (kPresetGap);
    out.presetScreen = centred (row.removeFromRight (kPresetScreenMinWidth)
                                    .withHeight (presetScreenHeight));
    row.removeFromRight (kPresetGap);
    out.presetPrev = centred (row.removeFromRight (Button::kArrowWidth).withHeight (arrowHeight));

    // ── 5. the global knob group, centred in what is left ──────────────────
    {
        const auto readHeight = ValueScreen::heightOf (type::Style::globalKnobReadout,
                                                       kGlobalKnobReadPadY);
        const auto nameHeight = textBox (type::Style::globalKnobName);

        const auto metaWidth = juce::jmax (kGlobalKnobReadMinWidth,
                                           juce::roundToInt (type::trackedWidth (
                                               type::Style::globalKnobName,
                                               globalKnobNames()[1])));

        // One knob and its meta: the dial, the gap, then the taller of the two
        // stacked rows' column.
        const auto knobUnitWidth = kGlobalKnobSize + kGlobalKnobMetaGap + metaWidth;
        const auto metaHeight = nameHeight + kGlobalKnobStackGap + readHeight;

        // The group is as tall as its TALLEST child, which is the 54 px dial or
        // the stacked meta — flexRow, not whichever someone wrote down.
        const auto contentHeight = flexRow (kGlobalKnobSize, metaHeight,
                                            kGlobalKnobDividerHeight);

        const auto groupWidth = kGlobalKnobsPadX * 2
                              + knobUnitWidth + kGlobalKnobsGap
                              + kGlobalKnobDividerWidth + kGlobalKnobsGap + knobUnitWidth;

        const auto groupHeight = contentHeight + kGlobalKnobsPadTop + kGlobalKnobsPadBottom;

        auto group = juce::Rectangle<int> (groupWidth, groupHeight)
                         .withCentre ({ row.getCentreX(), header.getCentreY() });

        out.globalKnobs = group;

        auto inner = group.reduced (kGlobalKnobsPadX, 0)
                         .withTrimmedTop (kGlobalKnobsPadTop)
                         .withTrimmedBottom (kGlobalKnobsPadBottom);

        const auto placeKnob = [&] (juce::Rectangle<int>& dial, juce::Rectangle<int>& name,
                                    juce::Rectangle<int>& read)
        {
            auto unit = inner.removeFromLeft (knobUnitWidth);

            dial = juce::Rectangle<int> (kGlobalKnobSize, kGlobalKnobSize)
                       .withCentre ({ unit.getX() + kGlobalKnobSize / 2, unit.getCentreY() });

            unit.removeFromLeft (kGlobalKnobSize + kGlobalKnobMetaGap);

            auto meta = unit.withSizeKeepingCentre (unit.getWidth(), metaHeight);

            name = meta.removeFromTop (nameHeight);
            meta.removeFromTop (kGlobalKnobStackGap);
            read = meta.removeFromTop (readHeight);
        };

        placeKnob (out.swingKnob, out.swingName, out.swingRead);
        inner.removeFromLeft (kGlobalKnobsGap);

        out.knobDivider = inner.removeFromLeft (kGlobalKnobDividerWidth)
                               .withSizeKeepingCentre (kGlobalKnobDividerWidth,
                                                       kGlobalKnobDividerHeight);

        inner.removeFromLeft (kGlobalKnobsGap);
        placeKnob (out.cachacaKnob, out.cachacaName, out.cachacaRead);
    }

    return out;
}

ChassisLayout ChassisLayout::forBounds (juce::Rectangle<int> bounds) noexcept
{
    ChassisLayout out;

    auto remaining = bounds;

    out.header    = remaining.removeFromTop (kHeaderHeight);
    out.headerLayout = headerInteriorOf (out.header);
    out.footer    = remaining.removeFromBottom (kFooterHeight);
    out.sequencer = remaining.removeFromBottom (kSequencerHeight);
    out.main      = remaining;                      // whatever is left: the 1fr row

    auto mainRow  = out.main;
    out.sidePanel = mainRow.removeFromRight (kSidePanelWidth);
    out.matrix    = mainRow;

    // Five equal columns with 1 px gaps. The width does not divide by five
    // evenly at the design size (609 px of matrix), so the columns are placed
    // from exact fractional edges and rounded — which distributes the remainder
    // instead of accumulating it in the last strip.
    const auto gaps       = static_cast<float> (kStripGap * (kNumStrips - 1));
    const auto stripWidth = (static_cast<float> (out.matrix.getWidth()) - gaps)
                          / static_cast<float> (kNumStrips);

    for (int i = 0; i < kNumStrips; ++i)
    {
        const auto left  = static_cast<float> (out.matrix.getX())
                         + static_cast<float> (i) * (stripWidth + static_cast<float> (kStripGap));
        const auto right = left + stripWidth;

        out.strips[static_cast<size_t> (i)] =
            juce::Rectangle<int>::leftTopRightBottom (juce::roundToInt (left),
                                                      out.matrix.getY(),
                                                      juce::roundToInt (right),
                                                      out.matrix.getBottom());
    }

    for (int i = 0; i < kNumStrips; ++i)
        out.stripLayouts[static_cast<size_t> (i)] =
            stripInteriorOf (out.strips[static_cast<size_t> (i)], i);

    return out;
}

ChassisLayout::StripLayout ChassisLayout::stripInteriorOf (juce::Rectangle<int> strip,
                                                           int channelIndex) noexcept
{
    // The single derivation of a strip's interior. paintStrip reads this rather
    // than re-running the removeFromTop chain, and so do the tests — the
    // stacking order lives in one place or it lives in four.
    StripLayout out;

    auto content = strip.reduced (kStripPadSide, 0)
                       .withTrimmedTop (kStripPadTop)
                       .withTrimmedBottom (kStripPadBottom);

    out.headRow = content.removeFromTop (kHeadRowHeight);

    content.removeFromTop (kAccentBarMarginTop);
    out.accentBar = content.removeFromTop (kAccentBarHeight);
    content.removeFromTop (kAccentBarMarginBottom);

    // Everything below the accent bar. Kept whole so the tiling assertion has
    // one rectangle to sum the stack against.
    out.controls = content;

    // ── the stack, PLANNING.md:276-294 in order ────────────────────────────
    //
    // One cursor walked downward, so the order here IS the spec's order and a
    // reordered pair is a moved box rather than two numbers that still add up.
    content.removeFromTop (kSampleSlotMarginTop);
    out.sampleSlot = content.removeFromTop (kSampleSlotHeight);

    content.removeFromTop (kHitVisualiserMarginTop);
    out.hitVisualiser = content.removeFromTop (kHitVisualiserHeight);

    content.removeFromTop (kStripDividerMargin);
    out.dividerTop = content.removeFromTop (kStripDividerHeight);
    content.removeFromTop (kStripDividerMargin);

    out.knobGrid = content.removeFromTop (kKnobGridHeight);

    content.removeFromTop (kStripDividerMargin);
    out.dividerBottom = content.removeFromTop (kStripDividerHeight);
    content.removeFromTop (kStripDividerMargin);

    content.removeFromTop (kPatternRowMarginTop);
    out.patternCycler = content.removeFromTop (kPatternRowHeight);

    content.removeFromTop (kMuteSoloMarginTop);
    out.muteSolo = content.removeFromTop (kMuteSoloHeight);

    content.removeFromTop (kGhostRowMarginTop);
    out.ghostLabel = content.removeFromTop (kGhostLabelHeight);
    content.removeFromTop (kGhostLabelGap);
    out.ghostFader = content.removeFromTop (kFaderHeight);

    // `.subdots` exists only on the channel with sub-lanes — bateria (app.js
    // gates it on `inst.subs`). Identified by its ACCENT rather than the
    // literal index 4: that binding is the one the static_assert at the top of
    // Chassis.h protects, so it cannot drift from the channel table.
    //
    // Walked off the SAME cursor as every other box rather than built by
    // arithmetic. It was `withY().withHeight()`, which made it the one box
    // removeFromTop's clamping could not contain — and the only one the
    // containment loop in the tests skipped.
    if (static_cast<theme::Accent> (channelIndex) == theme::Accent::bateria)
    {
        content.removeFromTop (kSubDotsMarginTop);
        out.subDots = content.removeFromTop (kSubDotsRowHeight);
    }

    // ── the knob cells: row-major across two columns ───────────────────────
    //
    // VOL / PITCH / DECAY / PAN, the order app.js:180-183 instantiates them in.
    // Derived here rather than at the call site so the gaps exist once.
    {
        const auto cellWidth = (out.knobGrid.getWidth() - kKnobGridColGap) / kKnobGridCols;

        for (int i = 0; i < static_cast<int> (out.knobCells.size()); ++i)
        {
            const auto row = i / kKnobGridCols;
            const auto col = i % kKnobGridCols;

            // `justify-items: center` (css:326) centres each knob in its cell,
            // so the cell is the full column width and the dial centres inside.
            out.knobCells[static_cast<size_t> (i)] = juce::Rectangle<int> (
                out.knobGrid.getX() + col * (cellWidth + kKnobGridColGap),
                out.knobGrid.getY() + row * (kKnobCellHeight + kKnobGridRowGap),
                cellWidth,
                kKnobCellHeight);
        }
    }

    return out;
}


Chassis::Chassis (ForroBoxLookAndFeel& lookAndFeelToUse)
    : lnf (lookAndFeelToUse)
{
    setOpaque (true);
    setSize (ChassisLayout::kWidth, ChassisLayout::kHeight);
}

Chassis::~Chassis() = default;

const std::array<juce::String, static_cast<size_t> (ChassisLayout::kNumStrips)>&
ChassisLayout::sampleNames()
{
    static const std::array<juce::String, static_cast<size_t> (kNumStrips)> names {{
        juce::String (juce::CharPointer_UTF8 ("Couro Aberto")),
        juce::String (juce::CharPointer_UTF8 ("A\xc3\xa7" "o Aberto")),        // Aço Aberto
        juce::String (juce::CharPointer_UTF8 ("Pandeiro M\xc3\xa9" "dio")),    // Pandeiro Médio
        juce::String (juce::CharPointer_UTF8 ("Ganz\xc3\xa1" " Seco")),        // Ganzá Seco
        juce::String (juce::CharPointer_UTF8 ("Kit Minimal")),
    }};

    return names;
}

const juce::String& ChassisLayout::patternScreenText()
{
    static const juce::String text { juce::CharPointer_UTF8 ("PAT 01") };
    return text;
}

const juce::String& ChassisLayout::subDotsLabel()
{
    static const juce::String text { juce::CharPointer_UTF8 (
        "BB \xc2\xb7" " CX \xc2\xb7" " HH \xc2\xb7" " TOM \xe2\x86\x97") };
    return text;
}

const juce::String& ChassisLayout::arrowPrev()
{
    static const juce::String text { juce::CharPointer_UTF8 ("\xe2\x80\xb9") };
    return text;
}

const juce::String& ChassisLayout::arrowNext()
{
    static const juce::String text { juce::CharPointer_UTF8 ("\xe2\x80\xba") };
    return text;
}

namespace
{
/** One channel parameter, or nullptr with an assertion.

    A missing parameter is a wiring error, not a case to paper over: a control
    bound to nothing would look right and do nothing. Shared by the knobs and
    by the three controls below them so the assertion is written once. */
juce::RangedAudioParameter* rangedParameter (juce::AudioProcessorValueTreeState& apvts,
                                             const char* channelId, const char* parameterId)
{
    auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (
                          apvts.getParameter (ids::channelParam (channelId, parameterId)));

    jassert (parameter != nullptr);

    return parameter;
}
} // namespace

void Chassis::attachParameters (juce::AudioProcessorValueTreeState& apvts, ValueTooltip* tooltip)
{
    // Idempotent. Appending would let a second call index stripLayouts past its
    // five entries, and a second set of children would stack invisibly on the
    // first — each attachment clears its own callbacks as it goes.
    stripKnobs.clear();

    for (auto& controls : stripControls)
    {
        // Safe to clear in one statement, and NOT because of declaration order.
        //
        // An implicitly-defined move-assignment assigns members in DECLARATION
        // order, unlike destruction, which runs in reverse — so this frees each
        // Button and Fader while its attachment still holds a reference, and
        // the attachment's destructor then runs. There is no ordering that is
        // safe on both paths, which is why this used to reset three attachments
        // by hand first: a rule the owner had to remember, and a fourth
        // attachment would have broken it silently.
        //
        // The attachments hold their controls through
        // juce::Component::SafePointer instead, so a control that died first is
        // simply gone rather than written to. Verified by removing this
        // comment's predecessor under AddressSanitizer: clean either way now,
        // where before it reported twenty heap-use-after-frees per call.
        controls = {};
    }

    for (int channel = 0; channel < ChassisLayout::kNumStrips; ++channel)
    {
        const auto& info = ids::channelInfos[static_cast<size_t> (channel)];

        // The same accent binding paintStrip uses, and the one the static_assert
        // at the top of this header protects.
        const auto colour = theme::accent (static_cast<theme::Accent> (channel));

        for (const auto& spec : ChassisLayout::knobSlots)
        {
            auto* parameter = rangedParameter (apvts, info.id, spec.param);

            if (parameter == nullptr)
                continue;

            auto knobComponent = std::make_unique<Knob> (lnf, ChassisLayout::kStripKnobSize,
                                                         spec.polarity, colour, spec.label);
            knobComponent->setTooltip (tooltip);

            auto knobAttachment = std::make_unique<KnobAttachment> (*parameter, *knobComponent);

            addAndMakeVisible (*knobComponent);

            stripKnobs.push_back ({ std::move (knobComponent), std::move (knobAttachment),
                                    channel, static_cast<int> (&spec - ChassisLayout::knobSlots.data()) });
        }

        auto& controls = stripControls[static_cast<size_t> (channel)];

        // ── the three stubs ────────────────────────────────────────────────
        //
        // Built, shown and left unwired. No onClick, no attachment, no state:
        // clicking LOAD or an arrow visibly presses and changes nothing, which
        // is what a v0.1 stub should look like to a reviewer.
        controls.load = std::make_unique<Button> (lnf, Button::Variant::load, "LOAD");
        controls.patternPrev = std::make_unique<Button> (lnf, Button::Variant::arrow,
                                                         ChassisLayout::arrowPrev());
        controls.patternNext = std::make_unique<Button> (lnf, Button::Variant::arrow,
                                                         ChassisLayout::arrowNext());

        // ── mute and solo ──────────────────────────────────────────────────
        controls.mute = std::make_unique<Button> (lnf, Button::Variant::muteSolo, "M",
                                                  Button::OnStyle::mute);
        controls.solo = std::make_unique<Button> (lnf, Button::Variant::muteSolo, "S",
                                                  Button::OnStyle::solo);

        if (auto* muteParameter = rangedParameter (apvts, info.id, ids::mute))
            controls.muteAttachment = std::make_unique<ToggleAttachment> (*muteParameter,
                                                                         *controls.mute);

        if (auto* soloParameter = rangedParameter (apvts, info.id, ids::solo))
            controls.soloAttachment = std::make_unique<ToggleAttachment> (*soloParameter,
                                                                          *controls.solo);

        // ── the ghost fader, and the readout that hangs off it ─────────────
        controls.ghost = std::make_unique<Fader> (lnf, colour);

        if (auto* ghostParameter = rangedParameter (apvts, info.id, ids::ghost))
        {
            // ONE listener on the parameter, and NO cached copy of its text.
            //
            // The readout used to be a `ghostText` field this lambda wrote.
            // That is a second representation of a value the parameter already
            // holds, and it produced exactly the bug a second representation
            // produces: `setProportion` fires this only when the proportion
            // CHANGES and a Fader starts at 0, so a project saved with GHOST at
            // 0% reopened with no percentage beside the fader — and
            // paintGhostLabel describes an empty readout as the honest unwired
            // state, which made it invisible. It needed a /code-review finding,
            // an explicit priming call and a dedicated test to close.
            //
            // paintGhostLabel asks the parameter instead. The callback's only
            // job is to say WHEN, and it repaints the readout's own box rather
            // than the strip — five knobs and a fader redrawn to change six
            // characters. Found by /simplify.
            controls.ghost->onProportionChanged =
                [this, channel] (float)
                {
                    repaint (layout.stripLayouts[static_cast<size_t> (channel)].ghostLabel);
                };

            controls.ghostAttachment =
                std::make_unique<ProportionAttachment<Fader>> (*ghostParameter, *controls.ghost);

            controls.ghostAttachment->sendInitialUpdate();
        }

        for (auto* child : { controls.load.get(), controls.patternPrev.get(),
                             controls.patternNext.get(), controls.mute.get(),
                             controls.solo.get() })
            addAndMakeVisible (*child);

        addAndMakeVisible (*controls.ghost);
    }

    buildHeaderControls (apvts);

    // The two things with no attachment to hang them on: `playing` is an atomic
    // outside the parameter surface, and the host's tempo is published from
    // processBlock. The two global readouts ride along, for the reason recorded
    // in buildGlobalKnob.
    polledProcessor = dynamic_cast<::ForroBoxAudioProcessor*> (&apvts.processor);
    polledApvts = &apvts;

    if (polledProcessor != nullptr)
    {
        headerPoll.tick = [this] { refreshHeaderFromProcessor(); };
        headerPoll.startTimerHz (kHeaderPollHz);
        refreshHeaderFromProcessor();
    }

    resized();
}

void Chassis::refreshHeaderFromProcessor()
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

    // The two global readouts are NOT polled: they hang off the knob's own
    // onProportionChanged, so this tick is exactly the things with no listener
    // to hang off — the transport's atomic and the host's tempo.
}

void Chassis::buildHeaderControls (juce::AudioProcessorValueTreeState& apvts)
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
                                            juce::String (juce::CharPointer_UTF8 ("\xc3\xb7" "2")));
    header.half->onClick = scaleBpm (0.5f);

    header.doubleUp = std::make_unique<Button> (lnf, Button::Variant::mini,
                                                juce::String (juce::CharPointer_UTF8 ("\xc3\x97" "2")));
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
                                                type::Style::quickSwitchCode);

    // The lit segment is the PERSISTED profile, asked of the state rather than
    // stored again here — the same rule the ghost readout ended up with. A
    // `selectedProfile` field on Chassis would be a second copy of something
    // the state already holds.
    //
    // And clicking does NOTHING. The full reload — bpm, swing, cachaça, all
    // five grids, the bateria sub-patterns, mutes and timbre — is Phase 6's
    // headline deliverable, and taking it here would move a phase's work into a
    // UI plan. An honest stub, like LOAD and the PAT cycler: it draws, it
    // hovers, and it changes nothing.
    if (auto* processor = dynamic_cast<::ForroBoxAudioProcessor*> (&apvts.processor))
        header.style->setSelectedIndex (ChassisLayout::indexOfProfile (
            processor->lockPatternState()->activeProfile));

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

void Chassis::resized()
{
    layout = ChassisLayout::forBounds (getLocalBounds());

    // Each knob into the cell it RECORDED, not one derived from its position in
    // the vector. The dial is centred in its cell (`justify-items: center`,
    // css:326) and the cell already includes the micro-label row.
    for (const auto& placed : stripKnobs)
    {
        const auto cell = layout.stripLayouts[static_cast<size_t> (placed.channel)]
                              .knobCells[static_cast<size_t> (placed.slot)];

        // The cell's own height, not a second spelling of the expression that
        // DEFINES it — kKnobCellHeight exists precisely because
        // `Knob::preferredHeight (kStripKnobSize, true)` had been written twice.
        placed.knob->setBounds (cell.withSizeKeepingCentre (ChassisLayout::kStripKnobSize,
                                                            cell.getHeight()));
    }

    for (int channel = 0; channel < ChassisLayout::kNumStrips; ++channel)
    {
        auto& controls = stripControls[static_cast<size_t> (channel)];

        if (controls.load == nullptr)
            continue;   // attachParameters has not run; the chassis is a surface

        const auto& interior = layout.stripLayouts[static_cast<size_t> (channel)];

        // ── sample slot: `display:flex; gap:7px` — name then LOAD ──────────
        //
        // The button is placed first and takes its own preferred width; the
        // name gets what is left, which is `flex: 1` (css:291). Its vertical
        // centring is the row's `align-items: center`, and the button is the
        // tallest child so it defines the row.
        {
            auto slot = interior.sampleSlot;
            const auto loadWidth = controls.load->preferredWidth();

            controls.load->setBounds (slot.removeFromRight (loadWidth)
                                          .withSizeKeepingCentre (loadWidth,
                                                                  controls.load->preferredHeight()));
        }

        // ── pattern cycler: arrow, screen, arrow ───────────────────────────
        //
        // The two arrows are fixed at 22x26 (css:231) and the screen is
        // `flex: 1`. Both arrows are vertically centred in the row rather than
        // filling it, which is what `align-items: center` does and what makes
        // the row's height the taller of the two children.
        {
            auto row = interior.patternCycler;

            controls.patternPrev->setBounds (
                row.removeFromLeft (Button::kArrowWidth)
                   .withSizeKeepingCentre (Button::kArrowWidth, Button::kArrowHeight));

            controls.patternNext->setBounds (
                row.removeFromRight (Button::kArrowWidth)
                   .withSizeKeepingCentre (Button::kArrowWidth, Button::kArrowHeight));
        }

        // ── mute / solo: two `flex: 1` buttons with a 5 px gap ─────────────
        //
        // The gap comes out of the middle and the halves take the rest, so an
        // odd width gives one button the extra pixel rather than leaving a
        // one-pixel seam at the right edge.
        {
            auto row = interior.muteSolo;
            const auto gap = ChassisLayout::kMuteSoloGap;
            const auto half = (row.getWidth() - gap) / 2;
            const auto height = controls.mute->preferredHeight();

            // Sized by the BUTTON, centred in the row — not stretched to the
            // row. Stretching made the AC-5 containment check unable to fail
            // for these two, because the child's bounds WERE the box; now the
            // row's height and the button's come from different places and the
            // check compares them. They are equal today.
            controls.mute->setBounds (row.removeFromLeft (half)
                                         .withSizeKeepingCentre (half, height));
            row.removeFromLeft (gap);
            controls.solo->setBounds (row.withSizeKeepingCentre (row.getWidth(), height));
        }

        // ── the ghost fader ────────────────────────────────────────────────
        //
        // Asks the fader for the bounds its reserved box needs, exactly as the
        // knob cell asks the knob for its height. The thumb then hangs into the
        // strip's 11 px side padding, which is where it hangs in the browser.
        controls.ghost->setBounds (Fader::boundsForBox (interior.ghostFader));
    }

    if (headerControls.logo != nullptr)
    {
        const auto& h = layout.headerLayout;
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
}

void Chassis::paint (juce::Graphics& g)
{
    // The chassis ground. Every region paints over it, so a region that fails
    // to paint shows as --bg rather than as whatever was in the buffer.
    g.fillAll (lnf.token (theme::Token::bg));

    // Only the regions the clip actually touches.
    //
    // Without this, a partial repaint costs a full one: the ghost readout's
    // `repaint (ghostLabel)` was carefully scoped to 161x10 px and then laid
    // out all 31 of the chassis's glyph arrangements anyway, because text
    // layout happens BEFORE any clipped drawing rejects it. Measured by
    // /simplify: 291 us to change six characters during a fader drag, against
    // 1794 us for the whole chassis — 16% of a full repaint for 0.17% of the
    // area. Skipping the regions outside the clip takes it to ~15 us.
    const auto clip = g.getClipBounds();

    const auto paintIfVisible = [&clip] (juce::Rectangle<int> area, auto&& painter)
    {
        if (area.intersects (clip))
            painter();
    };

    paintIfVisible (layout.header,    [&] { paintHeader (g, layout.header); });
    paintIfVisible (layout.matrix,    [&] { paintMatrix (g, layout.matrix); });
    paintIfVisible (layout.sidePanel, [&] { paintSidePanel (g, layout.sidePanel); });
    paintIfVisible (layout.sequencer, [&] { paintSequencer (g, layout.sequencer); });
    paintIfVisible (layout.footer,    [&] { paintFooter (g, layout.footer); });
}

void Chassis::paintRaisedHighlight (juce::Graphics& g, juce::Rectangle<int> area,
                                    juce::Colour highlight) const
{
    // The colour is a parameter, not read from one field here: the header and
    // the side/footer share this MECHANISM but not its value (see
    // theme::Shadows). Reaching for a single field is what shipped the light
    // header at 0.50 where the stylesheet says 0.05.
    g.setColour (highlight);
    g.fillRect (area.getX(), area.getY(), area.getWidth(), 1);
}

void Chassis::paintWellShadow (juce::Graphics& g, juce::Rectangle<int> area,
                               juce::Colour shadow, float depth) const
{
    const auto height = juce::jmin (static_cast<float> (area.getHeight()), depth * 3.0f);

    g.setGradientFill (juce::ColourGradient::vertical (shadow, static_cast<float> (area.getY()),
                                                       shadow.withAlpha (0.0f),
                                                       static_cast<float> (area.getY()) + height));
    g.fillRect (area.withHeight (juce::roundToInt (height)));
}

void Chassis::paintHeader (juce::Graphics& g, juce::Rectangle<int> area) const
{
    // `linear-gradient(180deg, <raised + 3% white>, <raised>)` plus the top
    // highlight and a 1 px bottom border.
    const auto raised = lnf.token (theme::Token::raised);

    g.setGradientFill (juce::ColourGradient::vertical (
        theme::mix (raised, juce::Colours::white, ChassisLayout::kHeaderGradientWeight), static_cast<float> (area.getY()),
        raised, static_cast<float> (area.getBottom())));
    g.fillRect (area);

    paintRaisedHighlight (g, area, lnf.shadows().headerHighlight);

    g.setColour (lnf.token (theme::Token::line));
    g.fillRect (area.getX(), area.getBottom() - 1, area.getWidth(), 1);

    // The header's own content: the recessed group behind the two knobs, and
    // the text that is not a component. Everything else there paints itself as
    // a child, which is what gives it hover and press for free.
    //
    // Clip-checked, like the regions above. Chassis::paint carries the clip one
    // level and these two were gated only on "has attachParameters run", so a
    // repaint touching ANY header pixel paid for the group's 18 px DropShadow,
    // its radial gradient and five tracked text runs. Measured: a 1x1 px header
    // repaint cost 55 us where the entire footer row costs 30. Found by
    // /simplify.
    if (headerControls.logo != nullptr)
    {
        const auto clip = g.getClipBounds();
        const auto& h = layout.headerLayout;

        if (h.globalKnobs.intersects (clip))
            paintGlobalKnobGroup (g);

        // One box over every run paintHeaderText draws, so the text is skipped
        // as a group rather than per string.
        const auto textBounds = h.wordmark.getUnion (h.swingName)
                                          .getUnion (h.cachacaName)
                                          .getUnion (h.styleLabel);

        if (textBounds.intersects (clip))
            paintHeaderText (g);
    }
}

void Chassis::paintGlobalKnobGroup (juce::Graphics& g) const
{
    const auto group = layout.headerLayout.globalKnobs;

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
    g.fillRect (layout.headerLayout.knobDivider);
}

void Chassis::paintHeaderText (juce::Graphics& g) const
{
    const auto& h = layout.headerLayout;

    // The wordmark: `FORRÓ` + a `·` in the ACCENT + `BOX`, one run, so the dot
    // has to be drawn in three pieces rather than as one coloured string.
    {
        static const juce::String first { juce::CharPointer_UTF8 ("FORR\xc3\x93") };
        static const juce::String dot   { juce::CharPointer_UTF8 ("\xc2\xb7") };
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

    type::drawTracked (g, type::Style::globalKnobName, ChassisLayout::globalKnobNames()[0],
                       h.swingName.toFloat(), juce::Justification::centredLeft);
    type::drawTracked (g, type::Style::globalKnobName, ChassisLayout::globalKnobNames()[1],
                       h.cachacaName.toFloat(), juce::Justification::centredLeft);

    // `STYLE`, the micro-label beside the segments.
    g.setColour (lnf.token (theme::Token::fgFaint));
    type::drawTracked (g, type::Style::styleLabel, "STYLE", h.styleLabel.toFloat(),
                       juce::Justification::centredLeft);
}

void Chassis::paintMatrix (juce::Graphics& g, juce::Rectangle<int> area) const
{
    // The 1 px gaps BETWEEN the strips are the dividers — nothing draws a
    // border, because two adjacent borders would give a 2 px seam.
    //
    // Only the gaps are filled, not the whole matrix. Filling the matrix and
    // letting the strips overpaint it is the same picture (verified: 0 of
    // 936,000 pixels differ, both themes) and 158x the work — 419,520 px
    // touched to reveal 1,824, of which the strips immediately cover 417,696.
    // Measured 548.7 us against 3.46 us, on a 1,516 us full repaint; at 2x the
    // wasted fill alone is ~2.2 ms.
    g.setColour (lnf.token (theme::Token::line));

    // Every column of the matrix no strip covers, so this is the old whole-area
    // fill's result by construction and not by an argument about float
    // rounding: the strips span the full matrix height, and the edges walked
    // here are matrix.getX() -> strip[0], each gap, and strip[last] ->
    // matrix.getRight(). The two outer slivers are empty at every size tested
    // and fill nothing when they are; they exist so that a strip derivation
    // whose rounding does NOT reach the matrix edge still paints --line there
    // rather than leaving --bg showing through.
    auto edge = area.getX();

    for (int i = 0; i < ChassisLayout::kNumStrips; ++i)
    {
        const auto& strip = layout.strips[static_cast<size_t> (i)];

        g.fillRect (juce::Rectangle<int>::leftTopRightBottom (
            edge, area.getY(), strip.getX(), area.getBottom()));

        edge = strip.getRight();
    }

    g.fillRect (juce::Rectangle<int>::leftTopRightBottom (
        edge, area.getY(), area.getRight(), area.getBottom()));

    for (int i = 0; i < ChassisLayout::kNumStrips; ++i)
        // Per strip, for the same reason: five strips is five of everything
        // below, and a ghost readout's repaint touches exactly one of them.
        if (layout.strips[static_cast<size_t> (i)].intersects (g.getClipBounds()))
            paintStrip (g, layout.strips[static_cast<size_t> (i)], i);
}

void Chassis::paintStrip (juce::Graphics& g, juce::Rectangle<int> area, int channelIndex) const
{
    const auto& interior = layout.stripLayouts[static_cast<size_t> (channelIndex)];
    const auto& info  = ids::channelInfos[static_cast<size_t> (channelIndex)];
    const auto  which = static_cast<theme::Accent> (channelIndex);
    const auto  colour = theme::accent (which);

    // The zabumba strip carries `.anchor`: 88% panel, 12% accent. Barely
    // perceptible on purpose — the brief forbids decoration, and this is the one
    // tint that survives that rule because it marks the anchor instrument.
    const auto panel = lnf.token (theme::Token::panel);
    const auto isAnchor = (which == theme::Accent::zabumba);

    g.setColour (isAnchor ? theme::mix (panel, colour, ChassisLayout::kAnchorAccentWeight) : panel);
    g.fillRect (area);

    // Head row: instrument name left, index right. The names come from
    // ids::channelInfos, never re-typed here.
    const auto headRow = interior.headRow.toFloat();

    g.setColour (lnf.token (theme::Token::fg));
    type::drawTracked (g, type::Style::stripInstrumentName, info.displayName,
                       headRow, juce::Justification::centredLeft);

    g.setColour (lnf.token (theme::Token::fgDim));
    type::drawTracked (g, type::Style::stripIndex,
                       juce::String (channelIndex + 1).paddedLeft ('0', 2),
                       headRow, juce::Justification::centredRight);

    // Accent bar: 4 px, radius 1, with the glow at accent-intensity x 35%.
    const auto bar = interior.accentBar.toFloat();

    // `box-shadow: 0 0 10px <colour at accent-intensity x 35%>`.
    //
    // juce::DropShadow, not a stack of expanded rectangles at low alpha. That
    // was the first attempt and it renders a HARD-EDGED rectangular frame
    // around each bar rather than a glow — clearly visible in the light theme,
    // where it read as a grey box. DropShadow does a real blur, and with a zero
    // offset it is exactly the CSS's centred glow.
    const auto glow = colour.withAlpha (lnf.accentIntensity() * ChassisLayout::kAccentGlowOpacity);

    juce::DropShadow (glow, ChassisLayout::kAccentGlowRadius, {}).drawForRectangle (g, bar.toNearestInt());

    // `--accent-i` applies to this bar TWICE — the glow alpha above and the
    // fill's saturation here (css:285). Both are the identity at the default
    // intensity of 1.0; only one of them used to exist.
    g.setColour (theme::accentFill (colour, lnf.accentIntensity()));
    g.fillRoundedRectangle (bar, 1.0f);

    // The two 1 px dividers that bracket the knob grid (css:321). 04-02 paints
    // these and the knob grid; every other reserved box stays empty until the
    // plan that owns it arrives.
    g.setColour (lnf.token (theme::Token::line));
    g.fillRect (interior.dividerTop);
    g.fillRect (interior.dividerBottom);

    // The four boxes whose content is TEXT or plain shapes. The six that are
    // components — LOAD, both arrows, M, S and the fader — paint themselves as
    // children, which is what gives them hover and press without this method
    // knowing anything about either.
    paintSampleSlot (g, interior, channelIndex);
    paintPatternCycler (g, interior);
    paintGhostLabel (g, interior, channelIndex);

    if (! interior.subDots.isEmpty())
        paintSubDots (g, interior);

    // interior.hitVisualiser stays empty: it is Phase 5's activity meter and
    // has nothing to show until there are triggers to show. Reserved in
    // ChassisLayout and asserted there, so it is reachable without being drawn
    // — the same rule that kept `controls` undrawn through 04-01 and 04-02.
}

void Chassis::paintSampleSlot (juce::Graphics& g, const ChassisLayout::StripLayout& interior,
                               int channelIndex) const
{
    const auto& controls = stripControls[static_cast<size_t> (channelIndex)];

    // `flex: 1; text-overflow: ellipsis` — whatever the LOAD button leaves,
    // minus the 7 px gap. When the button has not been built the name gets the
    // whole box, which is what a chassis with no processor should show.
    auto available = interior.sampleSlot;

    if (controls.load != nullptr)
        available = available.withTrimmedRight (controls.load->getWidth()
                                                + ChassisLayout::kSampleSlotGap);

    const auto& name = ChassisLayout::sampleNames()[static_cast<size_t> (channelIndex)];

    g.setColour (lnf.token (theme::Token::fg));
    type::drawTracked (g, type::Style::sampleName,
                       type::ellipsised (type::Style::sampleName, name,
                                         static_cast<float> (available.getWidth())),
                       available.toFloat(), juce::Justification::centredLeft);
}

void Chassis::paintPatternCycler (juce::Graphics& g,
                                  const ChassisLayout::StripLayout& interior) const
{
    // The screen, between the two arrow buttons. `flex: 1`, so it is the row
    // minus both arrows and both gaps — derived from the button's own width,
    // not from a third copy of 22.
    const auto inset = Button::kArrowWidth + ChassisLayout::kPatternRowGap;

    const auto screen = interior.patternCycler.reduced (inset, 0)
                            .withSizeKeepingCentre (interior.patternCycler.getWidth() - inset * 2,
                                                    ChassisLayout::kPatternScreenHeight);

    const auto radius = lnf.cornerRadius();

    g.setColour (lnf.token (theme::Token::screen));
    g.fillRoundedRectangle (screen.toFloat(), radius);

    g.setColour (lnf.token (theme::Token::line));
    g.drawRoundedRectangle (screen.toFloat().reduced (0.5f), radius, 1.0f);

    g.setColour (lnf.token (theme::Token::screenFg));
    type::drawTracked (g, type::Style::patternScreen, ChassisLayout::patternScreenText(),
                       screen.toFloat(), juce::Justification::centred);
}

void Chassis::paintGhostLabel (juce::Graphics& g, const ChassisLayout::StripLayout& interior,
                               int channelIndex) const
{
    // `justify-content: space-between` — the caption left, the readout right.
    const auto row = interior.ghostLabel.toFloat();

    g.setColour (lnf.token (theme::Token::fgFaint));
    type::drawTracked (g, type::Style::stripMicroLabel, "Ghost Prob", row,
                       juce::Justification::centredLeft);

    // Asked of the parameter, not of a cached copy. Empty on a chassis with no
    // processor, which is honest: there is no parameter to ask.
    const auto& controls = stripControls[static_cast<size_t> (channelIndex)];

    if (controls.ghostAttachment == nullptr)
        return;

    g.setColour (lnf.token (theme::Token::fgDim));
    type::drawTracked (g, type::Style::ghostValue,
                       controls.ghostAttachment->getParameter().getCurrentValueAsText(), row,
                       juce::Justification::centredRight);
}

void Chassis::paintSubDots (juce::Graphics& g, const ChassisLayout::StripLayout& interior) const
{
    // Four 8 px circles in the bateria piece colours, then the label, inset by
    // its own margin — css:351-354. A STUB: nothing opens the kit.
    auto row = interior.subDots;

    for (int i = 0; i < ChassisLayout::kNumSubDots; ++i)
    {
        const auto dot = row.removeFromLeft (ChassisLayout::kSubDotSize);

        g.setColour (theme::subColour (i).withMultipliedAlpha (ChassisLayout::kSubDotOpacity));
        g.fillEllipse (dot.toFloat());

        row.removeFromLeft (ChassisLayout::kSubDotGap);
    }

    g.setColour (lnf.token (theme::Token::fgFaint));
    type::drawTracked (g, type::Style::stripMicroLabel,
                       ChassisLayout::subDotsLabel(),
                       row.withTrimmedLeft (ChassisLayout::kSubDotsLabelInset).toFloat(),
                       juce::Justification::centredLeft);
}

void Chassis::paintSidePanel (juce::Graphics& g, juce::Rectangle<int> area) const
{
    g.setColour (lnf.token (theme::Token::raised));
    g.fillRect (area);

    g.setColour (lnf.token (theme::Token::line));
    g.fillRect (area.getX(), area.getY(), 1, area.getHeight());

    // `border-left: 1px solid var(--line)` AND `inset 0 1px 0 <highlight>`. An
    // inset box-shadow is drawn inside the border box, so the highlight starts
    // one column right of the border rather than running over it.
    paintRaisedHighlight (g, area.withTrimmedLeft (1), lnf.shadows().raisedHighlight);
}

void Chassis::paintSequencer (juce::Graphics& g, juce::Rectangle<int> area) const
{
    g.setColour (lnf.token (theme::Token::sunken));
    g.fillRect (area);

    const auto shadows = lnf.shadows();
    paintWellShadow (g, area, shadows.wellShadow, shadows.wellRadius);

    g.setColour (lnf.token (theme::Token::line));
    g.fillRect (area.getX(), area.getY(), area.getWidth(), 1);
}

void Chassis::paintFooter (juce::Graphics& g, juce::Rectangle<int> area) const
{
    g.setColour (lnf.token (theme::Token::raised));
    g.fillRect (area);

    g.setColour (lnf.token (theme::Token::line));
    g.fillRect (area.getX(), area.getY(), area.getWidth(), 1);

    // `border-top: 1px solid var(--line)` AND `inset 0 1px 0 <highlight>` —
    // two different rows. Painting the highlight first and the border over it
    // put both on row 0, so the footer's highlight never rendered at all.
    paintRaisedHighlight (g, area.withTrimmedTop (1), lnf.shadows().raisedHighlight);
}

} // namespace forrobox
