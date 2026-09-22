#include "PluginEditor.h"

ForroBoxAudioProcessorEditor::ForroBoxAudioProcessorEditor (ForroBoxAudioProcessor& p)
    : juce::AudioProcessorEditor (&p)
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (chassis);

    // The knobs are children of the CHASSIS so the scale transform reaches
    // them; the tooltip is a child of the EDITOR so it does not scale, and it
    // goes on top of everything.
    chassis.attachParameters (p.getAPVTS(), &valueTooltip);

    // The user's stored theme, corner radius and accent intensity, BEFORE the
    // first paint. This is the product's entry point and the owner of the
    // LookAndFeel, so it is where a global preference is applied — doing it
    // inside Chassis would override whatever a caller had deliberately set.
    // Applying it after the first paint would open every editor in the default
    // look and snap to the stored one a frame later, which reads as a glitch.
    chassis.applyStoredSettings();
    addAndMakeVisible (valueTooltip);

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

ForroBoxAudioProcessorEditor::~ForroBoxAudioProcessorEditor()
{
    // Required, not tidiness: JUCE asserts if a LookAndFeel is destroyed while
    // still set on a component.
    setLookAndFeel (nullptr);
}

float ForroBoxAudioProcessorEditor::getChassisScale() const noexcept
{
    return static_cast<float> (getWidth()) / static_cast<float> (kDesignWidth);
}

void ForroBoxAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Only visible in the letterboxed sliver when the host gives a size the
    // aspect constraint could not fully honour. The chassis covers the rest.
    g.fillAll (lookAndFeel.token (forrobox::theme::Token::bg));
}

void ForroBoxAudioProcessorEditor::resized()
{
    // The chassis keeps its design size in its OWN coordinates and is scaled by
    // one transform. This is what lets every later layout number be a design
    // pixel: nothing below has to know the host's window size.
    //
    // Width alone drives the scale rather than jmin(width/1200, height/780):
    // the constrainer already holds the 20:13 ratio, so the two agree, and
    // taking the minimum would silently absorb a broken aspect ratio instead of
    // making it visible as letterboxing.
    chassis.setTransform (juce::AffineTransform::scale (getChassisScale()));
    chassis.setBounds (0, 0, kDesignWidth, kDesignHeight);
}
