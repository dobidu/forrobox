#include <cmath>

#include "Chassis.h"

#include "GearButton.h"
#include "Settings.h"

#include "PluginProcessor.h"

#include "BpmField.h"
#include "FooterBar.h"
#include "HeaderBar.h"
#include "SequencerGrid.h"
#include "KnobAttachment.h"
#include "LogoMark.h"
#include "Segmented.h"
#include "Surface.h"
#include "ValueScreen.h"
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

int ChassisLayout::indexOfProfile (juce::StringRef profileId, int ifUnknown)
{
    for (size_t i = 0; i < ids::profileInfos.size(); ++i)
        if (profileId == juce::StringRef (ids::profileInfos[i].id))
            return static_cast<int> (i);

    return ifUnknown;
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
        juce::String (juce::CharPointer_UTF8 ("CACHAÇA")),
    }};

    return names;
}

const juce::String& ChassisLayout::presetStubLabel()
{
    static const juce::String label { juce::CharPointer_UTF8 ("PÉ-DE-SERRA 01") };
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
        return centredInRow (header, box);
    };

    auto row = header.reduced (kHeaderPadX, 0);

    const auto takeLeft = [&row] (int width) { return row.removeFromLeft (width); };

    // ── 1. the logo lockup: mark + wordmark, gap 10 ────────────────────────
    out.logoMark = centred (takeLeft (logo::kWidth).withHeight (logo::kHeight));
    row.removeFromLeft (logo::kLockupGap);

    const auto wordmarkWidth = juce::roundToInt (
        type::trackedWidth (type::Style::wordmark,
                            juce::String (juce::CharPointer_UTF8 ("FORRÓ·BOX"))));

    out.wordmark = centred (takeLeft (wordmarkWidth)
                                .withHeight (textBox (type::Style::wordmark)));
    row.removeFromLeft (kHeaderGap);

    // ── 1b. the settings gear, the one box in this row no stylesheet names ──
    //
    // It takes space from the SAME left-to-right flow as every other cluster,
    // so nothing here is special-cased. What that costs is stated rather than
    // hidden: the SWING/CACHAÇA group below is centred in whatever `row` is left
    // after both ends have taken theirs, so consuming `kSize + kHeaderGap` on
    // the left shifts that group right by half of it. No length
    // `verify-geometry.py` cross-checks changes, and every assertion on the
    // group is relative to the group — but the shift is real, it is visible,
    // and the human checkpoint is where it is judged. 08-02.
    out.gearButton = centred (takeLeft (gear::kSize).withHeight (gear::kSize));
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

    out.halfButton = centred (takeLeft (miniWidth ("÷2")).withHeight (miniHeight));
    row.removeFromLeft (bpmfield::kMiniGap);
    out.doubleButton = centred (takeLeft (miniWidth ("×2")).withHeight (miniHeight));
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
                                                        type::Style::quickSwitchCode,
                                                        Segmented::Variant::quickSwitch);
    const auto styleSegmentsHeight = Segmented::heightOf (type::Style::quickSwitchCode,
                                                          Segmented::Variant::quickSwitch);

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

    // The header's CLUSTERS are not derived here. `HeaderBar` owns them, in its
    // own coordinates, and asking for them twice cost ~36 us per resize — most
    // of this whole function — for a copy only the tests read.
    out.footer    = remaining.removeFromBottom (kFooterHeight);
    out.sequencer = remaining.removeFromBottom (kSequencerHeight);
    out.main      = remaining;                      // whatever is left: the 1fr row

    auto mainRow  = out.main;
    out.sidePanel = mainRow.removeFromRight (kSidePanelWidth);
    out.matrix    = mainRow;

    // Five equal columns with 1 px gaps. The width does not divide by five
    // evenly at the design size (609 px of matrix), which is why the remainder
    // has to go somewhere — `tileAcross` spreads it across the gaps rather than
    // dropping it all in the last strip.
    for (int i = 0; i < kNumStrips; ++i)
        out.strips[static_cast<size_t> (i)] = tileAcross (out.matrix, i, kNumStrips, kStripGap);

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

    // The LED sits left of the index, both right-aligned in the head row —
    // `.strip-head-r { display: flex; align-items: center; gap: 7px }`
    // (css:275). The index's width is ASKED of the type scale rather than
    // written down: "01" in the strip-index style, which is what paintStrip
    // draws there.
    {
        const auto indexWidth = juce::roundToInt (
            type::trackedWidth (type::Style::stripIndex, "01"));

        const auto led = juce::Rectangle<int> (0, 0, hitviz::kLedDiameter, hitviz::kLedDiameter)
                             .withX (out.headRow.getRight() - indexWidth - hitviz::kLedGap
                                     - hitviz::kLedDiameter);

        out.trigLed = centredInRow (out.headRow, led);
    }

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

    // The sub-dots open the kit. A child, not a rectangle this class hit-tests:
    // `Chassis::mouseUp` was one of the three containers /simplify found with
    // no right-click guard, and the zone carries it.
    //
    // HERE rather than in `attachParameters`, where it first landed: it takes
    // no parameters, and the ordering it was placed there to achieve is not a
    // thing this code depends on — see the member's own comment.
    subDotsZone.onClick = [this] { kitOverlay->setOpen (true); };
    addAndMakeVisible (subDotsZone);

    // The five visualisers, for the same reason the bars below exist here: a
    // HitVisualiser needs only an accent colour, which is known now. Its LEVEL
    // arrives with the poll, the way the bars' controls arrive with
    // attachParameters.
    //
    // A std::array, not a vector — kNumStrips is fixed, `stripControls` beside
    // it is already an array, and the vector's emptiness before attach was what
    // forced a size guard and a throwaway-visualiser branch into paintStrip.
    for (int channel = 0; channel < ChassisLayout::kNumStrips; ++channel)
        hitVisualisers[(size_t) channel].setAccent (
            theme::accent (static_cast<theme::Accent> (channel)));

    // The bar exists from construction, painting the header surface a bare
    // chassis used to paint itself. Its controls arrive with attachParameters,
    // the way the strips' do.
    headerBar = std::make_unique<HeaderBar> (lnf);
    addAndMakeVisible (*headerBar);

    // The kit panel exists from construction, hidden — same reason the bars do.
    // It sets itself always-on-top, which is css:554's `z-index: 40` as a
    // property of the component rather than a consequence of add order.
    sidePanel = std::make_unique<SidePanel> (lnf);
    addAndMakeVisible (*sidePanel);

    kitOverlay = std::make_unique<KitOverlay> (lnf);
    aboutOverlay = std::make_unique<AboutOverlay> (lnf);

    footerBar = std::make_unique<FooterBar> (lnf);
    addAndMakeVisible (*footerBar);

    sequencerGrid = std::make_unique<SequencerGrid> (lnf);
    addAndMakeVisible (*sequencerGrid);

    setSize (ChassisLayout::kWidth, ChassisLayout::kHeight);
}

Chassis::~Chassis() = default;

const std::array<juce::String, static_cast<size_t> (ChassisLayout::kNumStrips)>&
ChassisLayout::sampleNames()
{
    static const std::array<juce::String, static_cast<size_t> (kNumStrips)> names {{
        juce::String (juce::CharPointer_UTF8 ("Couro Aberto")),
        juce::String (juce::CharPointer_UTF8 ("Aço Aberto")),
        juce::String (juce::CharPointer_UTF8 ("Pandeiro Médio")),
        juce::String (juce::CharPointer_UTF8 ("Ganzá Seco")),
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
    static const juce::String text { juce::CharPointer_UTF8 ("BB · CX · HH · TOM ↗") };
    return text;
}

const juce::String& ChassisLayout::arrowPrev()
{
    static const juce::String text { juce::CharPointer_UTF8 ("‹") };
    return text;
}

const juce::String& ChassisLayout::arrowNext()
{
    static const juce::String text { juce::CharPointer_UTF8 ("›") };
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

void Chassis::pollVisualisers()
{
    auto* owner = dynamic_cast<::ForroBoxAudioProcessor*> (attachedProcessor);

    if (owner == nullptr)
        return;

    // ── record what each step played, as it is published ───────────────────
    //
    // ONE decode per poll: "stopped" below answers for the same publication the
    // hit is taken from.
    const auto snapshot = owner->getStepSnapshot();

    if (const auto count = owner->getStepPublicationCount(); count != lastPublicationSeen)
    {
        lastPublicationSeen = count;

        if (juce::isPositiveAndBelow (snapshot.step, State::kMaxSteps))
            playedVelocities[(size_t) snapshot.step] = snapshot.velocities;
    }

    const auto stopped = owner->isTransportStopped();

    // ── fire when the CORRECTED position reaches a step ────────────────────
    //
    // The position decides WHEN and the publication decided WHAT, so this and
    // the playhead read one scalar and cannot drift apart. Wrapped the way
    // Playhead::lineCentreFor wraps it, because the corrected position is
    // negative for the first output delay after Play.
    if (! stopped)
    {
        const auto window = juce::jmax (1, sequencerGrid->getStepCount());
        const auto position = owner->getDisplayPositionInSteps();

        const auto wrapped = std::fmod (std::fmod (position, (double) window) + window,
                                        (double) window);
        const auto reached = juce::jlimit (0, window - 1, (int) std::floor (wrapped));

        if (reached != lastStepShown)
        {
            // The resolved mute/solo gate, from the same resolver the engine
            // renders with. Re-deriving "is this channel audible" in the UI
            // would be a second answer to a question that already has one, and
            // the two would disagree the first time solo's precedence changed.
            const auto settings = owner->resolveChannelSettings();

            // EVERY step crossed since the last poll, not only the one landed
            // on. At 60 Hz against ~9 steps a second nothing is ever skipped —
            // but a throttled message thread or a host that pauses an inactive
            // editor drops polls, and firing only the latest would swallow the
            // steps in between. `HitVisualiser::advance` is hardened against
            // exactly that hazard two functions away; this is the same hazard
            // on the trigger side. A test that renders in bulk caught it.
            //
            // Capped at one window so a long stall replays a bar at most,
            // rather than walking however many steps the transport covered.
            auto step = lastStepShown;

            for (int guard = 0; guard < window && step != reached; ++guard)
            {
                step = (step + 1) % window;

                const auto& played = playedVelocities[(size_t) step];

                for (int lane = 0; lane < State::kNumLanes; ++lane)
                {
                    const auto channel = VoiceEngine::channelForLane (lane);

                    if (! juce::isPositiveAndBelow (channel, (int) hitVisualisers.size()))
                        continue;

                    // PLANNING.md:489 — muted or soloed-out channels do NOT light.
                    if (! settings.channels[(size_t) channel].audible)
                        continue;

                    if (const auto velocity = played[(size_t) lane]; velocity > 0)
                        hitVisualisers[(size_t) channel].trigger (
                            (float) velocity / (float) State::kMaxVelocity);
                }
            }

            lastStepShown = reached;
        }
    }
    else
    {
        // So the first step after Play fires rather than being swallowed as
        // "the same step we were already showing".
        lastStepShown = -1;
    }

    // ── decay, and repaint only what changed ───────────────────────────────

    for (size_t i = 0; i < hitVisualisers.size(); ++i)
    {
        auto& viz = hitVisualisers[i];
        const auto wasLit = viz.isLit();

        if (stopped)
            viz.reset();
        else
            viz.advance (1);

        if (viz.isLit() || wasLit)
        {
            const auto& interior = layout.stripLayouts[i];

            // The LED's glow falls OUTSIDE its box, so the repaint has to cover
            // the widest it can get: base + span, at full velocity. Rounded up
            // rather than truncated — a 9.0 that repainted 9 px would leave the
            // outermost ring of the glow stale.
            repaint (interior.trigLed.expanded (
                static_cast<int> (std::ceil (hitviz::kLedGlowBase + hitviz::kLedGlowSpan))));
            repaint (interior.hitVisualiser);
        }
    }
}

void Chassis::attachParameters (juce::AudioProcessorValueTreeState& apvts, ValueTooltip* tooltip)
{
    attachedProcessor = dynamic_cast<::ForroBoxAudioProcessor*> (&apvts.processor);

    // addChildComponent, not addAndMakeVisible-then-hide: JUCE's own one-line
    // idiom for "add it hidden", and the overlay's visibility is now the ONE
    // answer to "is it open", so a moment of it being true while nothing had
    // rebuilt was a moment the two disagreed.
    addChildComponent (*kitOverlay);
    kitOverlay->attachParameters (apvts);

    // Added hidden, and LAST so it sits above the kit overlay: the gear is
    // reachable while that panel is open, and a panel that opened underneath
    // another one would look like nothing happened.
    addChildComponent (*aboutOverlay);
    aboutOverlay->toFront (false);

    sidePanel->attachParameters (apvts);

    // The reload's confirmation flash, installed on BOTH entry points from the
    // one place that can see every view — `PLANNING.md:615`.
    sidePanel->onProfileLoaded = [this] { flashPadsForReload(); };
    headerBar->onProfileLoaded = [this] { flashPadsForReload(); };

    // The gear's menu lives HERE because this is the only object that can see
    // both the store and everything the store repaints.
    headerBar->onGearClicked = [this] { showSettingsMenu(); };

    // NOT applyStoredSettings() HERE, and the reason is a boundary rather than
    // an oversight. A Chassis is handed a LookAndFeel it does not own, and
    // seeding that object from a global store is the job of whoever CREATED it
    // — `ForroBoxAudioProcessorEditor` in the product, a rig in the tests.
    //
    // Calling it here instead made `ChassisRig { theme::Mode::light }` silently
    // dark, because the store said so: six light-theme checks failed and the
    // rig's whole reason for taking a mode stopped working. That is 08-01's
    // lesson one level over — a component gained a default and the callers that
    // had been setting the value another way lost.

    if (attachedProcessor != nullptr)
    {
        lastPublicationSeen = attachedProcessor->getStepPublicationCount();

        visualiserPoll.tick = [this] { pollVisualisers(); };
        visualiserPoll.startTimerHz (seq::kPlayheadPollHz);
    }

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

    headerBar->attachParameters (apvts);
    footerBar->attachParameters (apvts);
    sequencerGrid->attachParameters (apvts);

    resized();
}

void Chassis::refreshHeaderFromProcessor()
{
    headerBar->refreshFromProcessor();
}

// ── 08-02: the settings menu ───────────────────────────────────────────────
//
// A `juce::PopupMenu`, drawn by JUCE against this chassis's own LookAndFeel.
// That is the whole reason it is a PopupMenu rather than a panel: `PLANNING.md`
// specifies no settings UI at all — `:902` calls the prototype's tweaks panel
// "prototype-only scaffolding — not part of the plugin design" — so a custom
// panel would mean inventing a geometry, a type scale and a hit-test for five
// controls the design source never drew. A menu invents nothing but the gear.

namespace
{
/** Result ids. Zero is reserved: `PopupMenu` sends it for a dismissal.

    EVERY BASE IS OFFSET BY AN INDEX, never by a VALUE, and that is a bug fix
    rather than a style. The accent items were first built as `kAccentBase +
    percent`, which put 100% at 300 + 100 = 400 — exactly `kStepsBase`. Two menu
    items then shared one id, the dispatch's accent branch swallowed both step
    items, and choosing "Default step count -> 16" silently set the accent to
    100% instead while the step setting became unreachable from the UI
    altogether. Found by /code-review; the tick checks in `UiTest` caught it in
    the same run. An index cannot collide with a neighbouring base as long as no
    setting offers 100 choices, which `kSpan` asserts. */
enum SettingsMenuId
{
    kSpan       = 100,
    kThemeBase  = 1 * kSpan,
    kRadiusBase = 2 * kSpan,
    kAccentBase = 3 * kSpan,
    kStepsBase  = 4 * kSpan,
    kAboutId    = 9 * kSpan,
};

/** The accent values the menu offers, inside `PLANNING.md:859`'s 35-100 range.
    A menu cannot present a continuum, so it presents its ends and the middle —
    and the FLOOR and CEILING come from the settings table rather than being
    typed here, so widening the range widens the menu. */
constexpr std::array<int, 4> kAccentSteps { 35, 60, 80, 100 };

// NO BAND RUNS INTO THE NEXT, asserted against the tables themselves rather
// than against a literal. The accent ids were once `kAccentBase + percent`,
// which put 100% on 400 — `kStepsBase` exactly — so two items shared one id and
// the step setting became unreachable from the menu. A check in the test file
// spelled `300 + 4 <= 400`, every term typed there, and could not have seen a
// renumbering here. /code-review found the collision; /simplify moved the guard
// to where the constants are.
static_assert (kAccentBase + static_cast<int> (kAccentSteps.size()) <= kStepsBase,
               "the accent ids must end before the step ids begin");
static_assert (kThemeBase + 2 <= kRadiusBase,
               "the theme ids must end before the corner-radius ids begin");
static_assert (kRadiusBase + static_cast<int> (settings::cornerRadiiPx.size()) <= kAccentBase,
               "the corner-radius ids must end before the accent ids begin");
static_assert (kStepsBase + static_cast<int> (ids::stepWindows.size()) <= kAboutId,
               "the step ids must end before the About id");

static_assert (kAccentSteps.front()
                   == forrobox::settings::info (forrobox::Setting::accentIntensity).minValue,
               "the menu's lowest accent must be the setting's floor");
static_assert (kAccentSteps.back()
                   == forrobox::settings::info (forrobox::Setting::accentIntensity).maxValue,
               "the menu's highest accent must be the setting's ceiling");
} // namespace

juce::PopupMenu Chassis::buildSettingsMenu() const
{
    const auto& store = Settings::shared();

    juce::PopupMenu menu;

    // EVERY ITEM SHOWS ITS CURRENT VALUE with a tick. A menu that only sets is a
    // menu that cannot tell you what the plugin is doing, and these are exactly
    // the settings a user forgets having changed.
    juce::PopupMenu themeMenu;
    themeMenu.addItem (kThemeBase + 0, "Dark", true, store.get (Setting::theme) == 0);
    themeMenu.addItem (kThemeBase + 1, "OP-1 Light", true, store.get (Setting::theme) == 1);
    menu.addSubMenu ("Theme", themeMenu);

    juce::PopupMenu radiusMenu;
    for (size_t i = 0; i < settings::cornerRadiiPx.size(); ++i)
        radiusMenu.addItem (kRadiusBase + static_cast<int> (i),
                            juce::String (juce::roundToInt (settings::cornerRadiiPx[i])) + " px",
                            true,
                            store.get (Setting::cornerRadius) == static_cast<int> (i));
    menu.addSubMenu ("Corner radius", radiusMenu);

    juce::PopupMenu accentMenu;
    for (size_t i = 0; i < kAccentSteps.size(); ++i)
        accentMenu.addItem (kAccentBase + static_cast<int> (i),
                            juce::String (kAccentSteps[i]) + "%",
                            true,
                            store.get (Setting::accentIntensity) == kAccentSteps[i]);
    menu.addSubMenu ("Accent intensity", accentMenu);

    // The one setting here that is not cosmetic. It seeds a FRESH instance and
    // never touches this one — see the processor's constructor.
    juce::PopupMenu stepsMenu;
    for (size_t i = 0; i < ids::stepWindows.size(); ++i)
        stepsMenu.addItem (kStepsBase + static_cast<int> (i),
                           juce::String (ids::stepWindows[i]),
                           true,
                           store.get (Setting::defaultSteps) == static_cast<int> (i));
    menu.addSubMenu ("Default step count", stepsMenu);

    menu.addSeparator();
    menu.addItem (kAboutId, "About " + juce::String (juce::CharPointer_UTF8 ("Forr\xc3\xb3 Box")) + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6")));

    return menu;
}

bool Chassis::applySettingsMenuResult (int resultId)
{
    if (resultId == 0)
        return false;   // dismissed

    auto& store = Settings::shared();

    if (resultId == kAboutId)
    {
        showAbout();
        return true;
    }

    // EVERY BRANCH BOUNDS-CHECKS ITS OWN BAND. Only the accent one did, so
    // `kThemeBase + 7` wrote theme = 1 and returned TRUE — `store.set` clamps,
    // so an id the menu never built became a silent wrong-setting write while
    // this function's docstring promised it returned false. Harmless only while
    // every band is full; the moment one shrinks it is a real wrong write.
    // /code-review.
    const auto within = [resultId] (int base, size_t count)
    {
        return resultId >= base
            && resultId < base + static_cast<int> (count);
    };

    if (within (kThemeBase, 2))
        store.set (Setting::theme, resultId - kThemeBase);
    else if (within (kRadiusBase, settings::cornerRadiiPx.size()))
        store.set (Setting::cornerRadius, resultId - kRadiusBase);
    else if (within (kAccentBase, kAccentSteps.size()))
        store.set (Setting::accentIntensity,
                   kAccentSteps[static_cast<size_t> (resultId - kAccentBase)]);
    else if (within (kStepsBase, ids::stepWindows.size()))
        store.set (Setting::defaultSteps, resultId - kStepsBase);
    else
        return false;   // an id this menu never offered

    applyStoredSettings();
    return true;
}

void Chassis::applyStoredSettings()
{
    const auto& store = Settings::shared();

    // THE SETTERS PHASE 4 LEFT WITH NO CALLERS. `LookAndFeel.h:59` says exactly
    // why they exist: "Both tweakables are user-facing in Phase 8's settings
    // menu, so they are settable now rather than being constants that have to
    // be dug out later." This is that caller.
    lnf.setMode (store.themeMode());
    lnf.setCornerRadius (store.cornerRadiusPx());
    lnf.setAccentIntensity (store.accentIntensity());

    // ONE repaint of the root, and that is enough because nothing caches a
    // palette: all 101 colour reads in src/ go through `lnf.token(...)` at paint
    // time. `setMode`'s own comment states the contract — "Changing it repaints
    // nothing by itself, the caller owns that, because only the caller knows
    // what is on screen." This is the caller that knows.
    // A one-level child loop stood here and was redundant for every child it
    // reached and useless for the only one that would have justified it:
    // `Playhead` is the single `setBufferedToImage` component and it is a
    // GRANDCHILD, so the loop could not reach it anyway. /simplify.
    repaint();
}

void Chassis::showSettingsMenu()
{
    auto* gear = headerBar != nullptr ? headerBar->getGearButton() : nullptr;

    if (gear == nullptr)
        return;

    // ASYNC, and it must be. 07-02 established this for the export dialog:
    // `JUCE_MODAL_LOOPS_PERMITTED=1` is set on the TEST target only and modal
    // loops in a plugin are what that default forbids — a host's message thread
    // is not ours to block.
    buildSettingsMenu().showMenuAsync (
        juce::PopupMenu::Options().withTargetComponent (gear),
        [safeThis = juce::Component::SafePointer<Chassis> (this)] (int resultId)
        {
            // The menu outlives nothing, but the chassis can die while it is
            // open — a host closing the editor does exactly that. 04-02's
            // use-after-free came from the mirror of this assumption.
            if (safeThis != nullptr)
                safeThis->applySettingsMenuResult (resultId);
        });
}

void Chassis::showAbout()
{
    if (aboutOverlay == nullptr)
        return;

    aboutOverlay->setOpen (true);
}

void Chassis::flashPadsForReload()
{
    sequencerGrid->flashLitPads();
    kitOverlay->flashLitPads();
}

void Chassis::resized()
{
    layout = ChassisLayout::forBounds (getLocalBounds());

    // The WHOLE chassis — css:554's `inset: 0` against a subview appended to
    // #fb-window (app.js:37), which is what settles PLANNING.md:519's narrower
    // prose. See KitOverlay.h.
    kitOverlay->setBounds (getLocalBounds());

    // The ABOUT panel takes the whole chassis for the same reason, and HERE
    // rather than in `showAbout` — sized only on open, a resize while it was
    // showing left it at the old size. /simplify.
    aboutOverlay->setBounds (getLocalBounds());

    // The header's own rectangle. The bar derives its clusters from its local
    // bounds, which is the same 1200x72 box `layout.headerLayout` was derived
    // from — the header sits at the origin, so the two cannot disagree.
    headerBar->setBounds (layout.header);
    footerBar->setBounds (layout.footer);
    sequencerGrid->setBounds (layout.sequencer);
    sidePanel->setBounds (layout.sidePanel);

    // `subDots` is EMPTY on strips 1-4 — a box being empty is meaningful here,
    // which ChassisLayout records — so an empty rectangle gives a zone that
    // hits nothing, which is the same answer the old `isEmpty()` test gave.
    subDotsZone.setBounds (
        layout.stripLayouts[static_cast<size_t> (ChassisLayout::kNumStrips - 1)].subDots);

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

    // No header here: HeaderBar is a child and paints itself, which is what
    // makes its region one component's business rather than this one's.
    paintIfVisible (layout.matrix,    [&] { paintMatrix (g, layout.matrix); });
    // No sequencer either: SequencerGrid is a child and paints itself.

    // No footer either: FooterBar is a child and paints itself.
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
    // Cull EACH PIECE, not just the strip. 05-02 gave this function two things
    // that move at 60 Hz, so it now runs sixty times a second for a repaint of
    // an 8 px dot — and /simplify measured that 85% of its cost is furniture
    // redrawn identically: two drawTracked calls, the accent bar's DropShadow,
    // the sample slot, the cycler, the ghost label and the sub-dots. 475 us a
    // frame across five strips, continuously, for a dot and a bar.
    //
    // The same shape Chassis::paint already uses one level up, where its own
    // comment records the measurement that motivated it ("~15 us").
    const auto clip = g.getClipBounds();

    const auto paintIfVisible = [&clip] (juce::Rectangle<int> box, auto&& painter)
    {
        if (box.intersects (clip))
            painter();
    };

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
    paintIfVisible (interior.headRow, [&]
    {
        type::drawTracked (g, type::Style::stripInstrumentName, info.displayName,
                           headRow, juce::Justification::centredLeft);

    g.setColour (lnf.token (theme::Token::fgDim));
        type::drawTracked (g, type::Style::stripIndex,
                           juce::String (channelIndex + 1).paddedLeft ('0', 2),
                           headRow, juce::Justification::centredRight);
    });

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

    paintIfVisible (interior.accentBar.expanded (ChassisLayout::kAccentGlowRadius), [&]
    {
        juce::DropShadow (glow, ChassisLayout::kAccentGlowRadius, {})
            .drawForRectangle (g, bar.toNearestInt());
    });

    // `--accent-i` applies to this bar TWICE — the glow alpha above and the
    // fill's saturation here (css:285). Both are the identity at the default
    // intensity of 1.0; only one of them used to exist.
    paintIfVisible (interior.accentBar, [&]
    {
        g.setColour (theme::accentFill (colour, lnf.accentIntensity()));
        g.fillRoundedRectangle (bar, 1.0f);
    });

    // ── the trigger LED and the activity meter (05-02) ──────────────────────
    //
    // Both read ONE level, held by the chassis so a single poll drives all five.
    // Drawn here rather than as components because the strip itself is painted,
    // not composed — these are the first two things in it that MOVE, and a
    // component each would be ten more children for two circles and two bars.
    // ONE path. The visualisers exist from construction, the way the three bars
    // do — a HitVisualiser needs only an accent colour, which is known then; the
    // processor is needed for the POLL, not for the objects. Building them in
    // attachParameters instead meant a size guard and a ten-line else branch
    // constructing a throwaway visualiser per strip per paint, so that a chassis
    // with no processor still drew the well. Found by /simplify.
    {
        const auto& viz = hitVisualisers[(size_t) channelIndex];

        paintIfVisible (interior.trigLed.expanded (
                            static_cast<int> (std::ceil (hitviz::kLedGlowBase
                                                         + hitviz::kLedGlowSpan))),
                        [&] { viz.paintLed (g, interior.trigLed); });

        paintIfVisible (interior.hitVisualiser,
                        [&] { viz.paintMeter (g, interior.hitVisualiser, lnf); });
    }

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
    paintIfVisible (interior.sampleSlot, [&] { paintSampleSlot (g, interior, channelIndex); });
    paintIfVisible (interior.patternCycler, [&] { paintPatternCycler (g, interior); });
    paintIfVisible (interior.ghostLabel,  [&] { paintGhostLabel (g, interior, channelIndex); });

    if (! interior.subDots.isEmpty())
        paintIfVisible (interior.subDots, [&] { paintSubDots (g, interior); });

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


} // namespace forrobox
