#include "HitVisualiser.h"

#include "Surface.h"

#include <cmath>

namespace forrobox
{

void HitVisualiser::trigger (float normalisedVelocity) noexcept
{
    level = juce::jmax (level, juce::jlimit (0.0f, 1.0f, normalisedVelocity));
}

void HitVisualiser::advance (int frames) noexcept
{
    if (frames <= 0 || level <= 0.0f)
        return;

    // std::pow rather than a loop: a poll that missed frames — a busy message
    // thread, a host that throttles an inactive editor — must decay by the same
    // amount it would have across those frames, not by one frame's worth.
    level *= std::pow (hitviz::kDecayPerFrame, static_cast<float> (frames));

    if (level <= hitviz::kSilenceLevel)
        level = 0.0f;
}

float HitVisualiser::ledAlpha() const noexcept
{
    // At rest the LED is NOT off: `opacity: 0.22` (css:279) is a dark dot that
    // still reads as a lamp. The lit formula starts at 0.25, so a level of 0
    // resolves to the resting value rather than to a dimmer one.
    return isLit() ? hitviz::kLedLitBase + level * hitviz::kLedLitSpan
                   : hitviz::kLedRestingAlpha;
}

float HitVisualiser::ledGlowRadius() const noexcept
{
    return isLit() ? hitviz::kLedGlowBase + level * hitviz::kLedGlowSpan : 0.0f;
}

void HitVisualiser::paintLed (juce::Graphics& g, juce::Rectangle<int> box) const
{
    if (box.isEmpty())
        return;

    const auto dot = box.toFloat();

    if (const auto radius = ledGlowRadius(); radius > 0.0f)
    {
        // DropShadow with a zero offset is the CSS's centred glow — the same
        // choice the accent bar records, where a stack of expanded rectangles
        // at low alpha rendered a hard-edged frame instead of a blur.
        juce::DropShadow (accent.withAlpha (ledAlpha()), juce::roundToInt (radius), {})
            .drawForRectangle (g, box);
    }

    g.setColour (accent.withAlpha (ledAlpha()));
    g.fillEllipse (dot);
}

void HitVisualiser::paintMeter (juce::Graphics& g, juce::Rectangle<int> box,
                                ForroBoxLookAndFeel& lnf) const
{
    if (box.isEmpty())
        return;

    const auto radius = static_cast<float> (lnf.cornerRadius());
    const auto area = box.toFloat();

    // ── the well: --sunken, radius --r, inset 0 1px 2px (css:303-307) ──────
    g.setColour (lnf.token (theme::Token::sunken));
    g.fillRoundedRectangle (area, radius);

    {
        juce::Graphics::ScopedSaveState clip (g);
        juce::Path rounded;
        rounded.addRoundedRectangle (area, radius);
        g.reduceClipRegion (rounded);

        surface::wellShadow (g, box, juce::Colours::black,
                             lnf.getMode() == theme::Mode::dark ? 0.4f : 0.1f);

        // ── the fill: scaleX(level) from the left (css:309-313) ────────────
        if (isLit())
        {
            const auto filled = area.withWidth (area.getWidth() * level);

            // `saturate(0.4 + accent-i x 0.6)`, the same law the accent bar's
            // fill uses — theme::accentFill owns it, rather than a second
            // saturation written here.
            const auto saturated = theme::accentFill (
                accent, hitviz::kFillSaturationBase
                          + lnf.accentIntensity() * hitviz::kFillSaturationSpan);

            const auto alpha = hitviz::kFillBase + level * hitviz::kFillSpan;

            g.setGradientFill (juce::ColourGradient (
                saturated.withAlpha (alpha), filled.getX(), 0.0f,
                saturated.withAlpha (alpha * hitviz::kFillFarAlpha), filled.getRight(), 0.0f,
                false));

            g.fillRect (filled);
        }

        // ── the ticks: 16 divisions, ALWAYS, whatever the step count ───────
        //
        // `repeat(..., calc(100% / 16))` (css:317) is a fixed 16 even when the
        // sequencer is showing 32 steps — it reads as a bar ruler, not as a step
        // ruler. Deriving it from the step count would be a plausible-looking
        // change that contradicts the stylesheet.
        {
            const auto tick = lnf.token (theme::Token::bg)
                                  .withAlpha (hitviz::kTickGroundMix * hitviz::kTickAlpha);

            g.setColour (tick);

            for (int i = 1; i < hitviz::kTickDivisions; ++i)
            {
                const auto x = area.getX()
                             + area.getWidth() * static_cast<float> (i)
                                   / static_cast<float> (hitviz::kTickDivisions);

                g.fillRect (juce::Rectangle<float> (x - 1.0f, area.getY(), 1.0f,
                                                    area.getHeight()));
            }
        }
    }
}

} // namespace forrobox
