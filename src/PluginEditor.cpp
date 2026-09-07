#include "PluginEditor.h"

ForroBoxAudioProcessorEditor::ForroBoxAudioProcessorEditor (ForroBoxAudioProcessor& p)
    : juce::AudioProcessorEditor (&p)
{
    setResizable (true, true);

    // setResizeLimits installs the default constrainer, so it must come before
    // getConstrainer() is touched — and both must come before setSize, or the
    // first bounds escape the aspect lock.
    setResizeLimits (kMinWidth, kMinHeight, kMaxWidth, kMaxHeight);

    // Named to avoid shadowing AudioProcessorEditor's own member (-Wshadow).
    if (auto* bounds = getConstrainer())
        bounds->setFixedAspectRatio ((double) kDesignWidth / (double) kDesignHeight);

    setSize (kDesignWidth, kDesignHeight);
}

void ForroBoxAudioProcessorEditor::paint (juce::Graphics& g)
{
    // --bg chassis token. Placeholder text only: a host load should be
    // visually unmistakable without pre-building Phase 4's chrome.
    g.fillAll (juce::Colour (0xff141414));

    g.setColour (juce::Colour (0xffe8e8e8));
    g.setFont (juce::FontOptions (28.0f));
    g.drawFittedText ("FORRO BOX", getLocalBounds(), juce::Justification::centred, 1);
}

void ForroBoxAudioProcessorEditor::resized()
{
    // Phase 4: scale transform on a fixed 1200×780 child component, so all
    // layout maths stays in design px.
}
