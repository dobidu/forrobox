/* ============================================================================
   FORRÓ BOX — plugin editor

   Geometry and scaling only. The chassis is a fixed 1200×780 child and this
   applies one scale transform to it, so every layout number anywhere below is a
   design pixel.
============================================================================ */
#pragma once

#include <JuceHeader.h>

#include "Chassis.h"
#include "ValueTooltip.h"
#include "LookAndFeel.h"
#include "PluginProcessor.h"

class ForroBoxAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    // The fixed design geometry every later layout measurement is expressed in.
    //
    // Taken FROM the chassis rather than restated and then guarded by a
    // static_assert: the assert made the duplication safe but 1200 and 780 were
    // still written twice, and the resize limits below were hand-multiplied off
    // them. The aspect constrainer in resized() derives its ratio from these,
    // so a design size changed in one place and not the other would clamp the
    // window to a shape the constrainer then corrects.
    static constexpr int kDesignWidth  = forrobox::ChassisLayout::kWidth;
    static constexpr int kDesignHeight = forrobox::ChassisLayout::kHeight;

    static constexpr float kMinScale = 0.7f;   // still legible
    static constexpr float kMaxScale = 2.0f;

    static constexpr int kMinWidth  = static_cast<int> (kDesignWidth  * kMinScale + 0.5f);
    static constexpr int kMinHeight = static_cast<int> (kDesignHeight * kMinScale + 0.5f);
    static constexpr int kMaxWidth  = static_cast<int> (kDesignWidth  * kMaxScale + 0.5f);
    static constexpr int kMaxHeight = static_cast<int> (kDesignHeight * kMaxScale + 0.5f);

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

    // Declared before everything that holds a reference to it: members are
    // initialised in declaration order, so the look-and-feel must come first or
    // the tooltip and the chassis would bind references to an object that does
    // not exist yet.
    forrobox::ForroBoxLookAndFeel lookAndFeel;

    // The one value tooltip for the whole editor — controls.js has a module
    // singleton, not one per control. Owned HERE rather than by the chassis so
    // it sits outside the scale transform: a tooltip inside it would render its
    // 11 px mono at 22 px at 2x.
    forrobox::ValueTooltip valueTooltip { lookAndFeel };

    forrobox::Chassis chassis { lookAndFeel };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ForroBoxAudioProcessorEditor)
};
