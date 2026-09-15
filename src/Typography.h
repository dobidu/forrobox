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
    loadLabel,
    patternScreen,
    stripMicroLabel,
    ghostValue,
    miniButtonLabel,
    presetScreen,
    dragMidiArrow,
    dragMidiSub,
    outToggleLabel,
    seqHint,

    /// The kit overlay's header, `BATERIA · KIT` — css:569.
    kitTitle,
    /// Its explanatory sub-line — css:571.
    kitSubLine,
    /// A kit row's short code, BB / CX / HH / TOM — css:581.
    kitRowName,
    /// A kit row's full Portuguese name — css:582.
    kitRowFull,
};

inline constexpr int kNumStyles = 36;

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

    // Four more rows PLANNING.md's table omits, all from the strip and all
    // sourced from forrobox.css, cross-checked by verify-geometry.py against
    // the rule each is quoted from.
    //
    // `.load-btn` (css:295-299) declares no font-family at all, so a browser
    // renders it in its own default button face while every other label in the
    // strip inherits `--sans` from css:60. That is a prototype slip rather than
    // a design, and it is the one place here that does not follow the browser:
    // the row is `--sans` at the weight the rule inherits.
    { "LOAD button",                  Style::loadLabel,            9.0f, Face::sansRegular,     0.06f,  true,  1.00f },
    { "Pattern screen",               Style::patternScreen,       10.0f, Face::monoRegular,     0.04f,  false, 1.00f },

    // ONE row for two rules — `.ghost-row .gl span` (css:348) and
    // `.subdots-label` (css:354) — because they declare the SAME size, spacing
    // and transform. Not the mistake 04-01 made with the raised highlight:
    // that was one field serving two DIFFERENT declared values. Both rules are
    // cross-checked against this row, so the day either moves, it fails.
    { "Strip micro-label",            Style::stripMicroLabel,      9.0f, Face::sansRegular,     0.08f,  true,  1.00f },

    // `<b>` with an explicit `font-weight: 400` (css:349), so regular and not
    // the bold the tag would otherwise give it.
    { "Ghost readout",                Style::ghostValue,          10.0f, Face::monoRegular,     0.00f,  false, 1.00f },

    // The header's two, also from forrobox.css and also absent from
    // PLANNING.md's table. `.mini-btn` declares no font-weight and no
    // letter-spacing (css:181-183), so both inherit their defaults: regular,
    // and no tracking.
    { "Mini button",                  Style::miniButtonLabel,     10.0f, Face::monoRegular,     0.00f,  false, 1.00f },
    { "Preset screen",                Style::presetScreen,        10.5f, Face::monoRegular,     0.01f,  false, 1.00f },

    // The footer's three, from forrobox.css and also absent from PLANNING.md's
    // table.
    //
    // `.drag-midi .dm-arrow` (css:539) declares only size and colour, so its
    // face is the `--sans` css:60 gives the document. Drawn as TEXT and not as
    // a path, which was the alternative: U+2197 already inks in this same face
    // for the sub-dots label, and a path would be a second way of saying
    // "16 px" that no cross-check could compare to the stylesheet. The glyph
    // coverage test carries U+2193 for exactly that reason.
    { "DRAG MIDI arrow",              Style::dragMidiArrow,       16.0f, Face::sansRegular,     0.00f,  false, 1.00f },

    //
    // `.drag-midi .dm-sub` (css:543) declares family, size, colour and opacity
    // and nothing else, so weight and tracking are the inherited defaults. The
    // 0.7 is the rule's OWN `opacity`, carried in the row for the reason every
    // dimmed row carries it: the alternative is a bare 0.7f at the call site
    // that nothing cross-checks, which is how the light header shipped its
    // highlight at 0.50.
    { "DRAG MIDI sub-label",          Style::dragMidiSub,          9.0f, Face::monoRegular,     0.00f,  false, 0.70f },

    // `.out-toggle .ot` (css:546-548) — 9px/500/0.08em, uppercase. NOT the
    // STYLE control's `.quick-switch b`, which is 10px mono at 0.06em: the two
    // segmented controls share a component and not a type row.
    { "OUTPUT toggle label",          Style::outToggleLabel,       9.0f, Face::sansMedium,      0.08f,  true,  1.00f },

    // The sequencer's head row. `.seq-len` (css:500-503) carries BOTH the
    // isolate hint and the STEPS label — one rule, one row. It is 0.1em where
    // `.sect-label` beside it is 0.2em, which is why SEQUENCER cannot share it.
    { "Sequencer hint",               Style::seqHint,              9.0f, Face::sansRegular,     0.10f,  true,  1.00f },
    { "Kit overlay title",            Style::kitTitle,            14.0f, Face::sansSemiBold,    0.08f,  false, 1.00f },
    { "Kit overlay sub-line",         Style::kitSubLine,          10.0f, Face::sansRegular,     0.04f,  false, 1.00f },
    { "Kit row name",                 Style::kitRowName,          12.0f, Face::sansSemiBold,    0.08f,  false, 1.00f },
    { "Kit row full name",            Style::kitRowFull,           9.0f, Face::sansRegular,     0.04f,  false, 1.00f },
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

/** `text-overflow: ellipsis` — the text, shortened until it fits `maxWidth`
    with a trailing ellipsis, or unchanged when it already fits.

    Measured with `trackedWidth`, so the width that decides is the width that
    will be drawn. A `juce::Font::getStringWidth` here would ignore the tracking
    and cut the string at the wrong place — which is the shape 04-01 found when
    the tracking law was written twice. */
juce::String ellipsised (Style, const juce::String& text, float maxWidth);

/** The CSS box model for a content-sized row: the type row's own px height,
    plus its declared padding and border.

    ONE definition. `ChassisLayout::textBox`, `Button::heightOf`,
    `ValueScreen::heightOf` and `Segmented::heightOf` were four identical
    spellings of it — and `textBox`'s own docstring says it exists so "the law
    is written once and every content-sized box goes through it", which three of
    the four did not. Found by /simplify.

    Rounded up rather than truncated, because two of the four rounded and two
    truncated: one law with two roundings is the shape 04-03 already fixed once
    between kSubDotsRowHeight and Button::preferredHeight. */
constexpr int boxHeight (Style style, int padY = 0, int border = 0) noexcept
{
    return static_cast<int> (styleFor (style).heightPx + 0.5f) + padY * 2 + border * 2;
}

/** One tracked run as a glyph OUTLINE, at the origin, with its width.

    For the two things a caller cannot do with `drawTracked`: give the glyphs a
    real drop shadow that follows their shape, and draw them twice from ONE
    layout. `ValueScreen` needs both — css:597's `text-shadow` is a blur of the
    letters, not of their box — and it used to hand-roll the blur into an
    offscreen image because a comment claimed juce::DropShadow could not do it.
    It can: `DropShadow::drawForPath` (juce_DropShadowEffect.h:56). Measured
    5x faster, and it removes an image allocation per paint. Found by /simplify. */
struct TrackedRun
{
    juce::Path path;
    float      width { 0.0f };
};

TrackedRun trackedRun (Style, juce::StringRef text);

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
