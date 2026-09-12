/* ============================================================================
   FORRÓ BOX — ProportionAttachment

   The half that every parameter-bound control in this plugin shares: a
   `juce::RangedAudioParameter` on one side, a component that SHOWS a 0..1
   proportion and DRAGS to a new one on the other, and the host's gesture
   bracket around each drag.

   Extracted when the fader arrived rather than copied into it. The fader's
   entire attachment surface is this and nothing else — `controls.js:231-247`
   gives it no wheel, no reset and no typed entry — so a FaderAttachment would
   have been this file with the comments reworded, and two copies of a binding
   law is the shape that produced 03-03's `dry = 1 - 0.5 * wet` and 04-01's
   tracking written twice.

   KnobAttachment keeps everything this does not cover: the wheel's interval
   arithmetic, the reset target and the typed-text validation. Those are the
   knob's, and pushing them down here would give the fader three callbacks it
   has no way to fire.

   A template rather than a base class with virtuals: the two controls share no
   inheritance and need none — what they share is a SHAPE (`setProportion`,
   `onDragTo`, `onGestureStart`, `onGestureEnd`), and a compile error naming the
   missing member is a better diagnostic than a pure virtual nobody overrode.

   The control is held as a `juce::Component::SafePointer`, so the destructor
   cannot write into a freed control. There is NO declaration order that is safe
   on both paths: attachments-last is right for destruction, which runs in
   reverse, and wrong for assignment, which does not. `Chassis` hit exactly that
   — AddressSanitizer named it at `ToggleAttachment.cpp`, through
   `StripControls::operator=` — and answered it by resetting three attachments
   by hand before clearing, which is a rule the owner has to remember and a
   fourth attachment would silently break. A weak reference makes the question
   moot instead of answering it once. Found by /simplify.
============================================================================ */
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace forrobox
{

template <typename Control>
class ProportionAttachment
{
public:
    ProportionAttachment (juce::RangedAudioParameter& parameterToUse, Control& controlToUse)
        : parameter (parameterToUse),
          control (&controlToUse),
          attachment (parameterToUse,
                      [this] (float newDenormalisedValue)
                      {
                          // Parameter -> control. The ONLY writer of the
                          // displayed position, and it never writes back: a
                          // repaint must not become a parameter change, or host
                          // automation would fight itself.
                          if (auto* c = control.getComponent())
                              c->setProportion (parameter.convertTo0to1 (newDenormalisedValue));
                      })
    {
        controlToUse.onDragTo = [this] (float targetProportion)
        {
            attachment.setValueAsPartOfGesture (parameter.convertFrom0to1 (targetProportion));
        };

        controlToUse.onGestureStart = [this] { attachment.beginGesture(); };
        controlToUse.onGestureEnd   = [this] { attachment.endGesture(); };
    }

    ~ProportionAttachment()
    {
        // Every callback above captures `this`, so leaving them installed on a
        // control that outlives the attachment turns the next mouse event into
        // a use-after-free. Clearing them here makes the class answer for
        // itself — and through the SafePointer, so a control that died FIRST
        // is simply gone rather than written to.
        if (auto* c = control.getComponent())
        {
            c->onDragTo = nullptr;
            c->onGestureStart = nullptr;
            c->onGestureEnd = nullptr;
        }
    }

    /** Pushes the parameter's current value at the control. Called by the OWNER
        once its own extra callbacks are installed — a subclass-style attachment
        that let this fire from the constructor would send its initial update
        before its own wiring existed. */
    void sendInitialUpdate() { attachment.sendInitialUpdate(); }

    juce::RangedAudioParameter& getParameter() const noexcept { return parameter; }

    /** For the value changes this class does not model — a reset, a wheel
        notch, a typed value — each of which is one COMPLETE gesture of its own
        rather than part of a drag. */
    juce::ParameterAttachment& getAttachment() noexcept { return attachment; }

private:
    juce::RangedAudioParameter&           parameter;
    juce::Component::SafePointer<Control>  control;

    /** Declared LAST: its constructor takes a callback that touches the two
        references above, and it sends its initial update immediately. */
    juce::ParameterAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProportionAttachment)
};

} // namespace forrobox
