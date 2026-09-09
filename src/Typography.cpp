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
    const char* expectedFamily;
    const char* expectedStyle;
};

const std::array<FaceResource, kNumFaces>& faceResources()
{
    static const std::array<FaceResource, kNumFaces> resources {{
        { Face::sansRegular,  FontData::SpaceGroteskRegular_ttf,  FontData::SpaceGroteskRegular_ttfSize,  "Space Grotesk", "Regular"  },
        { Face::sansMedium,   FontData::SpaceGroteskMedium_ttf,   FontData::SpaceGroteskMedium_ttfSize,   "Space Grotesk", "Medium"   },
        { Face::sansSemiBold, FontData::SpaceGroteskSemiBold_ttf, FontData::SpaceGroteskSemiBold_ttfSize, "Space Grotesk", "SemiBold" },
        { Face::sansBold,     FontData::SpaceGroteskBold_ttf,     FontData::SpaceGroteskBold_ttfSize,     "Space Grotesk", "Bold"     },
        { Face::monoRegular,  FontData::IBMPlexMonoRegular_ttf,   FontData::IBMPlexMonoRegular_ttfSize,   "IBM Plex Mono", "Regular"  },
        { Face::monoMedium,   FontData::IBMPlexMonoMedium_ttf,    FontData::IBMPlexMonoMedium_ttfSize,    "IBM Plex Mono", "Medium"   },
        { Face::monoSemiBold, FontData::IBMPlexMonoSemiBold_ttf,  FontData::IBMPlexMonoSemiBold_ttfSize,  "IBM Plex Mono", "SemiBold" },
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

const TextStyle& styleFor (Style style) noexcept
{
    const auto index = static_cast<size_t> (style);
    jassert (index < textStyles.size());

    const auto& row = textStyles[index];

    // The table is indexed by the enum, so a row inserted out of order is a
    // failed assertion here rather than every label below it drawn at the wrong
    // size — the class of silent error 03-03's TIMBRE ordering control found.
    jassert (row.style == style);

    return row;
}

float trackedWidth (Style style, juce::StringRef text)
{
    const auto& spec = styleFor (style);
    const auto  font = fontFor (spec.face, spec.heightPx);
    const auto  string = spec.uppercase ? juce::String (text).toUpperCase() : juce::String (text);

    if (string.isEmpty())
        return 0.0f;

    const auto tracking = spec.letterSpacingEm * spec.heightPx;

    // Tracking applies BETWEEN glyphs: n glyphs have n-1 gaps. Counting n gaps
    // would leave a centred string half a tracking step left of centre.
    return juce::GlyphArrangement::getStringWidth (font, string)
         + tracking * static_cast<float> (juce::jmax (0, string.length() - 1));
}

void drawTracked (juce::Graphics& g, Style style, juce::StringRef text,
                  juce::Rectangle<float> area, juce::Justification justification)
{
    const auto& spec = styleFor (style);
    const auto  string = spec.uppercase ? juce::String (text).toUpperCase() : juce::String (text);

    if (string.isEmpty())
        return;

    const auto font = fontFor (spec.face, spec.heightPx);
    g.setFont (font);

    const auto width    = trackedWidth (style, text);
    const auto tracking = spec.letterSpacingEm * spec.heightPx;

    auto x = area.getX();

    if (justification.testFlags (juce::Justification::horizontallyCentred))
        x = area.getCentreX() - width * 0.5f;
    else if (justification.testFlags (juce::Justification::right))
        x = area.getRight() - width;

    // Baseline from the font's own ascent, so rows of different sizes centre
    // consistently rather than each by eye.
    const auto baseline = area.getCentreY() + (font.getAscent() - font.getDescent()) * 0.5f;

    // The style's own opacity is deliberately NOT applied here — see dimmed()
    // in the header. The caller owns the colour.

    for (int i = 0; i < string.length(); ++i)
    {
        const auto glyph = string.substring (i, i + 1);
        g.drawSingleLineText (glyph, juce::roundToInt (x), juce::roundToInt (baseline));
        x += juce::GlyphArrangement::getStringWidth (font, glyph) + tracking;
    }
}

} // namespace forrobox::type
