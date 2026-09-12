#include "LogoMark.h"

namespace forrobox
{

LogoMark::LogoMark (ForroBoxLookAndFeel& lookAndFeelToUse)
    : lnf (lookAndFeelToUse)
{
    // Built ONCE, in viewBox units. Rebuilding six juce::Paths on every repaint
    // would be work that cannot change — the mark has no state at all.
    //
    // `M3 10 L13 7.5 L13 26.5 L3 29 Z` — app.js:49.
    sanfona.startNewSubPath (3.0f, 10.0f);
    sanfona.lineTo (13.0f, 7.5f);
    sanfona.lineTo (13.0f, 26.5f);
    sanfona.lineTo (3.0f, 29.0f);
    sanfona.closeSubPath();

    // `M6 9.2 V27.8 M9 8.4 V27 M12 7.6 V26.2` — the three bellows pleats.
    const std::array<std::array<float, 3>, 3> pleatLines {{
        { 6.0f, 9.2f, 27.8f }, { 9.0f, 8.4f, 27.0f }, { 12.0f, 7.6f, 26.2f },
    }};

    for (const auto& pleat : pleatLines)
    {
        pleats.startNewSubPath (pleat[0], pleat[1]);
        pleats.lineTo (pleat[0], pleat[2]);
    }

    zabumba.addEllipse (logo::kZabumbaCx - logo::kZabumbaR, logo::kZabumbaCy - logo::kZabumbaR,
                        logo::kZabumbaR * 2.0f, logo::kZabumbaR * 2.0f);

    // `M24.5 9.7 V27.3 M15.7 18.5 H33.3` — the crossed tension rods.
    rods.startNewSubPath (logo::kZabumbaCx, logo::kZabumbaCy - logo::kZabumbaR);
    rods.lineTo (logo::kZabumbaCx, logo::kZabumbaCy + logo::kZabumbaR);
    rods.startNewSubPath (logo::kZabumbaCx - logo::kZabumbaR, logo::kZabumbaCy);
    rods.lineTo (logo::kZabumbaCx + logo::kZabumbaR, logo::kZabumbaCy);

    // `M31 6 L41.5 24 L23.5 24` — OPEN, so no closeSubPath: the gap is where a
    // triângulo is struck.
    triangulo.startNewSubPath (31.0f, 6.0f);
    triangulo.lineTo (41.5f, 24.0f);
    triangulo.lineTo (23.5f, 24.0f);

    beater.startNewSubPath (35.5f, 13.5f);
    beater.lineTo (41.0f, 10.5f);
}

void LogoMark::paint (juce::Graphics& g)
{
    // Scaled ONCE, the knob's rule. Fitted rather than stretched, so the mark
    // keeps its proportions whatever box it is given.
    const auto scale = juce::jmin (static_cast<float> (getWidth()) / logo::kViewBoxWidth,
                                   static_cast<float> (getHeight()) / logo::kViewBoxHeight);

    const auto transform = juce::AffineTransform::scale (scale)
                               .translated ((static_cast<float> (getWidth())
                                             - logo::kViewBoxWidth * scale) * 0.5f,
                                            (static_cast<float> (getHeight())
                                             - logo::kViewBoxHeight * scale) * 0.5f);

    // The stroke widths are viewBox units too, so each is scaled with the path
    // rather than applied in pixels afterwards.
    const auto strokeIn = [&] (const juce::Path& path, juce::Colour colour, float width,
                               juce::PathStrokeType::JointStyle joint,
                               juce::PathStrokeType::EndCapStyle cap)
    {
        g.setColour (colour);
        g.strokePath (path, { width * scale, joint, cap }, transform);
    };

    using Joint = juce::PathStrokeType;

    strokeIn (sanfona, lnf.token (theme::Token::fgDim), logo::kSanfonaStroke,
              Joint::curved, Joint::butt);
    strokeIn (pleats, lnf.token (theme::Token::fgDim), logo::kSanfonaStroke,
              Joint::curved, Joint::butt);

    strokeIn (zabumba, lnf.token (theme::Token::fg), logo::kZabumbaStroke,
              Joint::curved, Joint::butt);
    strokeIn (rods, lnf.token (theme::Token::fgFaint), logo::kRodStroke,
              Joint::curved, Joint::butt);

    // Round caps AND joins, css:163 — the one sub-mark that specifies them, and
    // the one drawn in the accent.
    strokeIn (triangulo, theme::accent (theme::Accent::zabumba), logo::kTrianguloStroke,
              Joint::curved, Joint::rounded);
    strokeIn (beater, theme::accent (theme::Accent::zabumba), logo::kTrianguloStroke,
              Joint::curved, Joint::rounded);
}

} // namespace forrobox
