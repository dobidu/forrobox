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

const std::array<FaceResource, kNumSansFaces>& sansResources()
{
    static const std::array<FaceResource, kNumSansFaces> resources {{
        { Face::sansRegular,  FontData::SpaceGroteskRegular_ttf,  FontData::SpaceGroteskRegular_ttfSize },
        { Face::sansMedium,   FontData::SpaceGroteskMedium_ttf,   FontData::SpaceGroteskMedium_ttfSize },
        { Face::sansSemiBold, FontData::SpaceGroteskSemiBold_ttf, FontData::SpaceGroteskSemiBold_ttfSize },
        { Face::sansBold,     FontData::SpaceGroteskBold_ttf,     FontData::SpaceGroteskBold_ttfSize },
    }};

    return resources;
}

/** family x weight, in ONE table.

    The same law `ids::channelInfos` and `settings::infos` follow: a family
    cannot be added without both of its weights, because the row is the unit.

    SPACE MONO'S MEDIUM IS ITS REGULAR FILE, and that is the design source's
    answer rather than a substitution invented here. Space Mono publishes 400,
    700 and their italics and is not variable, so there is no 500 to embed. CSS
    Fonts 4 section 5.2 resolves a 500 request against {400, 700} by trying 500,
    finding nothing, then walking DOWN — landing on 400. The prototype is a
    browser, so 400 is what it would render. Faking a 500 by synthesising one
    would put a weight in the binary that no foundry published and that the
    prototype would never show. */
struct MonoResource
{
    /** NAMED, not positional. `faceResources` carries its `Face` and asserts it;
        this carried nothing, so row n meant `MonoFamily`'s nth enumerator only
        by convention. Worse, `std::array` aggregate init value-initialises a
        MISSING row — adding a family and forgetting this table would compile
        clean and hand `createSystemTypefaceFor (nullptr, 0)` to the first paint
        in that family. /code-review. */
    MonoFamily  family;
    const char* data;
    int         size;
};

const std::array<std::array<MonoResource, 2>, kNumMonoFamilies>& monoResources()
{
    static const std::array<std::array<MonoResource, 2>, kNumMonoFamilies> resources {{
        // regular (400)                                                        medium (500)
        {{ { MonoFamily::ibmPlexMono,   FontData::IBMPlexMonoRegular_ttf,   FontData::IBMPlexMonoRegular_ttfSize },
           { MonoFamily::ibmPlexMono,   FontData::IBMPlexMonoMedium_ttf,    FontData::IBMPlexMonoMedium_ttfSize } }},

        {{ { MonoFamily::jetBrainsMono, FontData::JetBrainsMonoRegular_ttf, FontData::JetBrainsMonoRegular_ttfSize },
           { MonoFamily::jetBrainsMono, FontData::JetBrainsMonoMedium_ttf,  FontData::JetBrainsMonoMedium_ttfSize } }},

        // Space Mono: the 500 column is the 400 FILE. See the note above.
        {{ { MonoFamily::spaceMono,     FontData::SpaceMonoRegular_ttf,     FontData::SpaceMonoRegular_ttfSize },
           { MonoFamily::spaceMono,     FontData::SpaceMonoRegular_ttf,     FontData::SpaceMonoRegular_ttfSize } }},
    }};

    return resources;
}

bool isMonoFace (Face face) noexcept
{
    return face == Face::monoRegular || face == Face::monoMedium;
}

/** The process's chosen family. Plain, not atomic: every writer and every reader
    is the message thread — `applyStoredSettings` sets it and `paint` reads it. */
MonoFamily currentMonoFamily = MonoFamily::ibmPlexMono;
} // namespace

bool setMonoFamily (MonoFamily family) noexcept
{
    if (family == currentMonoFamily)
        return false;

    currentMonoFamily = family;
    return true;
}

MonoFamily getMonoFamily() noexcept
{
    return currentMonoFamily;
}

juce::Typeface::Ptr typefaceFor (Face face)
{
    // One cache for the process. Created on first use rather than at static
    // init: JUCE's font backend is not guaranteed ready before main().
    //
    // KEYED BY FAMILY AS WELL AS FACE. Keyed by face alone — which is what this
    // was before the display font became a setting — the first mono typeface
    // built would be returned for every family forever, so switching would
    // change nothing and switching back would look correct. Keying it means a
    // family that is selected twice still costs one construction, which
    // clearing-on-change would not.
    static std::array<std::array<juce::Typeface::Ptr, kNumFaces>,
                      kNumMonoFamilies> cache;

    const auto index = static_cast<size_t> (face);

    // Against the ARRAY's own extent rather than `kNumFaces`. The two agree
    // today, and comparing a size_t against an int also trips -Wsign-compare on
    // a stricter build. /code-review.
    jassert (index < cache[0].size());

    // The sans faces do not vary by family, so they all live in row 0 rather
    // than being built three times.
    const auto familyRow = isMonoFace (face) ? static_cast<size_t> (currentMonoFamily)
                                             : size_t { 0 };

    auto& slot = cache[familyRow][index];

    if (slot == nullptr)
    {
        const char* data = nullptr;
        int size = 0;

        if (isMonoFace (face))
        {
            const auto weight = face == Face::monoRegular ? size_t { 0 } : size_t { 1 };
            const auto& resource = monoResources()[familyRow][weight];

            jassert (resource.family == currentMonoFamily);   // the table is indexed by the enum
            jassert (resource.data != nullptr);

            data = resource.data;
            size = resource.size;
        }
        else
        {
            const auto& resource = sansResources()[index];
            jassert (resource.face == face);   // the table is indexed by the enum

            data = resource.data;
            size = resource.size;
        }

        slot = juce::Typeface::createSystemTypefaceFor (data, static_cast<size_t> (size));
    }

    return slot;
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

TrackedRun trackedRun (Style style, juce::StringRef text)
{
    auto layout = layOutTracked (style, text);

    TrackedRun out;
    out.width = layout.width;

    if (layout.glyphs.getNumGlyphs() > 0)
    {
        layout.glyphs.createPath (out.path);

        // Laid out from the arrangement's own origin, which is the text
        // BASELINE at x = the first glyph's left. Normalised to (0, 0) so a
        // caller positions it with one translate rather than rediscovering the
        // baseline.
        out.path.applyTransform (
            juce::AffineTransform::translation (-layout.glyphs.getGlyph (0).getLeft(), 0.0f));
    }

    return out;
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

    const auto baseline = baselineIn (style, area);

    // The style's own opacity is deliberately NOT applied here — see dimmed()
    // in the header. The caller owns the colour.
    layout.glyphs.moveRangeOfGlyphs (0, layout.glyphs.getNumGlyphs(), x, baseline);
    layout.glyphs.draw (g);
}

float baselineIn (Style style, juce::Rectangle<float> area)
{
    // From the font's own ascent, so rows of different sizes centre consistently
    // rather than each by eye.
    const auto font = fontFor (style);

    return area.getCentreY() + (font.getAscent() - font.getDescent()) * 0.5f;
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
    static const juce::String ellipsis = juce::String::fromUTF8 ("…");

    for (int length = text.length() - 1; length > 0; --length)
    {
        const auto candidate = text.substring (0, length).trimEnd() + ellipsis;

        if (trackedWidth (style, candidate) <= maxWidth)
            return candidate;
    }

    return ellipsis;
}

} // namespace forrobox::type
