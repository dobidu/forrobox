#include "KitOverlay.h"

#include "PluginProcessor.h"
#include "SequencerGrid.h"
#include "VoiceEngine.h"
#include "Surface.h"

#include <cmath>

namespace forrobox
{

// The kit's lanes ARE the composite row's cover, in the order drawn — asserted
// rather than assumed. Without this, a reordered lane table silently mislabels
// and miscolours every row.
static_assert ([]
               {
                   const auto covered = detail::channelToLanes[
                       static_cast<size_t> (detail::compositeChannel())];

                   if (covered.count != static_cast<int> (kitLaneIds.size()))
                       return false;

                   for (size_t i = 0; i < kitLaneIds.size(); ++i)
                       if (covered.entries[i] != detail::laneNamed (kitLaneIds[i]))
                           return false;

                   return true;
               }(),
               "the kit rows are bb, cx, hh, tom in that order — the overlay's names and colours "
               "are bound to the row index, so a reordered lane table would mislabel them");

const juce::String& subLineText()
{
    // app.js:466, verbatim. It was NOT: the source carried C3 83 C2 A7 where app.js
    // has C3 A7, so the panel rendered "peÃ§a" and a test pinned the mojibake
    // as "the accent intact". Caught by looking at the reference render.
    static const juce::String text = juce::String::fromUTF8 (
        "Sequencie cada peça do kit. As batidas aparecem somadas na linha BATERIA "
        "do sequenciador principal.");

    return text;
}

juce::String kitPieceName (int index)
{
    // data.js:25-28, in lane order: bb, cx, hh, tom.
    static const std::array<const char*, 4> names { "Bumbo", "Caixa", "Chimbal", "Surdo" };

    return juce::isPositiveAndBelow (index, static_cast<int> (names.size()))
             ? juce::String::fromUTF8 (names[static_cast<size_t> (index)])
             : juce::String();
}

double cubicBezierEase (double t) noexcept
{
    // The curve css:565 names, solved rather than approximated.
    //
    // A cubic-bezier easing is a PARAMETRIC curve: x and y are both cubics in a
    // parameter s, and the easing is y at the s where x == t. `smoothstep` looks
    // like it and is a different function — which is exactly the kind of
    // plausible substitute this project's tests exist to catch, so the control
    // points are used rather than eyeballed.
    const auto clamped = juce::jlimit (0.0, 1.0, t);

    const auto bezier = [] (double a, double b, double s)
    {
        const auto u = 1.0 - s;
        return 3.0 * u * u * s * a + 3.0 * u * s * s * b + s * s * s;
    };

    // Newton would need the derivative and can stall where the curve is flat;
    // bisection over a monotonic x is short, exact enough for a 200 ms
    // animation, and has no failure mode to reason about.
    auto low = 0.0, high = 1.0;

    for (int i = 0; i < 24; ++i)
    {
        const auto mid = (low + high) * 0.5;

        if (bezier (kit::kEaseX1, kit::kEaseX2, mid) < clamped)
            low = mid;
        else
            high = mid;
    }

    return bezier (kit::kEaseY1, kit::kEaseY2, (low + high) * 0.5);
}

KitOverlayLayout KitOverlayLayout::forBounds (juce::Rectangle<int> chassis) noexcept
{
    KitOverlayLayout out;

    // The WHOLE chassis. See the header for the spec conflict this settles.
    out.scrim = chassis;

    out.panel = chassis.removeFromRight (kit::kPanelWidth);
    out.border = out.panel.withWidth (kit::kPanelBorder);

    out.content = out.panel.reduced (kit::kPanelPadX, kit::kPanelPadY);

    auto remaining = out.content;

    // ── head: the title left, the close button right ────────────────────────
    const auto headHeight = juce::jmax (kit::kCloseSize,
                                        textBox (type::Style::kitTitle));

    out.head = remaining.removeFromTop (headHeight);
    remaining.removeFromTop (kit::kHeadMarginBottom);

    // Hoisted. `centredInRow (out.head, out.head.removeFromRight (...))` reads
    // and mutates `out.head` in two argument expressions, which C++17 leaves
    // indeterminately sequenced — benign only because centredInRow reads the Y
    // and height that removeFromRight does not touch, an invariant living in
    // another file. /code-review.
    const auto headRow = out.head;

    out.close = centredInRow (headRow,
                              out.head.removeFromRight (kit::kCloseSize)
                                      .withHeight (kit::kCloseSize));
    out.title = centredInRow (headRow,
                              out.head.withHeight (textBox (type::Style::kitTitle)));

    out.subLine = remaining.removeFromTop (textBox (type::Style::kitSubLine));
    remaining.removeFromTop (kit::kSubLineMarginBottom);

    // ── four rows ───────────────────────────────────────────────────────────
    //
    // The row is as tall as its TALLEST child — the pad at 26 px against a
    // two-line label — which is `flexRow`'s law, not a literal.
    const auto labelHeight = textBox (type::Style::kitRowName)
                           + kit::kRowLabelLineGap
                           + textBox (type::Style::kitRowFull);

    const auto rowHeight = flexRow (kit::kPadHeight, labelHeight);

    for (size_t i = 0; i < out.rows.size(); ++i)
    {
        if (i > 0)
            remaining.removeFromTop (kit::kRowGap);

        auto& row = out.rows[i];

        row.bounds = remaining.removeFromTop (rowHeight);

        auto columns = row.bounds;

        row.label = columns.removeFromLeft (kit::kRowLabelWidth);
        columns.removeFromLeft (kit::kRowLabelGap);

        row.pads = centredInRow (row.bounds, columns.withHeight (kit::kPadHeight));

        auto lines = centredInRow (row.label, row.label.withHeight (labelHeight));

        row.name = lines.removeFromTop (textBox (type::Style::kitRowName));
        lines.removeFromTop (kit::kRowLabelLineGap);
        row.full = lines.removeFromTop (textBox (type::Style::kitRowFull));
    }

    return out;
}

KitOverlay::KitOverlay (ForroBoxLookAndFeel& lnfToUse) : lnf (lnfToUse)
{
    setVisible (false);

    // The scrim swallows clicks that would otherwise reach the chassis beneath —
    // which is what makes clicking outside the panel a dismissal rather than an
    // edit of whatever happens to be under the pointer.
    setInterceptsMouseClicks (true, true);

    // ALWAYS ON TOP, not "added last".
    //
    // `addChildComponent` appends to the FRONT, and `attachParameters` adds this
    // near the top and then some fifty strip knobs, buttons and faders after it
    // — so every one of them painted OVER the scrim, strips 4 and 5 painted
    // inside the panel, and all of them still took the mouse. A "modal" overlay
    // you could drag a knob through, under a comment claiming it was added last.
    //
    // `toFront` at the end of attach would fix it once and break again the next
    // time a control is added after the call. JUCE keeps always-on-top children
    // above the rest whatever the add order (juce_Component.cpp:1214), so this
    // is the z-order as a property of the component rather than a rule the
    // owner has to remember. Found by /code-review.
    setAlwaysOnTop (true);

    panel = std::make_unique<Panel> (*this);
    addAndMakeVisible (*panel);

    closeButton = std::make_unique<Button> (lnf, Button::Variant::base, juce::String::fromUTF8 ("\xc3\x97"));
    closeButton->onClick = [this] { setOpen (false); };

    // Onto the PANEL, not the overlay: everything that fades and slides with the
    // entrance is a child of the thing that fades and slides.
    panel->addAndMakeVisible (*closeButton);
}

KitOverlay::~KitOverlay() = default;

void KitOverlay::attachParameters (juce::AudioProcessorValueTreeState& state)
{
    // No `apvts` member: it was stored and never read. Everything this needs —
    // the step window, the pattern, the publication count — comes through the
    // processor. /code-review.
    processor = dynamic_cast<::ForroBoxAudioProcessor*> (&state.processor);

    rebuildPads();
    resized();
    refreshFromState();
}

void KitOverlay::setOpen (bool shouldBeOpen)
{
    // Against `isVisible()`, which `setVisible` below already makes the one
    // answer. A separate `open` member was a second spelling of the same fact —
    // the class branched on one, every caller and every test on the other, and
    // `Chassis::attachParameters` drove them apart for a moment. /simplify.
    if (isVisible() == shouldBeOpen)
        return;

    // From zero on every open, so a reopen animates rather than appearing
    // already at rest. Closing is immediate: css:559's `display: none` has no
    // exit transition, and inventing one would be a motion the prototype does
    // not have.
    progress = shouldBeOpen ? 0.0 : 1.0;

    setVisible (shouldBeOpen);

    if (! shouldBeOpen)
    {
        // A shut panel does nothing at all — no entrance to advance and no
        // pattern to follow, because `setOpen (true)` refreshes on the way in.
        entrancePoll.stopTimer();
        return;
    }

    rebuildPads();
    resized();
    refreshFromState();
    toFront (false);

    // Re-based on every open, so the first tick reports one frame rather than
    // however long the panel had been shut — which is the whole entrance.
    lastPollSeconds = 0.0;
    entrancePoll.tick = [this] { poll(); };
    entrancePoll.startTimerHz (seq::kPlayheadPollHz);
}

void KitOverlay::poll()
{
    // The pattern first, and NOT behind the interval guard below: following a
    // writer has nothing to do with elapsed time, and gating it on "this is not
    // the first tick" made a single poll after a host recall do nothing.
    refreshIfStateChanged();

    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const auto previous = lastPollSeconds;

    lastPollSeconds = now;

    if (previous <= 0.0)
        return;   // the first tick has no interval to report

    // Clamped at both ends: a long stall finishes the entrance rather than
    // skipping past it by a factor of hundreds, and a clock that steps backwards
    // never runs it in reverse.
    advanceEntrance (juce::jlimit (0.0, kit::kEntranceSeconds, now - previous));
}

void KitOverlay::advanceEntrance (double seconds) noexcept
{
    if (! isVisible() || progress >= 1.0)
        return;

    progress = juce::jlimit (0.0, 1.0, progress + seconds / kit::kEntranceSeconds);

    // Move and fade the PANEL, not every element on it. `resized()` used to run
    // here, repositioning the close button and up to 128 pads on every frame of
    // a 200 ms animation, to express a translation the panel's own position
    // already carries.
    placePanel();

    // The overlay still repaints, but only where the shadow is: its bounds are
    // the WHOLE chassis, and repainting all of it per frame cost 6.2 ms and 22k
    // allocations against a 16.6 ms budget. The panel repaints itself through
    // its own setBounds/setAlpha above.
    if (panel != nullptr)
        repaint (panel->getBounds().expanded (kit::kPanelShadowRadius));
}

void KitOverlay::placePanel()
{
    if (panel == nullptr)
        return;

    panel->setAlpha (static_cast<float> (cubicBezierEase (progress)));
    panel->setBounds (layout.panel.translated (entranceOffset(), 0));
}

int KitOverlay::entranceOffset() const noexcept
{
    // 24 px at progress 0, 0 at rest — css:564/566.
    return juce::roundToInt ((1.0 - cubicBezierEase (progress)) * kit::kEntranceOffset);
}

void KitOverlay::rebuildPads()
{
    pads.clear();

    stepCount = readStepWindow (processor);

    // The bateria row's lanes, from the same derivation the grid uses. The kit
    // is NOT a second list of four ids — `lanesForRow` already answers which
    // lanes the composite row covers, and a literal here would be a copy that a
    // reordered lane table could invalidate.
    const auto& covered = lanesForRow (detail::compositeChannel());

    pads.reserve (static_cast<size_t> (covered.size() * stepCount));

    for (int row = 0; row < covered.size(); ++row)
    {
        const auto colour = theme::subColour (row);

        for (int step = 0; step < stepCount; ++step)
        {
            auto pad = std::make_unique<StepPad> (lnf, colour);

            pad->setBeat (step % 4 == 0);
            pad->onClick = [this, row, step] { toggleCell (row, step); };

            panel->addAndMakeVisible (*pad);
            pads.push_back (std::move (pad));
        }
    }
}

StepPad* KitOverlay::padFor (int row, int step) const
{
    if (! juce::isPositiveAndBelow (row, static_cast<int> (layout.rows.size()))
        || ! juce::isPositiveAndBelow (step, stepCount))
        return nullptr;

    const auto index = static_cast<size_t> (row * stepCount + step);

    return index < pads.size() ? pads[index].get() : nullptr;
}

void KitOverlay::toggleCell (int row, int step)
{
    if (processor == nullptr)
        return;

    const auto& covered = lanesForRow (detail::compositeChannel());

    if (! juce::isPositiveAndBelow (row, covered.size()))
        return;

    const auto lane = covered.entries[static_cast<size_t> (row)];

    if (! juce::isPositiveAndBelow (lane, State::kNumLanes)
        || ! juce::isPositiveAndBelow (step, State::kMaxSteps))
        return;

    {
        auto handle = processor->lockPatternState();

        auto& slot = handle->lanes[static_cast<size_t> (lane)][static_cast<size_t> (step)];

        // The SAME value the grid toggles to — seq::kToggleOnVelocity is pinned
        // to app.js:392, and two editors writing two different "on" velocities
        // would be a groove that changed depending on which one you used.
        slot = static_cast<std::uint8_t> (slot > 0 ? seq::kToggleOffVelocity
                                                   : seq::kToggleOnVelocity);

        handle->dirty = true;
    }

    refreshFromState();
}

void KitOverlay::refreshFromState()
{
    if (processor == nullptr)
        return;

    std::uint32_t generation = 0;
    const auto snapshot = snapshotPattern (*processor, generation);

    const auto& covered = lanesForRow (detail::compositeChannel());

    for (int row = 0; row < covered.size(); ++row)
    {
        const auto lane = covered.entries[static_cast<size_t> (row)];

        for (int step = 0; step < stepCount; ++step)
            if (auto* pad = padFor (row, step))
                pad->setVelocity (static_cast<int> (
                    snapshot.lanes[static_cast<size_t> (lane)][static_cast<size_t> (step)]));
    }

    lastPatternGeneration = generation;
}

void KitOverlay::refreshIfStateChanged()
{
    if (! isVisible() || processor == nullptr)
        return;

    // The STEP WINDOW first, for the grid's reason: it changes how many pads
    // there are, so a velocity refresh against the old count would leave 16 pads
    // showing a 32-step window.
    if (readStepWindow (processor) != stepCount)
    {
        rebuildPads();
        resized();
        refreshFromState();
        return;
    }

    if (processor->getPatternPublicationCount() != lastPatternGeneration)
        refreshFromState();
}

void KitOverlay::resized()
{
    layout = KitOverlayLayout::forBounds (getLocalBounds());

    placePanel();

    // The panel's children are placed against the panel's UNSHIFTED origin: the
    // entrance's translation is the panel component's own position, so applying
    // it here too would apply it twice.
    const auto origin = layout.panel.getPosition();

    closeButton->setBounds (layout.close - origin);

    for (int row = 0; row < static_cast<int> (layout.rows.size()); ++row)
    {
        const auto strip = layout.rows[static_cast<size_t> (row)].pads - origin;

        for (int step = 0; step < stepCount; ++step)
            if (auto* pad = padFor (row, step))
            {
                const auto cell = SequencerLayout::padBounds (strip, step, stepCount);
                pad->setBounds (StepPad::boundsForPadRect (cell));
            }
    }
}

void KitOverlay::mouseUp (const juce::MouseEvent& event)
{
    // Outside the panel is a dismissal — css:557's backdrop click. Inside is
    // not, so a missed pad does not close the thing you were editing in.
    //
    // The panel's own bounds, which already carry the entrance's offset. This
    // used to re-derive the offset here, so the hit-test and the paint each
    // computed the panel's position from `progress` independently.
    if (panel == nullptr || ! panel->getBounds().contains (event.getPosition()))
        setOpen (false);
}

KitOverlay::Panel::Panel (KitOverlay& o) : owner (o)
{
    // The panel takes its own clicks so `KitOverlay::mouseUp` — the dismissal —
    // only ever sees the scrim.
    setInterceptsMouseClicks (true, true);
}

void KitOverlay::Panel::paint (juce::Graphics& g) { owner.paintPanel (g); }

void KitOverlay::paintPanel (juce::Graphics& g)
{
    // ONE transform, so everything below is written in the layout's own
    // coordinates — the same rectangles `KitOverlayLayout::forBounds` produced
    // and the same ones the tests read back — rather than each carrying a
    // `.translated (shift, 0)` that one of them will eventually be missing.
    g.addTransform (juce::AffineTransform::translation (
        static_cast<float> (-layout.panel.getX()), static_cast<float> (-layout.panel.getY())));

    g.setColour (lnf.token (theme::Token::panel));
    g.fillRect (layout.panel);

    g.setColour (lnf.token (theme::Token::lineStrong));
    g.fillRect (layout.border);

    // ── everything below is TEXT, and culled against the clip ───────────────
    //
    // The clip is in the same coordinates as the boxes, because the transform
    // above moved both. While the panel is open the playhead sweeps underneath
    // it sixty times a second, and each move invalidates a narrow strip that
    // intersects this component — so without these guards eleven tracked-text
    // runs were re-shaped for a repaint that could not show any of them: 142 us
    // and 2268 heap allocations per frame, against 18 us for the same strip with
    // the panel shut. The sub-line alone was 69 us of it. Measured by /simplify;
    // it is the guard `SequencerGrid::paintRowLabels` already uses.
    const auto clip = g.getClipBounds();

    // CONSERVATIVE, by one line height above and below each box: `drawTracked`
    // centres its glyphs in the box it is given, and their ink overflows it —
    // clipping tightly around the sub-line and culling the title by its own
    // exact rectangle dropped 51 levels of the title's descenders out of a
    // region that should have shown them. A cull that is too eager is a
    // rendering bug; one that is too shy is only a missed saving.
    const auto reaches = [clip] (juce::Rectangle<int> box)
    {
        return box.expanded (0, box.getHeight()).intersects (clip);
    };

    // ── header: BATERIA in the kit accent, · KIT in the foreground ─────────
    if (reaches (layout.title))
    {
        const auto accentWord = juce::String ("BATERIA");
        const auto rest = juce::String::fromUTF8 (" \xc2\xb7 KIT");

        const auto accentWidth = juce::roundToInt (
            type::trackedWidth (type::Style::kitTitle, accentWord));

        g.setColour (theme::accent (theme::Accent::bateria));
        type::drawTracked (g, type::Style::kitTitle, accentWord,
                           layout.title.toFloat(), juce::Justification::centredLeft);

        g.setColour (lnf.token (theme::Token::fg));
        type::drawTracked (g, type::Style::kitTitle, rest,
                           layout.title.withTrimmedLeft (accentWidth).toFloat(),
                           juce::Justification::centredLeft);
    }

    // ── the sub-line, in Brazilian Portuguese as every instructional string is
    if (reaches (layout.subLine))
    {
        g.setColour (lnf.token (theme::Token::fgFaint));
        type::drawTracked (g, type::Style::kitSubLine, subLineText(),
                           layout.subLine.toFloat(), juce::Justification::centredLeft);
    }

    // ── row labels ──────────────────────────────────────────────────────────
    const auto& covered = lanesForRow (detail::compositeChannel());

    for (int row = 0; row < static_cast<int> (layout.rows.size()) && row < covered.size(); ++row)
    {
        const auto& box = layout.rows[static_cast<size_t> (row)];
        const auto lane = covered.entries[static_cast<size_t> (row)];

        if (! reaches (box.name) && ! reaches (box.full))
            continue;

        g.setColour (theme::subColour (row));
        type::drawTracked (g, type::Style::kitRowName,
                           juce::String (ids::lanes[static_cast<size_t> (lane)]).toUpperCase(),
                           box.name.toFloat(), juce::Justification::centredLeft);

        g.setColour (lnf.token (theme::Token::fgFaint));
        type::drawTracked (g, type::Style::kitRowFull, kitPieceName (row),
                           box.full.toFloat(), juce::Justification::centredLeft);
    }
}

void KitOverlay::paint (juce::Graphics& g)
{
    // ── the scrim ───────────────────────────────────────────────────────────
    //
    // FULL STRENGTH from the first frame, and deliberately not eased with the
    // panel: css:557 switches `.subview` from `display: none` to `display: flex`
    // and declares no transition on it, so the dimmed chassis appears at once
    // and only `.subview-panel` (css:565) slides and fades in over it.
    //
    // NO BLUR. css:556 asks for backdrop-filter: blur(3px), which JUCE has no
    // equivalent for short of capturing the region behind this component and
    // blurring it. Decided with the user at planning: the --bg 78% the same
    // rule specifies does the separation on its own, and a captured blur would
    // be a new rendering technique in a codebase that has deliberately avoided
    // them.
    g.setColour (lnf.token (theme::Token::bg).withAlpha (kit::kScrimOpacity));
    g.fillRect (layout.scrim);

    // ── the panel's drop shadow ─────────────────────────────────────────────
    //
    // The one piece of the panel the PANEL cannot paint: `-20px 0 60px` falls
    // outside its bounds, and a component does not paint past them. So it is
    // drawn here, against the panel's current position, and faded by hand —
    // the only `withAlpha (eased)` left in this file, because it is the only
    // thing the panel's own component alpha does not reach.
    if (panel != nullptr)
        juce::DropShadow (juce::Colours::black.withAlpha (
                              kit::kPanelShadowOpacity
                              * static_cast<float> (cubicBezierEase (progress))),
                          kit::kPanelShadowRadius, { kit::kPanelShadowOffsetX, 0 })
            .drawForRectangle (g, panel->getBounds());
}

} // namespace forrobox
