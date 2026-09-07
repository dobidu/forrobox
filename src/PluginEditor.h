/* ============================================================================
   FORRÓ BOX — plugin editor

   Geometry only. The chassis is Phase 4; this holds the 1200×780 / 20:13
   design frame so the plugin's window shape is settled before any chrome
   is drawn against it.
============================================================================ */
#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class ForroBoxAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    // The fixed design geometry every later layout measurement is expressed in.
    static constexpr int kDesignWidth  = 1200;
    static constexpr int kDesignHeight = 780;
    static constexpr int kMinWidth     = 840;   // 0.7× — still legible
    static constexpr int kMinHeight    = 546;
    static constexpr int kMaxWidth     = 2400;  // 2×
    static constexpr int kMaxHeight    = 1560;

    explicit ForroBoxAudioProcessorEditor (ForroBoxAudioProcessor&);
    ~ForroBoxAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // No processor reference is stored. AudioProcessorEditor already keeps one;
    // phases that need the derived type should cast getAudioProcessor() at the
    // point of use rather than carrying a second reference that must be kept in
    // step with the base class's own bookkeeping.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ForroBoxAudioProcessorEditor)
};
