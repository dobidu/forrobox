/* ============================================================================
   FORRÓ BOX — design tokens, both themes

   Every colour in the UI comes from this table and nowhere else. The values are
   NOT trusted: scripts/verify-theme.py compares them against forrobox.css's
   `:root` and `[data-theme="light"]` blocks (and app.js's SUBCOLORS) on every
   build, both directions, and fails the build naming any token that diverged.

   That check exists for the same reason the groove tables are cross-checked
   against data.js: a wrong hex digit produces no crash, no failing test and no
   visible error — only a colour that is subtly wrong, with no way to tell which
   digit. A unit test holding the expected hexes by hand would duplicate the
   typo risk it is supposed to catch.
============================================================================ */
#pragma once

#include <juce_graphics/juce_graphics.h>

#include <array>

namespace forrobox::theme
{

/** Which palette is in force. Both themes are first-class — the light (OP-1
    cream) theme is a deliberate differentiator, not an afterthought. */
enum class Mode { dark, light };

/** The surface, text and line tokens. One enum value per CSS custom property,
    named as the property is. */
enum class Token
{
    bg,            ///< --bg        window background / chassis
    panel,         ///< --panel     channel strips, buttons, inset panels
    raised,        ///< --raised    header, side panel, footer
    sunken,        ///< --sunken    sequencer well, recessed groups, meters
    fg,            ///< --fg        primary text, knob indicator lines
    fgDim,         ///< --fg-dim    secondary text, inactive button labels
    fgFaint,       ///< --fg-faint  micro-labels, section labels
    line,          ///< --line      hairline dividers
    lineStrong,    ///< --line-strong  button borders, knob tracks
    active,        ///< --active    active/selected fill
    hub,           ///< --hub       knob hub fill
    danger,        ///< --danger    mute, limiter GR, Ciclotron accent
    screen,        ///< --screen    readout/LCD background
    screenFg,      ///< --screen-fg readout text
};

inline constexpr int kNumTokens = 14;

/** One token, in both themes.

    Both values in one row so a token cannot exist in one theme and be missing
    from the other — the same argument as `ids::channelInfos`. Where the
    stylesheet does not redefine a token under `[data-theme="light"]` it
    INHERITS `:root`, so the two values here are deliberately equal; that is a
    property the cross-check asserts rather than an oversight. */
struct TokenSpec
{
    const char*     cssName;
    Token           token;
    juce::uint32    dark;    ///< ARGB
    juce::uint32    light;   ///< ARGB
};

inline constexpr std::array<TokenSpec, kNumTokens> tokenSpecs {{
    // --danger, the accents and the tweakables are NOT redefined for light;
    // equal values below mean "inherits :root", not "not yet filled in".
    { "--bg",          Token::bg,         0xff141414, 0xffe6e1d6 },
    { "--panel",       Token::panel,      0xff1e1e1e, 0xfff1ede4 },
    { "--raised",      Token::raised,     0xff242424, 0xfffaf7f0 },
    { "--sunken",      Token::sunken,     0xff0f0f0f, 0xffd6d0c3 },
    { "--fg",          Token::fg,         0xffe8e8e8, 0xff191919 },
    { "--fg-dim",      Token::fgDim,      0x80e8e8e8, 0x8c191919 },
    { "--fg-faint",    Token::fgFaint,    0x4de8e8e8, 0x57191919 },
    { "--line",        Token::line,       0x17ffffff, 0x1f000000 },
    { "--line-strong", Token::lineStrong, 0x33ffffff, 0x47000000 },
    { "--active",      Token::active,     0xb3ffffff, 0xc7000000 },
    { "--hub",         Token::hub,        0xff181818, 0xffefeadf },
    { "--danger",      Token::danger,     0xffff4136, 0xffff4136 },
    { "--screen",      Token::screen,     0xff0a0a0a, 0xffcfc8b8 },
    { "--screen-fg",   Token::screenFg,   0xffd8d8d8, 0xff2a2620 },
}};

/** The five instrument accents. Identical in both themes by design: colour
    carries instrument identity, and identity does not change with the theme. */
enum class Accent { zabumba, triangulo, pandeiro, ganza, bateria };

inline constexpr int kNumAccents = 5;

struct AccentSpec
{
    const char*  cssName;
    Accent       accent;
    juce::uint32 argb;
};

inline constexpr std::array<AccentSpec, kNumAccents> accentSpecs {{
    { "--c-zabumba",   Accent::zabumba,   0xffe8650a },
    { "--c-triangulo", Accent::triangulo, 0xff00c2c7 },
    { "--c-pandeiro",  Accent::pandeiro,  0xfff2c200 },
    { "--c-ganza",     Accent::ganza,     0xff7abf6e },
    { "--c-bateria",   Accent::bateria,   0xffe84646 },
}};

/** The four bateria kit pieces, in lane order (BB, CX, HH, TOM).

    These are the ONLY colours not in forrobox.css — they live in `app.js:227`
    as `SUBCOLORS`, and the cross-check reads them from there. */
inline constexpr std::array<juce::uint32, 4> subColours {
    0xffe84646, 0xfff2887a, 0xfff4b6ae, 0xff9b2f2f
};

/** `--r`: corner radius. 2 px by the spec, 0 offered as the "hard" setting.
    Nested groups use `--r + 1` … `--r + 3`, so those are derived, not stored. */
inline constexpr float kCornerRadius = 2.0f;

/** `--accent-i`: accent intensity, 0..1. Scales the accent glows and the
    saturation of value arcs and fills. A tweakable, so it is a variable with a
    default rather than a constant folded into each call site. */
inline constexpr float kAccentIntensity = 1.0f;

/** The token's colour in the given theme. */
juce::Colour colour (Token, Mode) noexcept;

/** An instrument accent. No `Mode` parameter — deliberately: accents do not
    vary by theme, and a parameter that is always ignored invites a caller to
    believe it is not. */
juce::Colour accent (Accent) noexcept;

/** A bateria kit piece by index 0..3 (BB, CX, HH, TOM). */
juce::Colour subColour (int index) noexcept;

/** `color-mix(in srgb, base <weight>%, other)` — the CSS function the design
    uses for the anchor tint and every "colour + N% white" highlight. Mixes in
    plain sRGB, as the stylesheet asks for, and NOT in a perceptual space: the
    numbers in the design were chosen against this behaviour. */
juce::Colour mix (juce::Colour base, juce::Colour other, float otherWeight) noexcept;

/** The shadow recipes, per theme. Carried here rather than inlined per
    component because they are as much of the look as the colours are, and
    because both themes specify different ones for the same surface. */
struct Shadows
{
    juce::Colour recessedInner;      ///< inset 0 1px 3px  — screens and insets
    juce::Colour recessedOutline;    ///< inset 0 0 0 1px  — dark theme only
    juce::Colour raisedHighlight;    ///< inset 0 1px 0    — top edge of raised panels
    juce::Colour wellShadow;         ///< inset 0 2px 6px  — the sequencer well
    float        wellRadius;
    juce::Colour padRecessInner;     ///< inset 0 1px 1px  — an unlit step pad
    juce::Colour padRecessOutline;
};

Shadows shadowsFor (Mode) noexcept;

/** `text-shadow: 0 0 8px <screen-fg at 30%>` on every mono readout. */
inline constexpr float kScreenGlowRadius  = 8.0f;
inline constexpr float kScreenGlowOpacity = 0.30f;

} // namespace forrobox::theme
