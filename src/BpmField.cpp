#include "BpmField.h"

namespace forrobox
{

BpmField::BpmField (ForroBoxLookAndFeel& lookAndFeelToUse)
    : lnf (lookAndFeelToUse),
      screen (lookAndFeelToUse, type::Style::bpmReadout, bpmfield::kMinWidth,
              bpmfield::kPadX, bpmfield::kPadY)
{
    screen.setSuffix (" BPM", type::Style::bpmSuffix);
    screen.setInterceptsMouseClicks (false, false);

    addAndMakeVisible (screen);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void BpmField::resized() { screen.setBounds (getLocalBounds()); }

void BpmField::paint (juce::Graphics&) {}

void BpmField::setValueText (juce::String text) { screen.setText (std::move (text)); }

void BpmField::setReadOnly (bool shouldBeReadOnly)
{
    if (readOnly == shouldBeReadOnly)
        return;

    readOnly = shouldBeReadOnly;

    // Visibly read-only, not silently inert. `cursor: ns-resize` (css:175) is
    // the affordance that says "drag me"; withdrawing it is what tells a user
    // who drags and gets nothing that the control is not broken.
    setMouseCursor (readOnly ? juce::MouseCursor::NormalCursor
                             : juce::MouseCursor::UpDownResizeCursor);
    setAlpha (readOnly ? theme::kReadOnlyAlpha : 1.0f);
    repaint();
}

void BpmField::mouseDown (const juce::MouseEvent& e)
{
    // Right-click belongs to the host, as on every other control here.
    if (readOnly || e.mods.isPopupMenu())
        return;

    // ANCHORED at the press, like the knob and unlike the fader: `wireBPM`
    // captures `sv` here and every move works from it, so a drag out and back
    // lands exactly where it started.
    anchorY = e.getPosition().y;
    gestureActive = true;

    if (onGestureStart != nullptr)
        onGestureStart();
}

void BpmField::mouseDrag (const juce::MouseEvent& e)
{
    // readOnly re-checked, not only at mouse-down: `sync` is an automatable
    // parameter, so a host can turn it on mid-drag. Without this the field
    // dims, withdraws its cursor and switches to the host's tempo while the
    // in-flight gesture keeps writing — a control that looks inert and is not.
    if (! gestureActive || readOnly)
        return;

    if (onDragBy != nullptr)
        onDragBy (anchorY - e.getPosition().y);
}

void BpmField::mouseUp (const juce::MouseEvent&)
{
    if (! gestureActive)
        return;

    gestureActive = false;

    if (onGestureEnd != nullptr)
        onGestureEnd();
}

void BpmField::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    // NOT during a drag. applyNudge sends a COMPLETE gesture, so a notch with
    // the button held would call beginChangeGesture inside the drag's own
    // bracket — which JUCE asserts on in debug and which shows a host
    // begin/begin/end/end. Found by /code-review.
    if (readOnly || gestureActive || onNudge == nullptr)
        return;

    // `-Math.sign(e.deltaY)` — exactly one step per notch whatever the delta's
    // magnitude, and NOT the knob's `max(1, range/50)`, which over 40..300
    // would move five.
    //
    // isReversed honoured, which 04-02's review found the knob ignoring: a
    // natural-scrolling trackpad reports the same gesture with the sign
    // flipped, and a control that ignores the flag moves the wrong way.
    const auto delta = wheel.isReversed ? -wheel.deltaY : wheel.deltaY;

    if (! juce::approximatelyEqual (delta, 0.0f))
        onNudge (delta > 0.0f ? 1 : -1);
}

void BpmField::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (readOnly || e.mods.isPopupMenu())
        return;

    showEditor();
}

void BpmField::showEditor()
{
    // A unique_ptr, not a raw new. Component's destructor removes its children
    // but does NOT delete them — 04-02's review found exactly that leak in the
    // knob's editor.
    editor = std::make_unique<juce::TextEditor>();

    editor->setBounds (getLocalBounds());
    editor->setJustification (juce::Justification::centred);
    editor->setFont (type::fontFor (type::Style::bpmReadout));
    editor->setColour (juce::TextEditor::backgroundColourId, lnf.token (theme::Token::screen));
    editor->setColour (juce::TextEditor::textColourId, lnf.token (theme::Token::screenFg));
    editor->setText (screen.getText(), false);
    editor->selectAll();

    // SafePointer, not a raw `this`. ~BpmField destroys the editor, which gives
    // away keyboard focus, which fires onFocusLost — posting a message that
    // would call editor.reset() on a field the queue outlives. Knob already
    // captures a SafePointer at Knob.cpp:330 for exactly this; this deviated
    // from it. Found by /code-review.
    const auto safeThis = juce::Component::SafePointer<BpmField> (this);

    editor->onEscapeKey = [safeThis]
    {
        juce::MessageManager::callAsync ([safeThis]
                                         { if (auto* f = safeThis.getComponent()) f->editor.reset(); });
    };

    editor->onReturnKey = [safeThis]
    {
        auto* f = safeThis.getComponent();

        if (f == nullptr)
            return;

        const auto typed = f->editor->getText();

        // REJECTED text keeps the editor open so the typist can correct it,
        // which is what Knob.cpp:305-308 does and what this class's own header
        // promised. Closing either way discarded the entry silently.
        if (f->onTextEntered != nullptr && ! f->onTextEntered (typed))
            return;

        // Async, because this runs from inside the editor's own key handler.
        juce::MessageManager::callAsync ([safeThis]
                                         { if (auto* g = safeThis.getComponent()) g->editor.reset(); });
    };

    editor->onFocusLost = [safeThis]
    {
        juce::MessageManager::callAsync ([safeThis]
                                         { if (auto* f = safeThis.getComponent()) f->editor.reset(); });
    };

    addAndMakeVisible (*editor);
    editor->grabKeyboardFocus();
}

} // namespace forrobox
