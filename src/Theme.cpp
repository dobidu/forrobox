#include "Theme.h"

namespace forrobox::theme
{

juce::Colour colour (Token token, Mode mode) noexcept
{
    const auto index = static_cast<size_t> (token);
    jassert (index < tokenSpecs.size());

    const auto& spec = tokenSpecs[index];

    // Indexed by the enum, so a row inserted out of order fails here rather
    // than repainting the whole UI in the wrong colours silently.
    jassert (spec.token == token);

    return juce::Colour (mode == Mode::dark ? spec.dark : spec.light);
}

juce::Colour accent (Accent which) noexcept
{
    const auto index = static_cast<size_t> (which);
    jassert (index < accentSpecs.size());

    const auto& spec = accentSpecs[index];
    jassert (spec.accent == which);

    return juce::Colour (spec.argb);
}

juce::Colour subColour (int index) noexcept
{
    jassert (juce::isPositiveAndBelow (index, static_cast<int> (subColours.size())));

    return juce::Colour (subColours[static_cast<size_t> (
                             juce::jlimit (0, static_cast<int> (subColours.size()) - 1, index))]);
}

juce::Colour mix (juce::Colour base, juce::Colour other, float otherWeight) noexcept
{
    const auto weight = juce::jlimit (0.0f, 1.0f, otherWeight);

    // Interpolated in float and quantised ONCE, deliberately not through
    // juce::Colour::interpolatedWith. That function quantises the proportion
    // itself to 8 bits first — `c1.tween (c2, roundToInt (p * 255.0f))` — and
    // then round-trips the channels through premultiplied integers, so the
    // weight it actually applies is n/255 and the result drifts by a few LSBs.
    //
    // Measured: the anchor tint at the spec's 12% came out as a 27/255 move on
    // red where the arithmetic gives 24.2, and a 50% black-to-white mix came
    // out 127 where the browser gives 128. Small, but these are the numbers the
    // design was chosen against, and reproducing a percentage is the whole job
    // of this function.
    const auto channel = [weight] (float from, float to) { return from + (to - from) * weight; };

    return juce::Colour::fromFloatRGBA (channel (base.getFloatRed(),   other.getFloatRed()),
                                        channel (base.getFloatGreen(), other.getFloatGreen()),
                                        channel (base.getFloatBlue(),  other.getFloatBlue()),
                                        channel (base.getFloatAlpha(), other.getFloatAlpha()));
}

juce::Colour saturated (juce::Colour colour, float intensity,
                        float floorAmount, float range) noexcept
{
    // CSS `filter: saturate(n)` scales saturation about grey and CLAMPS at the
    // top — it does not wrap — so n > 1 is representable but never produced
    // here: floorAmount + range == 1.0 at both specified sites, so the factor
    // reaches exactly 1.0 at full intensity and the law is the identity there.
    const auto factor = floorAmount + range * juce::jlimit (0.0f, 1.0f, intensity);

    return colour.withSaturation (juce::jlimit (0.0f, 1.0f, colour.getSaturation() * factor));
}

Shadows shadowsFor (Mode mode) noexcept
{
    // Straight from PLANNING.md's "Spacing, radius, shadow". The light theme is
    // not the dark theme at a different alpha: it specifies fewer layers (no
    // recessed outline) and a shallower well, so it gets its own row.
    // Designated initialisers, not positional: this struct gained an eighth
    // field (headerHighlight) and a positional list is how a colour lands one
    // slot out with nothing failing. Named fields also diff against the CSS.
    const auto black = [] (float alpha) { return juce::Colour::fromFloatRGBA (0.0f, 0.0f, 0.0f, alpha); };
    const auto white = [] (float alpha) { return juce::Colour::fromFloatRGBA (1.0f, 1.0f, 1.0f, alpha); };

    if (mode == Mode::dark)
        return { .recessedInner     = black (0.45f),
                 .recessedOutline   = black (0.20f),
                 .headerHighlight   = white (0.05f),   // css:87  — same in both themes
                 .raisedHighlight   = white (0.04f),   // css:600 — .side, .footer
                 .wellShadow        = black (0.40f),
                 .wellRadius        = 6.0f,
                 .padRecessInner    = black (0.40f),
                 .padRecessOutline  = black (0.25f) };

    return { .recessedInner     = black (0.14f),
             .recessedOutline   = juce::Colours::transparentBlack,
             .headerHighlight   = white (0.05f),   // css:87  — NOT overridden for light
             .raisedHighlight   = white (0.50f),   // css:601 — .side, .footer
             .wellShadow        = black (0.10f),
             .wellRadius        = 5.0f,
             .padRecessInner    = black (0.12f),
             .padRecessOutline  = juce::Colours::transparentBlack };
}

} // namespace forrobox::theme
