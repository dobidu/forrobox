#include "BpmAttachment.h"

#include <cmath>

namespace forrobox
{

BpmAttachment::BpmAttachment (juce::RangedAudioParameter& parameterToUse, BpmField& fieldToUse)
    : parameter (parameterToUse),
      field (&fieldToUse),
      attachment (parameterToUse, [this] (float) { refreshText(); })
{
    fieldToUse.onGestureStart = [this]
    {
        // Captured ONCE per gesture. The law is `sv + pixels * 0.5` where `sv`
        // is the value at the press, so re-reading the parameter each move
        // would compound the integer rounding and a drag out and back would
        // land somewhere else.
        anchorValue = parameter.convertFrom0to1 (parameter.getValue());
        attachment.beginGesture();
    };

    fieldToUse.onGestureEnd = [this] { attachment.endGesture(); };

    fieldToUse.onDragBy = [this] (int pixelsUp) { applyDrag (pixelsUp); };
    fieldToUse.onNudge  = [this] (int direction) { applyNudge (direction); };

    fieldToUse.onTextEntered = [this] (const juce::String& text)
    {
        // The TEXT is validated, not the parsed number. getValueForText bottoms
        // out in String::getIntValue, which returns 0 for anything
        // unparseable — so an isfinite() guard accepts "hello" and slams the
        // tempo to 40 while reporting success. 04-02's review found exactly
        // this on the knob.
        if (! text.containsAnyOf ("0123456789"))
            return false;

        const auto normalised = parameter.getValueForText (text.trim());

        if (! std::isfinite (normalised))
            return false;

        attachment.setValueAsCompleteGesture (parameter.convertFrom0to1 (normalised));
        return true;
    };

    attachment.sendInitialUpdate();
}

BpmAttachment::~BpmAttachment()
{
    // Through the SafePointer, so a field that died first is gone rather than
    // written to.
    if (auto* f = field.getComponent())
    {
        f->onGestureStart = nullptr;
        f->onGestureEnd = nullptr;
        f->onDragBy = nullptr;
        f->onNudge = nullptr;
        f->onTextEntered = nullptr;
    }
}

void BpmAttachment::applyDrag (int pixelsUp)
{
    const auto& range = parameter.getNormalisableRange();

    const auto target = anchorValue + static_cast<float> (pixelsUp) * bpmfield::kBpmPerPixel;

    // snapToLegalValue clamps AND quantises — the parameter is an
    // AudioParameterInt, so its interval is 1 and the rounding app.js does with
    // Math.round is the range's own job here rather than a second expression of
    // it.
    attachment.setValueAsPartOfGesture (range.snapToLegalValue (target));
}

void BpmAttachment::applyNudge (int direction)
{
    const auto& range = parameter.getNormalisableRange();
    const auto current = parameter.convertFrom0to1 (parameter.getValue());

    // ONE interval per notch — for this parameter that is 1 BPM, which is what
    // `Math.sign` gives in app.js:143.
    jassert (range.interval > 0.0f);

    attachment.setValueAsCompleteGesture (
        range.snapToLegalValue (current + static_cast<float> (direction) * range.interval));
}

void BpmAttachment::scaleBy (float factor)
{
    // Refused while synced, for the same reason every other gesture is: the
    // host owns the tempo then.
    if (synced)
        return;

    const auto& range = parameter.getNormalisableRange();
    const auto current = parameter.convertFrom0to1 (parameter.getValue());

    // snapToLegalValue clamps AND quantises, so "clamped to range"
    // (PLANNING.md:399) is the range's own job rather than a jlimit here that
    // would be a second expression of it.
    attachment.setValueAsCompleteGesture (range.snapToLegalValue (current * factor));
}

void BpmAttachment::setSyncedToHost (bool syncedToHost, float hostBpm)
{
    const auto changed = synced != syncedToHost
                      || ! juce::approximatelyEqual (hostTempo, hostBpm);

    synced = syncedToHost;
    hostTempo = hostBpm;

    if (auto* f = field.getComponent())
        f->setReadOnly (synced);

    if (changed)
        refreshText();
}

void BpmAttachment::refreshText()
{
    auto* f = field.getComponent();

    if (f == nullptr)
        return;

    // Under SYNC the field shows what the HOST reports, not the parameter —
    // PLANNING.md:400 and its stub table at :838. A hostTempo of 0 means the
    // host reported none, so the parameter's own value is the honest thing to
    // show rather than a zero.
    if (synced && hostTempo > 0.0f)
    {
        f->setValueText (juce::String (juce::roundToInt (hostTempo)));
        return;
    }

    f->setValueText (parameter.getCurrentValueAsText());
}

} // namespace forrobox
