#include "ValueScreen.h"

namespace forrobox
{

ValueScreen::ValueScreen (ForroBoxLookAndFeel& lookAndFeelToUse, type::Style styleToUse,
                          int minWidthToUse, int padXToUse, int padYToUse)
    : lnf (lookAndFeelToUse),
      style (styleToUse),
      minWidth (minWidthToUse),
      padX (padXToUse),
      padY (padYToUse)
{
}

int ValueScreen::preferredWidth() const
{
    const auto content = type::trackedWidth (style, text)
                       + (suffix.isNotEmpty() ? type::trackedWidth (suffixStyle, suffix) : 0.0f);

    return juce::jmax (minWidth,
                       juce::roundToInt (content) + padX * 2 + kBorderWidth * 2);
}

int ValueScreen::preferredHeight() const
{
    return static_cast<int> (type::styleFor (style).heightPx + 0.5f) + padY * 2 + kBorderWidth * 2;
}

void ValueScreen::setText (juce::String newText)
{
    if (text != newText)
    {
        text = std::move (newText);
        repaint();
    }
}

void ValueScreen::setSuffix (juce::String newSuffix, type::Style newStyle)
{
    suffix = std::move (newSuffix);
    suffixStyle = newStyle;
    repaint();
}

void ValueScreen::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced (kBorderWidth * 0.5f);
    const auto radius = lnf.cornerRadius();

    g.setColour (lnf.token (theme::Token::screen));
    g.fillRoundedRectangle (area, radius);

    g.setColour (lnf.token (theme::Token::line));
    g.drawRoundedRectangle (area, radius, static_cast<float> (kBorderWidth));

    // `text-shadow: 0 0 8px color-mix(in srgb, var(--screen-fg) 30%, transparent)`
    // — css:597. A real blur, for the reason Chassis records about the accent
    // bar: a stack of offset copies at low alpha renders as a hard-edged
    // outline rather than a glow.
    //
    // Drawn as a shadow of the TEXT, so it follows the glyphs. The text is laid
    // out once into an image-free path via drawTracked, so the glow is produced
    // by drawing the same run first, blurred, in the glow colour.
    const auto valueWidth = type::trackedWidth (style, text);
    const auto suffixWidth = suffix.isNotEmpty() ? type::trackedWidth (suffixStyle, suffix) : 0.0f;

    // `text-align: center` over the whole run, value and suffix together.
    const auto runLeft = area.getCentreX() - (valueWidth + suffixWidth) * 0.5f;

    const auto valueBox = juce::Rectangle<float> (runLeft, area.getY(), valueWidth,
                                                  area.getHeight());
    const auto suffixBox = juce::Rectangle<float> (runLeft + valueWidth, area.getY(), suffixWidth,
                                                   area.getHeight());

    const auto drawRun = [&] (juce::Colour colour, float suffixAlpha)
    {
        g.setColour (colour);
        type::drawTracked (g, style, text, valueBox, juce::Justification::centredLeft);

        if (suffix.isNotEmpty())
        {
            g.setColour (colour.withMultipliedAlpha (suffixAlpha));
            type::drawTracked (g, suffixStyle, suffix, suffixBox, juce::Justification::centredLeft);
        }
    };

    const auto screenFg = lnf.token (theme::Token::screenFg);
    const auto suffixAlpha = type::styleFor (type::Style::bpmSuffix).opacity;

    // The glow: the same run, blurred, composited UNDER the text.
    //
    // juce::DropShadow blurs a rectangle, not glyphs, so it cannot do this one
    // — the shadow has to follow the letter shapes. The run is drawn into an
    // offscreen ARGB image, Gaussian-blurred, and composited at the declared
    // opacity. Done once here rather than by each of the three callers.
    {
        juce::Image glowLayer (juce::Image::ARGB, juce::jmax (1, getWidth()),
                               juce::jmax (1, getHeight()), true);

        {
            juce::Graphics glowGraphics (glowLayer);

            glowGraphics.setColour (screenFg);
            type::drawTracked (glowGraphics, style, text, valueBox,
                               juce::Justification::centredLeft);

            if (suffix.isNotEmpty())
            {
                glowGraphics.setColour (screenFg.withMultipliedAlpha (suffixAlpha));
                type::drawTracked (glowGraphics, suffixStyle, suffix, suffixBox,
                                   juce::Justification::centredLeft);
            }
        }

        // An odd kernel size, because ImageConvolutionKernel requires one — and
        // the CSS radius is a blur RADIUS, which is the kernel's half-width.
        juce::ImageConvolutionKernel blur (juce::roundToInt (theme::kScreenGlowRadius) | 1);
        blur.createGaussianBlur (theme::kScreenGlowRadius * 0.5f);
        blur.applyToImage (glowLayer, glowLayer, glowLayer.getBounds());

        g.setOpacity (theme::kScreenGlowOpacity);
        g.drawImageAt (glowLayer, 0, 0);
        g.setOpacity (1.0f);
    }

    drawRun (screenFg, suffixAlpha);
}

} // namespace forrobox
