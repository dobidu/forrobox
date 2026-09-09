/* ============================================================================
   FORRÓ BOX — plugin editor

   Geometry and scaling only. The chassis is a fixed 1200×780 child and this
   applies one scale transform to it, so every layout number anywhere below is a
   design pixel.
============================================================================ */
#pragma once

#include <JuceHeader.h>

#include "Chassis.h"
#include "LookAndFeel.h"
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

    static_assert (kDesignWidth == forrobox::ChassisLayout::kWidth
                && kDesignHeight == forrobox::ChassisLayout::kHeight,
                   "The editor's design size and the chassis' must be the same number, or the "
                   "scale transform below is computed against the wrong denominator and every "
                   "region lands slightly off with nothing failing.");

    explicit ForroBoxAudioProcessorEditor (ForroBoxAudioProcessor&);
    ~ForroBoxAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** The scale currently applied to the chassis. 1.0 at the design size. */
    float getChassisScale() const noexcept;

private:
    // No processor reference is stored. AudioProcessorEditor already keeps one;
    // phases that need the derived type should cast getAudioProcessor() at the
    // point of use rather than carrying a second reference that must be kept in
    // step with the base class's own bookkeeping.

    // Declared before the chassis: the chassis holds a reference to it, so it
    // must outlive it, and member destruction runs in reverse declaration order.
    forrobox::ForroBoxLookAndFeel lookAndFeel;
    forrobox::Chassis chassis { lookAndFeel };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ForroBoxAudioProcessorEditor)
};
