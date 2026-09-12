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
    setAlpha (readOnly ? 0.55f : 1.0f);
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
    if (! gestureActive)
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
    if (readOnly || onNudge == nullptr)
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

    editor->onEscapeKey = [this] { juce::MessageManager::callAsync ([this] { editor.reset(); }); };

    editor->onReturnKey = [this]
    {
        const auto typed = editor->getText();

        if (onTextEntered != nullptr)
            onTextEntered (typed);

        // Async, because this runs from inside the editor's own key handler.
        juce::MessageManager::callAsync ([this] { editor.reset(); });
    };

    editor->onFocusLost = [this]
    { juce::MessageManager::callAsync ([this] { editor.reset(); }); };

    addAndMakeVisible (*editor);
    editor->grabKeyboardFocus();
}

} // namespace forrobox
