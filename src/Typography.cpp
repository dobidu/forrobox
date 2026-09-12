#include "Typography.h"

#include <FontData.h>

namespace forrobox::type
{
namespace
{
/** The embedded blob for one face.

    `FontData`'s symbol names come from juceaide's mangling of the filenames, so
    they are listed once here and nowhere else. */
struct FaceResource
{
    Face        face;
    const char* data;
    int         size;
};

const std::array<FaceResource, kNumFaces>& faceResources()
{
    static const std::array<FaceResource, kNumFaces> resources {{
        { Face::sansRegular,  FontData::SpaceGroteskRegular_ttf,  FontData::SpaceGroteskRegular_ttfSize },
        { Face::sansMedium,   FontData::SpaceGroteskMedium_ttf,   FontData::SpaceGroteskMedium_ttfSize },
        { Face::sansSemiBold, FontData::SpaceGroteskSemiBold_ttf, FontData::SpaceGroteskSemiBold_ttfSize },
        { Face::sansBold,     FontData::SpaceGroteskBold_ttf,     FontData::SpaceGroteskBold_ttfSize },
        { Face::monoRegular,  FontData::IBMPlexMonoRegular_ttf,   FontData::IBMPlexMonoRegular_ttfSize },
        { Face::monoMedium,   FontData::IBMPlexMonoMedium_ttf,    FontData::IBMPlexMonoMedium_ttfSize },
        { Face::monoSemiBold, FontData::IBMPlexMonoSemiBold_ttf,  FontData::IBMPlexMonoSemiBold_ttfSize },
    }};

    return resources;
}
} // namespace

juce::Typeface::Ptr typefaceFor (Face face)
{
    // One cache for the process. Created on first use rather than at static
    // init: JUCE's font backend is not guaranteed ready before main().
    static std::array<juce::Typeface::Ptr, kNumFaces> cache;

    const auto index = static_cast<size_t> (face);
    jassert (index < cache.size());

    if (cache[index] == nullptr)
    {
        const auto& resource = faceResources()[index];
        jassert (resource.face == face);   // the table is indexed by the enum

        cache[index] = juce::Typeface::createSystemTypefaceFor (resource.data,
                                                                static_cast<size_t> (resource.size));
    }

    return cache[index];
}

juce::Font fontFor (Face face, float heightPx)
{
    return juce::Font (juce::FontOptions (typefaceFor (face))).withHeight (heightPx);
}


float trackingFor (Style style) noexcept
{
    const auto& spec = styleFor (style);

    return spec.letterSpacingEm * spec.heightPx;
}

namespace
{
/** The one tracked-text layout: the glyph positions AND the total width.

    Both used to be computed independently — `trackedWidth` from the whole
    string's advance plus n-1 tracking steps, `drawTracked` by accumulating
    each glyph's own advance plus tracking — and then `drawTracked` positioned
    itself using `trackedWidth`'s number while walking its own. Those agree
    only if per-glyph advances sum to the whole-string advance, which is false
    whenever the font kerns a pair: both embedded families carry GPOS. Every
    centred label would sit off-centre by the kerning delta, and the check that
    should have caught it allows 3 px — enough to absorb exactly this.

    So it is one arrangement now: the positions drawn and the width reported
    come from the same walk. That also stops the string being reshaped once per
    glyph (measured 243.6 us -> 65.0 us for the chassis's ten labels). */
struct TrackedLayout
{
    juce::GlyphArrangement glyphs;
    float                  width { 0.0f };
};

TrackedLayout layOutTracked (Style style, juce::StringRef text)
{
    const auto& spec = styleFor (style);
    const auto  font = fontFor (spec.face, spec.heightPx);
    const auto  string = spec.uppercase ? juce::String (text).toUpperCase() : juce::String (text);

    TrackedLayout out;

    if (string.isEmpty())
        return out;

    out.glyphs.addLineOfText (font, string, 0.0f, 0.0f);

    // Tracking applies BETWEEN glyphs: n glyphs have n-1 gaps. Shifting the
    // last glyph too would leave a centred string half a step left of centre.
    const auto tracking = trackingFor (style);
    const auto count = out.glyphs.getNumGlyphs();

    for (int i = 1; i < count; ++i)
        out.glyphs.moveRangeOfGlyphs (i, count - i, tracking, 0.0f);

    const auto& last = out.glyphs.getGlyph (count - 1);
    out.width = last.getRight() - out.glyphs.getGlyph (0).getLeft();

    return out;
}
} // namespace

float trackedWidth (Style style, juce::StringRef text)
{
    return layOutTracked (style, text).width;
}

void drawTracked (juce::Graphics& g, Style style, juce::StringRef text,
                  juce::Rectangle<float> area, juce::Justification justification)
{
    auto layout = layOutTracked (style, text);

    if (layout.glyphs.getNumGlyphs() == 0)
        return;

    const auto& spec = styleFor (style);
    const auto  font = fontFor (spec.face, spec.heightPx);

    auto x = area.getX();

    if (justification.testFlags (juce::Justification::horizontallyCentred))
        x = area.getCentreX() - layout.width * 0.5f;
    else if (justification.testFlags (juce::Justification::right))
        x = area.getRight() - layout.width;

    // Baseline from the font's own ascent, so rows of different sizes centre
    // consistently rather than each by eye.
    const auto baseline = area.getCentreY() + (font.getAscent() - font.getDescent()) * 0.5f;

    // The style's own opacity is deliberately NOT applied here — see dimmed()
    // in the header. The caller owns the colour.
    layout.glyphs.moveRangeOfGlyphs (0, layout.glyphs.getNumGlyphs(), x, baseline);
    layout.glyphs.draw (g);
}

juce::String ellipsised (Style style, const juce::String& text, float maxWidth)
{
    if (maxWidth <= 0.0f)
        return {};

    if (trackedWidth (style, text) <= maxWidth)
        return text;

    // The ellipsis is a single character (U+2026), as the browser draws it, and
    // it is measured as part of the candidate rather than subtracted from the
    // budget: the tracking applies to it too.
    static const juce::String ellipsis = juce::String::fromUTF8 ("\xe2\x80\xa6");

    for (int length = text.length() - 1; length > 0; --length)
    {
        const auto candidate = text.substring (0, length).trimEnd() + ellipsis;

        if (trackedWidth (style, candidate) <= maxWidth)
            return candidate;
    }

    return ellipsis;
}

} // namespace forrobox::type
