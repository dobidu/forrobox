#include "Knob.h"

#include "ValueTooltip.h"

namespace forrobox
{

Knob::Knob (ForroBoxLookAndFeel& lookAndFeelToUse, int dialSizePx, Polarity polarityToUse,
            juce::Colour arcColour, juce::String microLabel)
    : lnf (lookAndFeelToUse),
      dialSize (dialSizePx),
      polarity (polarityToUse),
      accentColour (arcColour),
      label (std::move (microLabel))
{
    setWantsKeyboardFocus (true);

    // `cursor: ns-resize` on the dial (css:358). Vertical drag is the gesture.
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

int Knob::preferredHeight (int dialSizePx, bool hasLabel) noexcept
{
    if (! hasLabel)
        return dialSizePx;

    return dialSizePx + knob::kLabelGap + knob::kLabelHeight;
}

float Knob::unitScale() const noexcept
{
    return static_cast<float> (dialSize) / knob::kViewBox;
}

juce::Rectangle<float> Knob::dialBounds() const noexcept
{
    // Centred horizontally, top-aligned: `.fb-knob` is a column flex with
    // align-items center (css:357), and the label sits below.
    const auto size = static_cast<float> (dialSize);

    return { (static_cast<float> (getWidth()) - size) * 0.5f, 0.0f, size, size };
}

juce::Rectangle<int> Knob::labelBounds() const noexcept
{
    if (label.isEmpty())
        return {};

    const auto top = dialSize + knob::kLabelGap;

    return { 0, top, getWidth(), getHeight() - top };
}

void Knob::paintArc (juce::Graphics& g, juce::Rectangle<float> dial,
                     float fromDeg, float toDeg, juce::Colour colour) const
{
    if (std::abs (toDeg - fromDeg) < 1.0e-4f)
        return;   // a zero-extent arc: a bipolar knob sitting exactly at centre

    const auto scale  = unitScale();
    const auto centre = dial.getCentre();
    const auto radius = knob::kArcRadius * scale;

    juce::Path path;

    // JUCE's addCentredArc takes radians with 0 at twelve o'clock, increasing
    // clockwise — the same convention controls.js uses once its own -90 deg
    // offset is applied, so the spec's degrees map straight across with no
    // second transform to get wrong.
    path.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                        juce::degreesToRadians (juce::jmin (fromDeg, toDeg)),
                        juce::degreesToRadians (juce::jmax (fromDeg, toDeg)),
                        true);

    g.setColour (colour);
    g.strokePath (path, juce::PathStrokeType (knob::kArcStroke * scale,
                                              juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
}

void Knob::paint (juce::Graphics& g)
{
    const auto dial   = dialBounds();
    const auto scale  = unitScale();
    const auto centre = dial.getCentre();

    // Painted in the order controls.js appends the SVG children (:70):
    // track, hub, value arc, line. The value arc must come after the track so
    // it covers it, and the line after the hub so it sits on top.

    // ── track arc: the full sweep, always visible (css:360) ────────────────
    paintArc (g, dial, knob::kSweepStartDeg, knob::kSweepEndDeg,
              lnf.token (theme::Token::lineStrong));

    // ── hub (css:362) ──────────────────────────────────────────────────────
    {
        const auto hubRadius = knob::kHubRadius * scale;
        const auto hub = juce::Rectangle<float> (hubRadius * 2.0f, hubRadius * 2.0f)
                             .withCentre (centre);

        g.setColour (lnf.token (theme::Token::hub));
        g.fillEllipse (hub);

        // The focus ring IS the hub stroke turning --active (css:359) — not an
        // extra ring drawn around the knob.
        g.setColour (showFocusRing ? lnf.token (theme::Token::active)
                                   : lnf.token (theme::Token::line));
        g.drawEllipse (hub, knob::kHubStroke * scale);
    }

    // ── value arc ──────────────────────────────────────────────────────────
    {
        const auto valueDeg = knob::angleForProportion (proportion);

        // Unipolar grows from the sweep's start; bipolar from its centre, in
        // either direction. Polarity changes the ARC only — the indicator line
        // below points to the same angle in both.
        const auto fromDeg = (polarity == Polarity::bipolar) ? knob::kCentreDeg
                                                             : knob::kSweepStartDeg;

        paintArc (g, dial, fromDeg, valueDeg,
                  theme::saturated (accentColour, lnf.accentIntensity(),
                                    knob::kSaturationFloor, knob::kSaturationRange));
    }

    // ── indicator line: the knob's identity (css:363) ──────────────────────
    {
        const auto tipLength = (knob::kViewBox * 0.5f - knob::kIndicatorTipY) * scale;
        const auto radians   = juce::degreesToRadians (knob::angleForProportion (proportion));

        // Rotated about the centre, exactly as controls.js rotates the line
        // element (`rotate(ang 50 50)`, :112).
        const juce::Point<float> tip { centre.x + std::sin (radians) * tipLength,
                                       centre.y - std::cos (radians) * tipLength };

        g.setColour (lnf.token (theme::Token::fg));
        g.drawLine ({ centre, tip }, knob::kIndicatorStroke * scale);
    }

    // ── micro-label (css:364) ──────────────────────────────────────────────
    if (label.isNotEmpty())
    {
        g.setColour (lnf.token (theme::Token::fgFaint));
        type::drawTracked (g, type::Style::knobMicroLabel, label,
                           labelBounds().toFloat(), juce::Justification::centred);
    }
}

void Knob::setProportion (float newProportion)
{
    const auto clamped = juce::jlimit (0.0f, 1.0f, newProportion);

    if (! juce::approximatelyEqual (clamped, proportion))
    {
        proportion = clamped;
        repaint();
    }
}

// ── gestures ────────────────────────────────────────────────────────────────
//
// Every law here is controls.js's, which outranks PLANNING.md's prose where
// the two differ (and they do — PLANNING.md:368 says only "Scroll wheel:
// increment; Shift for fine steps"). The numbers are quoted at each site.

namespace
{
/** `(dy / 160) * range` — controls.js:138. Full range is ~160 px of travel.

    Applied in NORMALISED space, where the range cancels: dv/range == dy/160.
    That is exactly equivalent for the linear ranges the prototype has, and it
    is the better behaviour for any skewed one, since travel stays uniform
    under the pointer instead of bunching at one end. */
constexpr float kPixelsForFullTravel = 160.0f;

/** `e.shiftKey ? 0.18 : 1` — controls.js:136. */
constexpr float kShiftDragFactor = 0.18f;
} // namespace

void Knob::mouseDown (const juce::MouseEvent& e)
{
    // Right-click is NOT consumed. PLANNING.md:370's interaction table says it
    // resets, and PLANNING.md:876-878 overrides that for a plugin: "ensure this
    // doesn't collide with the host's parameter context menu (or move reset to
    // Alt+click ...  which is the DAW convention)". So it falls through to the
    // host, and reset is Alt+click below.
    if (e.mods.isPopupMenu())
        return;

    if (e.mods.isAltDown())
    {
        if (onReset != nullptr)
            onReset();

        showTooltip ("reset");
        return;
    }

    dragStartProportion = proportion;
    dragStartY = e.getMouseDownY();

    if (onGestureStart != nullptr)
        onGestureStart();

    showTooltip();
}

void Knob::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || e.mods.isAltDown())
        return;

    // Anchored at mouse-down, computed from the TOTAL delta — see the members'
    // documentation for why this is not incremental.
    const auto dy = static_cast<float> (dragStartY - e.y);
    const auto fine = e.mods.isShiftDown() ? kShiftDragFactor : 1.0f;
    const auto target = dragStartProportion + (dy / kPixelsForFullTravel) * fine;

    if (onDragTo != nullptr)
        onDragTo (juce::jlimit (0.0f, 1.0f, target));

    showTooltip();
}

void Knob::mouseUp (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

    if (onGestureEnd != nullptr)
        onGestureEnd();

    if (! isMouseOver (true))
        hideTooltip();
}

void Knob::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (onNudge == nullptr)
        return;

    const auto direction = wheel.deltaY > 0.0f ? 1 : (wheel.deltaY < 0.0f ? -1 : 0);

    if (direction == 0)
        return;

    // The attachment owns the magnitude, because it depends on the parameter's
    // range and interval. Shift asks for the finest move there is.
    onNudge (direction, juce::ModifierKeys::currentModifiers.isShiftDown());

    showTooltip();
}

bool Knob::keyPressed (const juce::KeyPress& key)
{
    if (onNudge == nullptr)
        return false;

    // `ArrowUp || ArrowRight` adds one step, `ArrowDown || ArrowLeft` subtracts
    // — controls.js:184-185. One step, not the wheel's coarse multiple.
    if (key == juce::KeyPress::upKey || key == juce::KeyPress::rightKey)
    {
        onNudge (1, true);
        showTooltip();
        return true;
    }

    if (key == juce::KeyPress::downKey || key == juce::KeyPress::leftKey)
    {
        onNudge (-1, true);
        showTooltip();
        return true;
    }

    return false;
}

void Knob::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onTextEntered == nullptr)
        return;

    // PLANNING.md:369 states the deviation itself: the prototype uses prompt(),
    // "a plugin should show an inline text editor". So this is spec-directed.
    auto* editor = new juce::TextEditor();

    editor->setText (getDisplayText != nullptr ? getDisplayText() : juce::String());
    editor->selectAll();
    editor->setBounds (dialBounds().toNearestInt());
    editor->setJustification (juce::Justification::centred);
    editor->setFont (type::fontFor (type::Face::monoRegular,
                                    type::textStyles[static_cast<size_t> (type::Style::tooltip)].heightPx));

    editor->onReturnKey = [this, editor]
    {
        if (onTextEntered != nullptr)
            onTextEntered (editor->getText());

        removeChildComponent (editor);
        delete editor;
    };

    editor->onEscapeKey = [this, editor]
    {
        removeChildComponent (editor);
        delete editor;
    };

    editor->onFocusLost = [this, editor]
    {
        removeChildComponent (editor);
        delete editor;
    };

    addAndMakeVisible (editor);
    editor->grabKeyboardFocus();
}

void Knob::mouseEnter (const juce::MouseEvent&)
{
    showTooltip();
}

void Knob::mouseExit (const juce::MouseEvent&)
{
    if (! isMouseButtonDown())
        hideTooltip();
}

void Knob::focusGained (FocusChangeType)
{
    setShowingFocusRing (true);
}

void Knob::focusLost (FocusChangeType)
{
    setShowingFocusRing (false);
}

void Knob::showTooltip (const juce::String& overrideText)
{
    if (tooltip == nullptr)
        return;

    const auto text = overrideText.isNotEmpty() ? overrideText
                    : (getDisplayText != nullptr ? getDisplayText() : juce::String());

    if (text.isEmpty())
        return;

    // The anchor is this knob's dial in the TOOLTIP's parent coordinates, so a
    // knob nested three components deep still positions correctly.
    if (auto* parent = tooltip->getParentComponent())
        tooltip->showFor (text, parent->getLocalArea (this, dialBounds()).toNearestInt());
}

void Knob::hideTooltip()
{
    if (tooltip != nullptr)
        tooltip->hide();
}

void Knob::setShowingFocusRing (bool shouldShow)
{
    if (showFocusRing != shouldShow)
    {
        showFocusRing = shouldShow;
        repaint();
    }
}

} // namespace forrobox
