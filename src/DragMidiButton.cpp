#include "DragMidiButton.h"

namespace forrobox
{

namespace
{
/** U+2193, the arrow app.js:422 puts in `.dm-arrow`.

    A juce::String built from EXPLICIT UTF-8, not a const char*: String's
    const char* constructor goes through CharPointer_ASCII, which is how U+2039
    reached 04-04's reference sheet as "a<EUR>1/2" with every check green. */
const juce::String& arrowGlyph()
{
    static const juce::String text { juce::CharPointer_UTF8 ("↓") };
    return text;
}

} // namespace

DragMidiButton::Metrics DragMidiButton::metrics()
{
    const auto arrow = juce::roundToInt (type::trackedWidth (type::Style::dragMidiArrow,
                                                              arrowGlyph()));
    const auto label = juce::roundToInt (type::trackedWidth (type::Style::dragMidiLabel,
                                                              "DRAG MIDI"));
    const auto sub = juce::roundToInt (type::trackedWidth (type::Style::dragMidiSub, ".mid"));

    // The 1.5 px border is the one fractional length in the design, so it is
    // doubled and rounded ONCE here rather than at each site that needs it.
    const auto border = juce::roundToInt (dragmidi::kBorder * 2.0f);

    const auto content = arrow + dragmidi::kGap + label + dragmidi::kGap + sub;

    // `align-items: center`: the row is as tall as its TALLEST child, and the
    // 16 px arrow is not obviously it. flexRow, not whichever looks biggest.
    const auto contentHeight = flexRow (type::boxHeight (type::Style::dragMidiArrow),
                                        type::boxHeight (type::Style::dragMidiLabel),
                                        type::boxHeight (type::Style::dragMidiSub));

    return { arrow, label, sub,
             content + dragmidi::kPadX * 2 + border,
             contentHeight + dragmidi::kPadY * 2 + border };
}

DragMidiButton::DragMidiButton (ForroBoxLookAndFeel& lookAndFeelToUse)
    : lnf (lookAndFeelToUse),
      box (metrics()),
      // A folder of this instance's own, so two plugins in one project cannot
      // write, overwrite or sweep each other's pending drags. Created lazily by
      // the first export, not here — a plugin nobody drags from leaves nothing
      // behind.
      instanceFolder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getChildFile ("forrobox")
                        .getChildFile (juce::Uuid().toDashedString()))
{
    // `cursor: grab` — css:524. The affordance is the whole point of a call to
    // action, and it is honest now that there is a file behind it.
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);

    // The tick reads the clock; advancePulse is TOLD the interval and never
    // reads one. Surface.h's PollTimer already owns that split.
    // CLAMPED — the overload SidePanel and the three other animation sites all
    // use. A stalled message thread finishes the breath rather than jumping it
    // to an arbitrary phase.
    pulsePoll.tick = [this]
    {
        advancePulse (pulsePoll.secondsSinceLastTick (dragmidi::kPulseSeconds));
    };
    pulsePoll.startTimerHz (kUiPollHz);
}

void DragMidiButton::advancePulse (double seconds) noexcept
{
    // css:534 and css:541 — `animation: none` on hover and on `.hot`. The phase
    // is HELD rather than reset, so releasing resumes the breath where it was
    // instead of snapping it back to the start.
    if (! isPulsing())
        return;

    if (seconds <= 0.0)
        return;

    pulsePhase += seconds / dragmidi::kPulseSeconds;
    pulsePhase -= std::floor (pulsePhase);

    // Only what the breath can reach. The component reserves kGlowMargin (30)
    // for the HOVER glow, but the idle one is kPulseGlowRadius (20), so a bare
    // repaint() dirtied 21,922 px to change 206x77 of them — measured at 82.3 us
    // a frame against 52.2 for the narrower rect.
    repaint (contentBox().expanded (dragmidi::kPulseGlowRadius));
}

double DragMidiButton::pulseAmount() const noexcept
{
    // The keyframes are 0%, 50% and 100% with ease-in-out BETWEEN them, so the
    // breath is the easing applied to a 0 -> 1 -> 0 triangle, not to the phase.
    // Easing the phase directly would give a curve that jumps at the midpoint.
    //
    // Through `keyframeValueAt` since 08-04, which hoisted exactly this shape
    // for the sway and the `♪ NO PONTO` pulse and named two callers in its
    // docstring while THIS one sat here already written out — the third. The
    // hand-rolled form was a correct `easeInOut` of a triangle, and equal to
    // this everywhere, because `cubic-bezier(.42,0,.58,1)` is symmetric about
    // (0.5, 0.5); it also wrapped its phase with `-= std::floor(...)`, which
    // does not survive a negative one. /simplify.
    return keyframeValueAt (pulsePhase * dragmidi::kPulseSeconds, dragmidi::kPulseSeconds,
                            { { 0.0, 0.0f }, { 0.5, 1.0f }, { 1.0, 0.0f } },
                            KeyframeTiming::easeInOut);
}

void DragMidiButton::paint (juce::Graphics& g)
{
    const auto area = contentBox().toFloat();

    if (area.isEmpty())
        return;

    const auto accent = theme::accent (theme::Accent::zabumba);
    const auto panel = lnf.token (theme::Token::panel);
    const auto radius = lnf.cornerRadius (dragmidi::kRadiusExtra);

    // `transform: scale(0.98)` — css:538. On the CONTEXT, around the box's own
    // centre, so the press reads as a push rather than a slide.
    //
    // A transform and not a smaller rectangle: a CSS transform scales the
    // element's TEXT with it, and shrinking the box alone would move the three
    // labels 2% closer together while leaving them at full size. The border and
    // the corner radius scale too, which is also what the browser does.
    juce::Graphics::ScopedSaveState pressState (g);

    if (pressed)
        g.addTransform (juce::AffineTransform::scale (dragmidi::kPressScale,
                                                       dragmidi::kPressScale,
                                                       area.getCentreX(), area.getCentreY()));

    const auto tintPct = hovered ? dragmidi::kHoverTintPct : dragmidi::kTintPct;

    // ── the outer glow, a shadow, so it is drawn first ────────────────────
    //
    // Two rules, and only one of them is on at a time. HOVER is `0 0 30px
    // <accent 55%>` (css:537). At rest the idle animation breathes `0 0 20px
    // <accent 28%>` at the top of the cycle and nothing at the bottom
    // (css:531), which is what `pulseAmount` returns.
    const auto breath = isPulsing() ? pulseAmount() : 0.0;

    // Gated on the alpha that will actually be DRAWN, not on `breath > 0.0`.
    // The bisection in `cubicBezierEase` bottoms out at 2.66e-15 rather than
    // exact zero, so a float comparison was true at every point of the cycle —
    // including the frames whose 8-bit alpha rounds away, which rasterised a
    // 33.9 us shadow nobody can see.
    const auto glowAlpha = juce::roundToInt (breath * dragmidi::kPulseGlowPct / 100.0 * 255.0);

    if (hovered)
    {
        juce::DropShadow (accent.withAlpha (dragmidi::kHoverGlowPct / 100.0f),
                          dragmidi::kHoverGlowRadius, {})
            .drawForRectangle (g, area.toNearestInt());
    }
    else if (glowAlpha > 0)
    {
        // The RADIUS is fixed and the alpha scales: a DropShadow radius is an
        // int, so animating it would step visibly at 30 Hz where the alpha is
        // continuous. The keyframe's `0 0 0 transparent` and `0 0 20px <28%>`
        // differ in both, and alpha is the one the eye reads as breathing.
        juce::DropShadow (accent.withAlpha (static_cast<juce::uint8> (glowAlpha)),
                          dragmidi::kPulseGlowRadius, {})
            .drawForRectangle (g, area.toNearestInt());
    }

    // ── the ring: `0 0 0 Npx <accent>`, a spread with no blur ──────────────
    //
    // Drawn as a stroke OUTSIDE the border box, which is what a zero-blur
    // spread is. Solid accent on hover (css:537), a translucent 18% at rest
    // (css:526).
    {
        const auto ringWidth = hovered ? dragmidi::kHoverRingWidth
                                       : static_cast<float> (dragmidi::kRingWidth);

        // css:531 takes the ring from 18% to 35% across the same cycle — the
        // part PLANNING.md:499's prose leaves out.
        const auto ringPct = dragmidi::kRingPct
                           + breath * (dragmidi::kPulseRingPct - dragmidi::kRingPct);

        g.setColour (hovered ? accent
                             : accent.withAlpha (static_cast<float> (ringPct / 100.0)));
        g.drawRoundedRectangle (area.expanded (ringWidth * 0.5f), radius + ringWidth * 0.5f,
                                ringWidth);
    }

    // ── the ground: `linear-gradient(180deg, <accent at N%> + panel, panel)` ──
    g.setGradientFill (juce::ColourGradient::vertical (
        theme::mix (panel, accent, theme::mixWeight (100.0f - tintPct, tintPct)), area.getY(),
        panel, area.getBottom()));
    g.fillRoundedRectangle (area, radius);

    // `inset 0 1px 0 rgba(255,255,255,0.06)` — css:526, and it survives the
    // hover rule, which restates it.
    g.setColour (juce::Colours::white.withAlpha (dragmidi::kInsetAlpha));
    g.fillRect (area.withHeight (1.0f).reduced (radius * 0.5f, 0.0f));

    // ── the border: 1.5px color-mix(--c-zabumba 45%, --line-strong) ────────
    g.setColour (hovered ? accent
                         : theme::mix (lnf.token (theme::Token::lineStrong), accent,
                                       theme::mixWeight (100.0f - dragmidi::kBorderPct,
                                                          dragmidi::kBorderPct)));
    g.drawRoundedRectangle (area.reduced (dragmidi::kBorder * 0.5f), radius,
                            dragmidi::kBorder);

    // ── the three runs: arrow, label, sub-label ────────────────────────────
    //
    // Laid out left to right inside the padding, each taking its own width —
    // `display: flex; align-items: center; gap: 11px` (css:521).
    auto content = area.reduced (dragmidi::kPadX + dragmidi::kBorder,
                                dragmidi::kPadY + dragmidi::kBorder);

    // Widths taken from the cached metrics, NOT measured again. drawTracked
    // lays the string out itself, so a trackedWidth beside it shapes everything
    // twice — 23% of this method, measured by /simplify.
    //
    // They are the ROUNDED widths, which is the point rather than a side effect:
    // `metrics()` rounds each run to reserve the box, and paint used to advance
    // by the raw float. The two therefore disagreed by a fraction of a pixel per
    // run, accumulating across three. Measured against a build of the previous
    // commit: 223 pixels move, all of them inside this button's 102x9 text row,
    // and what they move to is the position the reserved box was measured for.
    const auto run = [&] (type::Style style, const juce::String& text, juce::Colour colour,
                          int width)
    {
        g.setColour (colour);
        type::drawTracked (g, style, text, content.withWidth (static_cast<float> (width)),
                           juce::Justification::centred);

        content.removeFromLeft (static_cast<float> (width + dragmidi::kGap));
    };

    // `@keyframes midiarrow { 50% { transform: translateY(2px) } }` — css:540,
    // on the SAME 2.6 s cycle as the glow, which is why both read `breath`.
    // Only the arrow moves; the two labels sit still, as the stylesheet scopes
    // the animation to `.dm-arrow`.
    {
        juce::Graphics::ScopedSaveState arrowState (g);
        g.addTransform (juce::AffineTransform::translation (
            0.0f, static_cast<float> (breath * dragmidi::kArrowBobPx)));

        run (type::Style::dragMidiArrow, arrowGlyph(), accent, box.arrowWidth);
    }

    // `color: var(--fg)` on hover (css:535); at rest the label inherits the
    // footer's own `--fg-dim`.
    run (type::Style::dragMidiLabel, "DRAG MIDI",
         lnf.token (hovered ? theme::Token::fg : theme::Token::fgDim), box.labelWidth);

    // The row's OWN opacity, 0.7, which type::styleFor carries so that no call
    // site has to remember it.
    run (type::Style::dragMidiSub, ".mid", accent, box.subWidth);
}

void DragMidiButton::mouseEnter (const juce::MouseEvent&)
{
    hovered = true;
    syncPulseTimer();
    repaint();
}

void DragMidiButton::mouseExit (const juce::MouseEvent&)
{
    hovered = false;
    pressed = false;
    syncPulseTimer();
    repaint();
}

void DragMidiButton::syncPulseTimer()
{
    // A hovered or dragged button does NO work, rather than 30 wake-ups a
    // second to compute a breath css:534 has switched off. KitOverlay's
    // entrance poll is started on open and stopped on close for the same
    // reason.
    if (isPulsing())
    {
        if (! pulsePoll.isTimerRunning())
        {
            // Re-based, so a button hovered for ten minutes reports one frame
            // on its first tick rather than ten minutes.
            pulsePoll.restart();
            pulsePoll.startTimerHz (kUiPollHz);
        }
    }
    else
    {
        pulsePoll.stopTimer();
    }
}

void DragMidiButton::mouseDown (const juce::MouseEvent& e)
{
    // Right-click belongs to the host, as it does on every other control here.
    if (e.mods.isPopupMenu())
        return;

    pressed = true;
    dragging = false;
    gestureTried = false;
    syncPulseTimer();
    repaint();
}

void DragMidiButton::mouseDrag (const juce::MouseEvent& e)
{
    // Once per gesture. performExternalDragDropOfFiles is asynchronous and
    // mouseDrag fires on every movement, so without this the OS would be handed
    // a new drag — and a new file — several times a second.
    if (! pressed || gestureTried)
        return;

    // A THRESHOLD, because mouseDrag is not "the pointer moved". JUCE sends it
    // whenever the pointer STATE changes while a button is held, and that state
    // includes pressure and orientation — so on a trackpad or a pen a perfectly
    // still press still delivers drags. Without this, an ordinary click sets
    // `dragging` and `mouseUp` returns before the save dialog, which would make
    // the click path unreliable on the input devices most people have.
    //
    // JUCE's own default, the one DragAndDropContainer::startDragging uses.
    if (e.getDistanceFromDragStart() < dragmidi::kDragThresholdPx)
        return;

    // ONE attempt per gesture, latched BEFORE the work and independently of
    // whether it succeeds. `dragging` cannot do this job: it stays false when
    // the write fails and is cleared again when the OS refuses the drag, so
    // both failure paths re-ran on the NEXT pointer event of the same gesture —
    // measured at 127.8 us of filesystem work each, ~125 times a second on a
    // held pointer. `dragging` keeps its own meaning: the visual state, and the
    // mouseUp suppression.
    gestureTried = true;

    const auto file = writeExportFile();

    if (file == juce::File())
        return;

    dragging = true;
    syncPulseTimer();
    repaint();

    // STATIC: no DragAndDropContainer instance is needed, and the editor does
    // not become one. canMoveFiles = false because the file is ours — the
    // receiver must copy it, not move it out of our temp folder.
    //
    // The completion callback clears the visual state ONLY. It deliberately does
    // not delete the file; see sweepOldExports for why that is not an oversight.
    const auto started = juce::DragAndDropContainer::performExternalDragDropOfFiles (
        { file.getFullPathName() }, false, this,
        [safe = juce::Component::SafePointer<DragMidiButton> (this)]
        {
            // SafePointer, because this fires after the drag and the editor may
            // have closed inside it — a host can tear the window down mid-drag.
            if (auto* button = safe.getComponent())
            {
                button->dragging = false;
                button->pressed = false;
                button->syncPulseTimer();
                button->repaint();
            }
        });

    // THE RETURN VALUE IS NOT DECORATION. Every platform backend can refuse —
    // no peer for the drag event, or a drag already in flight for this peer —
    // and each returns false WITHOUT ever invoking the callback. Latching
    // `dragging` on a refusal would leave the button stuck in its drag state and
    // swallow this gesture's mouseUp, so the click would do nothing until the
    // user pressed it again.
    if (! started)
    {
        dragging = false;
        syncPulseTimer();
        repaint();
    }
}

void DragMidiButton::mouseUp (const juce::MouseEvent& e)
{
    if (! pressed)
        return;

    pressed = false;
    repaint();

    // A drag is a press, a move and a release, so the release at the end of one
    // arrives here too. Without this the drop would also open a save dialog.
    if (dragging)
        return;

    // And a press that wandered off the button is not a click — the law
    // `isPlainClickInside` states and `HitZone` and `SelectableTile` already
    // call. Without it this was the one control in the footer with no
    // cancel-by-dragging-off, on every path where the drag did not latch.
    if (! isPlainClickInside (e, getLocalBounds()))
        return;

    // `PLANNING.md:505` — "click downloads the same file". A plugin has no
    // browser download, so this is the async save dialog that means the same
    // thing, decided with the user at 07-02 planning.
    //
    // launchAsync, NEVER browseForFileToSave. JUCE_MODAL_LOOPS_PERMITTED=1 is
    // set on the TEST target only and deliberately — CMakeLists.txt says so
    // beside it: "modal loops in a plugin are exactly what the default
    // forbids". A modal call here compiles and deadlocks a host.
    // Never over a live one. See the member's comment: replacing a chooser whose
    // native dialog is still open leaves that dialog pointing at freed memory,
    // and JUCE asserts on it. A modeless save panel plus a second click is all
    // it takes.
    if (chooser != nullptr)
        return;

    const auto groove = askForExport();

    if (groove.bytes.empty())
        return;

    chooser = std::make_unique<juce::FileChooser> (
        "Export MIDI",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
            .getChildFile (groove.filename),
        "*.mid");

    chooser->launchAsync (
        juce::FileBrowserComponent::saveMode
            | juce::FileBrowserComponent::canSelectFiles
            | juce::FileBrowserComponent::warnAboutOverwriting,
        [safe = juce::Component::SafePointer<DragMidiButton> (this),
         bytes = groove.bytes] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();

            // Released here so the next click can open a dialog again — and
            // through a SafePointer, because the editor may have closed while
            // the dialog was up.
            if (auto* button = safe.getComponent())
                button->chooser.reset();

            // An empty result is a dismissed dialog, which must write nothing.
            if (file == juce::File())
                return;

            // The extension is forced rather than assumed: `saveMode` does not
            // add one, so a user who types a bare name would otherwise get a
            // file no DAW recognises.
            const auto target = file.hasFileExtension ("mid") ? file
                                                              : file.withFileExtension ("mid");

            // CHECKED. A read-only folder, a full disk or a file locked by
            // another application all fail here, and without this the dialog
            // simply closes and the user believes the export was written.
            if (! target.replaceWithData (bytes.data(), bytes.size()))
                juce::NativeMessageBox::showAsync (
                    juce::MessageBoxOptions()
                        .withIconType (juce::MessageBoxIconType::WarningIcon)
                        .withTitle ("Export MIDI")
                        .withMessage ("Could not write " + target.getFullPathName()),
                    nullptr);
        });
}

GrooveExport DragMidiButton::askForExport() const
{
    return onExportRequested != nullptr ? onExportRequested() : GrooveExport {};
}

void DragMidiButton::sweepOldExports (const juce::File& folder, const juce::File& keep)
{
    if (! folder.isDirectory())
        return;

    for (const auto& entry : folder.findChildFiles (juce::File::findFiles, false, "*.mid"))
        if (entry != keep)
            entry.deleteFile();
}

juce::File DragMidiButton::writeExportFile()
{
    const auto groove = askForExport();

    if (groove.bytes.empty())
        return {};

    const auto folder = instanceFolder;

    if (! folder.createDirectory())
        return {};

    const auto file = folder.getChildFile (groove.filename);

    if (! file.replaceWithData (groove.bytes.data(), groove.bytes.size()))
        return {};

    // Swept AFTER the new file is written and with it excluded, so the one the
    // OS is about to read is never the one deleted. Scoped to THIS instance's
    // folder, so a second plugin's pending drag is not in range.
    sweepOldExports (folder, file);

    return file;
}

} // namespace forrobox
