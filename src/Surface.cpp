#include "Surface.h"

namespace forrobox::surface
{

void raisedHighlight (juce::Graphics& g, juce::Rectangle<int> area, juce::Colour highlight)
{
    g.setColour (highlight);
    g.fillRect (area.getX(), area.getY(), area.getWidth(), 1);
}

void wellShadow (juce::Graphics& g, juce::Rectangle<int> area, juce::Colour shadow, float depth)
{
    const auto height = juce::jmin (static_cast<float> (area.getHeight()), depth * 3.0f);

    g.setGradientFill (juce::ColourGradient::vertical (shadow, static_cast<float> (area.getY()),
                                                       shadow.withAlpha (0.0f),
                                                       static_cast<float> (area.getY()) + height));
    g.fillRect (area.withHeight (juce::roundToInt (height)));
}

} // namespace forrobox::surface
