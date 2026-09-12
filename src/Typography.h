/* ============================================================================
   FORRÓ BOX — embedded typefaces and the type scale

   Two families, seven weights, all embedded. PLANNING.md's type scale is
   reproduced here as one table so a later reader can diff it against the spec
   line by line rather than hunting the numbers across call sites.

   MESSAGE THREAD ONLY. Nothing here is reachable from processBlock and none of
   it is real-time safe: the faces are created lazily and juce::Font allocates.
============================================================================ */
#pragma once

#include <juce_graphics/juce_graphics.h>

#include <array>

namespace forrobox::type
{

/** The seven embedded faces.

    Space Grotesk carries 600 and 700 for the wordmark and every instrument
    name; the two are NOT interchangeable with a synthetic bold, which is why
    they are instanced from the variable font in scripts/build-fonts.py rather
    than faked at runtime. */
enum class Face
{
    sansRegular,      ///< Space Grotesk 400
    sansMedium,       ///< Space Grotesk 500
    sansSemiBold,     ///< Space Grotesk 600
    sansBold,         ///< Space Grotesk 700
    monoRegular,      ///< IBM Plex Mono 400
    monoMedium,       ///< IBM Plex Mono 500
    monoSemiBold,     ///< IBM Plex Mono 600
};

inline constexpr int kNumFaces = 7;

/** The typeface for one weight, created once and cached for the process.

    Looked up by this enum and NEVER by name. `juce::Font ("Space Grotesk",
    "SemiBold", h)` would go through JUCE's name-based lookup, which depends on
    the four faces having been registered with distinguishable names — a
    dependency this project has no reason to take on, and one the instancer
    silently broke until the name tables were patched (every weight reported
    family "Space Grotesk Light", style "Regular"). */
juce::Typeface::Ptr typefaceFor (Face);

/** A `juce::Font` at that face and pixel height. */
juce::Font fontFor (Face, float heightPx);

/** Every text style the design specifies, one enum value per row of
    PLANNING.md's type-scale table, in the table's own order. */
enum class Style
{
    wordmark,
    bpmReadout,
    bpmSuffix,
    globalKnobReadout,
    globalKnobName,
    stripInstrumentName,
    stripIndex,
    sampleName,
    knobMicroLabel,
    sectionLabel,
    sequencerRowLabel,
    profileName,
    profileDescription,
    timbreName,
    timbreSubLabel,
    quickSwitchCode,
    styleLabel,
    buttonLabel,
    dragMidiLabel,
    footerLabel,
    tooltip,
    muteSoloLabel,
};

inline constexpr int kNumStyles = 22;

/** One row of the type scale.

    `element` is the spec's own wording for the row, carried alongside the
    numbers on purpose: 21 rows of bare floats are unreviewable against the
    table they came from, and Phase 3 flagged exactly that shape in
    `voiceSpecs`' anonymous positional rows.

    `letterSpacingEm` is in em, as the CSS is. JUCE has no tracking on
    `juce::Font`, so `drawTracked` below applies it. */
struct TextStyle
{
    const char* element;
    Style       style;
    float       heightPx;
    Face        face;
    float       letterSpacingEm;
    bool        uppercase;
    float       opacity;        ///< 1.0 unless the spec dims the row
};

inline constexpr std::array<TextStyle, kNumStyles> textStyles {{
    // element                        style                       px    face                    em      upper  opacity
    { "Wordmark FORRO-BOX",           Style::wordmark,            17.0f, Face::sansBold,        0.13f,  false, 1.00f },
    { "BPM readout",                  Style::bpmReadout,          22.0f, Face::monoMedium,      0.02f,  false, 1.00f },
    { "BPM suffix",                   Style::bpmSuffix,            9.0f, Face::monoRegular,     0.12f,  false, 0.55f },
    { "Global knob readout",          Style::globalKnobReadout,   16.0f, Face::monoMedium,      0.01f,  false, 1.00f },
    { "Global knob name",             Style::globalKnobName,       9.5f, Face::sansSemiBold,    0.18f,  true,  1.00f },
    { "Strip instrument name",        Style::stripInstrumentName, 12.5f, Face::sansSemiBold,    0.10f,  false, 1.00f },
    { "Strip index",                  Style::stripIndex,           9.0f, Face::monoRegular,     0.00f,  false, 1.00f },
    { "Sample name",                  Style::sampleName,          12.5f, Face::sansMedium,      0.01f,  false, 1.00f },
    { "Knob micro-label",             Style::knobMicroLabel,       9.0f, Face::sansRegular,     0.07f,  true,  1.00f },
    { "Section label",                Style::sectionLabel,         9.0f, Face::sansMedium,      0.20f,  true,  1.00f },
    { "Sequencer row label",          Style::sequencerRowLabel,   10.5f, Face::sansSemiBold,    0.06f,  false, 1.00f },
    { "Profile name",                 Style::profileName,         11.0f, Face::sansSemiBold,    0.08f,  false, 1.00f },
    { "Profile description",          Style::profileDescription,   9.5f, Face::sansRegular,     0.00f,  false, 1.00f },
    { "Timbre name",                  Style::timbreName,          11.0f, Face::sansSemiBold,    0.06f,  false, 1.00f },
    { "Timbre sub-label",             Style::timbreSubLabel,       8.0f, Face::sansRegular,     0.10f,  true,  1.00f },
    { "Quick-switch code",            Style::quickSwitchCode,     10.0f, Face::monoMedium,      0.06f,  false, 1.00f },
    { "STYLE label",                  Style::styleLabel,           8.5f, Face::sansRegular,     0.20f,  true,  1.00f },
    { "Button label",                 Style::buttonLabel,         10.0f, Face::sansMedium,      0.08f,  true,  1.00f },
    { "DRAG MIDI label",              Style::dragMidiLabel,       11.0f, Face::sansBold,        0.16f,  true,  1.00f },
    { "Footer label",                 Style::footerLabel,          9.0f, Face::sansRegular,     0.14f,  true,  1.00f },
    { "Tooltip",                      Style::tooltip,             11.0f, Face::monoRegular,     0.00f,  false, 1.00f },

    // NOT in PLANNING.md's type-scale table, which lists 21 rows and omits the
    // mute/solo button. Its face is specified only in forrobox.css:337 —
    // `font-family: var(--mono); font-size: 11px; font-weight: 500` — so the
    // stylesheet is the source for this one row.
    { "Mute/Solo button",             Style::muteSoloLabel,       11.0f, Face::monoMedium,      0.00f,  false, 1.00f },
}};

/** The row for a style. Indexed, then asserted — so a reordered enum is a
    failed assertion rather than a wrong font. */
/** Every row sits at its own enum's index.

    Asserted ONCE for the whole table at compile time, which is strictly
    stronger than the per-call `jassert` this replaces: that only fired for a
    row someone happened to look up, and only in a debug build. A row inserted
    out of order is now a build failure — the class of silent error 03-03's
    TIMBRE ordering control found, made unrepresentable. */
constexpr bool textStylesIndexedByEnum() noexcept
{
    for (size_t i = 0; i < textStyles.size(); ++i)
        if (static_cast<size_t> (textStyles[i].style) != i)
            return false;

    return true;
}

static_assert (textStylesIndexedByEnum(),
               "textStyles is no longer indexed by its own Style enum, so styleFor would return "
               "some other row — every label below the reordered one would draw at the wrong size");

/** The row for a style.

    `constexpr` and defined here so compile-time users — ChassisLayout's knob
    cell height, Knob's label row — go through the SAME accessor rather than
    indexing `textStyles` by hand, each carrying a local static_assert that
    reproduces the table-wide one above. */
constexpr const TextStyle& styleFor (Style style) noexcept
{
    return textStyles[static_cast<size_t> (style)];
}

/** `juce::Font` for a style, at its specified height and face. */
inline juce::Font fontFor (Style style) { const auto& s = styleFor (style); return fontFor (s.face, s.heightPx); }

/** `base` dimmed by the style's own opacity.

    The spec dims exactly one row — the BPM suffix, to 55%. That belongs to the
    COLOUR, not to the drawing: `drawTracked` cannot apply it, because
    juce::Graphics has no way to read the current colour back, and applying it
    with `setOpacity` would silently overwrite the alpha of a translucent token
    the caller had chosen deliberately (`--fg-dim` is already 50%).

    So the field has one owner and one consumer: callers pick a token and pass
    it through here. */
inline juce::Colour dimmed (Style style, juce::Colour base)
{
    return base.withMultipliedAlpha (styleFor (style).opacity);
}

/** Draws `text` with the style's letter-spacing applied.

    Exists because JUCE has no tracking on `juce::Font` and the design leans on
    it hard — 0.20em on section labels, 0.13em on the wordmark. Every later plan
    needs this, so it is written once here rather than reinvented per component.

    The tracking lands BETWEEN glyphs and not after the last one; adding it
    after the last glyph would make a centred string sit left of centre by half
    the tracking. Positions and width come from ONE arrangement shared with
    trackedWidth — see trackingFor below for why that matters.

    `justification` supports only the horizontal thirds and vertical centring
    the layout actually uses. */
void drawTracked (juce::Graphics&, Style, juce::StringRef text,
                  juce::Rectangle<float> area, juce::Justification);

/** The width `drawTracked` will occupy, so callers can lay out around it.

    The same walk `drawTracked` performs, not a second calculation that ought
    to agree with it. */
float trackedWidth (Style, juce::StringRef text);

/** The per-glyph tracking step in pixels, `letterSpacingEm * heightPx`.

    Exists because the expression was written out twice — once in
    `trackedWidth` and once in `drawTracked` — and only the first was tested. A
    negative control that set the drawing path's copy to zero passed all 1275
    checks: every label in the UI would have lost the letter-spacing the design
    calls "a big part of the look", while the width calculation still reserved
    room for it. Same shape as the `dry = 1 - 0.5 * wet` duplicate 03-03 found.

    That fix shared the STEP and left the two paths still computing the total
    extent differently — one from the whole string's advance, one by summing
    per-glyph advances, which disagree wherever the font kerns. Both now come
    from one `juce::GlyphArrangement`, so there is no second copy left to
    diverge. */
float trackingFor (Style) noexcept;

} // namespace forrobox::type
