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

int ValueScreen::heightOf (type::Style style, int padY)
{
    return type::boxHeight (style, padY, kBorderWidth);
}

int ValueScreen::preferredHeight() const { return heightOf (style, padY); }

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
    // — css:597, on exactly three elements and all of them this component's.
    //
    // The glyph OUTLINES, laid out ONCE and used twice: blurred for the glow,
    // then filled for the text. juce::DropShadow::drawForPath
    // (juce_DropShadowEffect.h:56) blurs a path's shape, so it follows the
    // letters — an earlier comment here claimed DropShadow "blurs a rectangle,
    // not glyphs, so it cannot do this one" and hand-rolled an offscreen image
    // plus an ImageConvolutionKernel on that basis. It was simply wrong:
    // measured 170 us against 34 us per screen, with an image allocation per
    // paint. Found by /simplify.
    //
    // Six layout passes became two for the same reason: trackedWidth twice for
    // positioning, then drawTracked twice into the glow layer and twice more
    // for the text. A TrackedRun carries the width AND the outline.
    const auto value = type::trackedRun (style, text);
    const auto suffixRun = suffix.isNotEmpty() ? type::trackedRun (suffixStyle, suffix)
                                               : type::TrackedRun();

    if (value.path.isEmpty() && suffixRun.path.isEmpty())
        return;

    const auto screenFg = lnf.token (theme::Token::screenFg);

    // `text-align: center` over the whole run, value and suffix together.
    const auto runLeft = area.getCentreX() - (value.width + suffixRun.width) * 0.5f;

    // The arrangement's origin is the BASELINE; JUCE centres a line of text on
    // it the way drawTracked does, so the same offset is used here.
    const auto baseline = area.getCentreY()
                        + type::styleFor (style).heightPx * kBaselineFromCentre;

    juce::Path glow;

    const auto place = [&] (const type::TrackedRun& run, float x, float y)
    {
        auto positioned = run.path;
        positioned.applyTransform (juce::AffineTransform::translation (x, y));
        return positioned;
    };

    const auto valuePath = place (value, runLeft, baseline);
    glow.addPath (valuePath);

    auto suffixPath = suffixRun.path;

    if (! suffixRun.path.isEmpty())
    {
        // The suffix sits on the same baseline but is a smaller row, so its own
        // metrics decide how far its baseline drops.
        const auto suffixBaseline = area.getCentreY()
                                  + type::styleFor (suffixStyle).heightPx * kBaselineFromCentre;

        suffixPath = place (suffixRun, runLeft + value.width, suffixBaseline);
        glow.addPath (suffixPath);
    }

    juce::DropShadow (screenFg.withAlpha (theme::kScreenGlowOpacity),
                      juce::roundToInt (theme::kScreenGlowRadius), {})
        .drawForPath (g, glow);

    g.setColour (screenFg);
    g.fillPath (valuePath);

    if (! suffixRun.path.isEmpty())
    {
        g.setColour (screenFg.withMultipliedAlpha (type::styleFor (suffixStyle).opacity));
        g.fillPath (suffixPath);
    }
}

} // namespace forrobox
