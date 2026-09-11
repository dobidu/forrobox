#!/usr/bin/env python3
"""Prove the C++ design tokens still match forrobox.css, the design source.

Why this exists: a wrong hex digit in a colour table produces no crash, no
failed build and no failing test — only a colour that is subtly wrong, with no
way to tell which digit. A unit test holding the expected hexes by hand would
duplicate the same typo risk it is meant to catch. Comparing against the
stylesheet is the only check here with real signal. Same argument, and same
shape, as verify-profiles.py does for the groove tables against data.js.

What it covers:
  * the 14 surface/text/line tokens, in BOTH themes
  * the 5 instrument accents
  * the 4 bateria kit colours, which live in app.js (SUBCOLORS), not the CSS
  * --r and --accent-i
  * the two `inset 0 1px 0` raised-edge highlights, in BOTH themes. Added
    because this gap was not theoretical: one shared C++ field covered two
    different CSS values and shipped the light header at 0.50 where the
    stylesheet says 0.05, and nothing here could see it.

What it does NOT cover, stated so the gap is known rather than assumed closed:
  * the type scale. PLANNING.md declares it as a table but the CSS spreads it
    across ~20 rules, so src/Typography.h is transcribed from the handoff by
    hand. Each row carries the spec's own element name to keep it diffable.
  * the remaining shadow and gradient recipes — the recessed/well/pad layers
    and the header gradient. Same argument as the type scale; the highlights
    are covered because they are the pair that actually diverged.

Light-theme inheritance is modelled explicitly: a token the stylesheet does not
redefine under [data-theme="light"] INHERITS :root, so the C++ table is required
to carry equal dark and light values for it. That is asserted, not assumed — an
accidentally-diverging pair would otherwise read as intentional.

Exit 0 when everything matches; exit 1 naming every token that diverged.
"""
from __future__ import annotations

import math
import pathlib
import re
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CSS = ROOT / "forrobox.css"
APP_JS = ROOT / "app.js"
THEME_H = ROOT / "src" / "Theme.h"
THEME_CPP = ROOT / "src" / "Theme.cpp"

# Tokens the stylesheet declares in :root but deliberately does not redefine for
# the light theme, so both C++ values must be equal.
INHERITED_BY_LIGHT = {"--danger"}


def parse_css_block(css: str, selector: str) -> dict[str, str]:
    """The custom properties declared in one selector's block."""
    match = re.search(re.escape(selector) + r"\s*\{(.*?)\}", css, re.S)
    if match is None:
        sys.exit(f"FAIL: could not find the `{selector}` block in {CSS.name}")

    return {
        name: value.strip()
        for name, value in re.findall(r"(--[a-z0-9-]+)\s*:\s*([^;]+);", match.group(1))
    }


def float_to_uint8(value: float) -> int:
    """juce::ColourHelpers::floatToUInt8, replicated exactly.

    `return n <= 0.0f ? 0 : (n >= 1.0f ? 255 : (uint8) roundToInt (n * 255.0f));`
    — juce_Colour.cpp:42. Two details, and getting either wrong makes this
    script disagree with correct code:

      * the arithmetic is float32, not double. 0.3f is 0.300000012, so
        0.3f * 255.0f is 76.500003 and rounds up; 0.7f is 0.699999988, and
        0.7f * 255.0f lands on exactly 178.5 in float32.
      * juce::roundToInt rounds half AWAY FROM ZERO. Python's round() is
        banker's rounding, which sends both .5 cases the other way — this
        function first used it and reported two false failures against a
        correct table, for --fg-faint (0.3) and --active (0.7).
    """
    if value <= 0.0:
        return 0
    if value >= 1.0:
        return 255

    scaled = struct.unpack("f", struct.pack("f", struct.unpack(
        "f", struct.pack("f", value))[0] * 255.0))[0]

    return int(math.floor(scaled + 0.5))


def css_colour_to_argb(value: str) -> int | None:
    """`#rrggbb` or `rgba(r, g, b, a)` as 0xAARRGGBB. None if not a colour."""
    value = value.strip()

    hex_match = re.fullmatch(r"#([0-9a-fA-F]{6})", value)
    if hex_match:
        return 0xFF000000 | int(hex_match.group(1), 16)

    rgba = re.fullmatch(r"rgba\(\s*([\d.]+)\s*,\s*([\d.]+)\s*,\s*([\d.]+)\s*,\s*([\d.]+)\s*\)", value)
    if rgba:
        r, g, b = (int(float(rgba.group(i))) for i in (1, 2, 3))
        a = float_to_uint8(float(rgba.group(4)))
        return (a << 24) | (r << 16) | (g << 8) | b

    return None


def parse_cpp_table(struct_rows: str, value_indices: list[int]) -> list[tuple]:
    """Rows of a `{ "--token", Enum::x, 0x..., 0x... }` initialiser list."""
    rows = []
    for line in struct_rows.splitlines():
        line = line.strip()
        if not line.startswith("{ \""):
            continue

        fields = [f.strip() for f in line.strip("{},").split(",")]
        name = fields[0].strip('"')
        values = [int(fields[i], 16) for i in value_indices]
        rows.append((name, *values))

    return rows


def extract_initialiser(header: str, marker: str) -> str:
    """The body of the initialiser list that follows `marker`."""
    start = header.find(marker)
    if start < 0:
        sys.exit(f"FAIL: could not find `{marker}` in {THEME_H.name}")

    open_brace = header.find("{{", start)
    close_brace = header.find("}}", open_brace)
    if open_brace < 0 or close_brace < 0:
        sys.exit(f"FAIL: could not read the initialiser after `{marker}`")

    return header[open_brace + 2:close_brace]


def parse_float_constant(header: str, name: str) -> float:
    match = re.search(re.escape(name) + r"\s*=\s*([0-9.]+)f?\s*;", header)
    if match is None:
        sys.exit(f"FAIL: could not find `{name}` in {THEME_H.name}")

    return float(match.group(1))


def css_box_shadow_colour(css: str, selector: str) -> str | None:
    """The colour of an `inset 0 1px 0 <colour>` box-shadow on one selector."""
    match = re.search(re.escape(selector) + r"\s*\{[^}]*box-shadow:\s*([^;}]+)", css, re.S)
    if match is None:
        return None

    shadow = re.search(r"inset\s+0\s+1px\s+0\s+(rgba?\([^)]*\))", match.group(1))
    return shadow.group(1) if shadow else None


def parse_shadow_field(source: str, field: str, mode: str) -> float | None:
    """The float alpha of one `Shadows` field in one branch of shadowsFor().

    Reads the `white (0.05f)` / `black (0.45f)` spelling the designated
    initialisers use. Returns None if the field is not a white highlight.
    """
    body = source.split("if (mode == Mode::dark)", 1)
    if len(body) != 2:
        return None

    dark_branch, light_branch = body[1].split("return {", 2)[1], body[1].split("return {")[2]
    branch = dark_branch if mode == "dark" else light_branch

    match = re.search(r"\." + field + r"\s*=\s*white\s*\(\s*([\d.]+)f?\s*\)", branch)
    return float(match.group(1)) if match else None


def parse_css_length(value: str) -> float:
    return float(re.sub(r"[a-z%]+$", "", value.strip()))


def main() -> int:
    css = CSS.read_text(encoding="utf-8")
    header = THEME_H.read_text(encoding="utf-8")

    dark = parse_css_block(css, ":root")
    light = parse_css_block(css, '[data-theme="light"]')

    failures: list[str] = []

    # ── the 14 surface / text / line tokens, both themes ────────────────────
    token_rows = parse_cpp_table(extract_initialiser(header, "tokenSpecs"), [2, 3])

    if not token_rows:
        return exit_with(["could not parse any rows out of tokenSpecs"])

    for name, cpp_dark, cpp_light in token_rows:
        if name not in dark:
            failures.append(f"{name}: in Theme.h but not in the CSS :root block")
            continue

        expected_dark = css_colour_to_argb(dark[name])
        if expected_dark is None:
            failures.append(f"{name}: :root value {dark[name]!r} is not a colour")
        elif cpp_dark != expected_dark:
            failures.append(f"{name} dark:  Theme.h 0x{cpp_dark:08x} != CSS 0x{expected_dark:08x} ({dark[name]})")

        if name in light:
            expected_light = css_colour_to_argb(light[name])
            if expected_light is None:
                failures.append(f"{name}: light value {light[name]!r} is not a colour")
            elif cpp_light != expected_light:
                failures.append(f"{name} light: Theme.h 0x{cpp_light:08x} != CSS 0x{expected_light:08x} ({light[name]})")
        else:
            # Not redefined for light, so it inherits :root and both C++ values
            # must be the dark one. Asserted rather than assumed.
            if name not in INHERITED_BY_LIGHT:
                failures.append(f"{name}: not redefined under [data-theme=\"light\"], and not listed "
                                f"as inherited in this script — add it to INHERITED_BY_LIGHT or to the CSS")
            elif cpp_light != expected_dark:
                failures.append(f"{name} light: inherits :root, so Theme.h should carry "
                                f"0x{expected_dark:08x}, not 0x{cpp_light:08x}")

    # Neither side may carry a token the other lacks.
    accent_rows = parse_cpp_table(extract_initialiser(header, "accentSpecs"), [2])

    cpp_names = {row[0] for row in token_rows}
    accent_names = {name for name, *_ in accent_rows}
    structural = {"--r", "--accent-i", "--sans", "--mono"}

    for name in sorted(set(dark) - cpp_names - accent_names - structural):
        failures.append(f"{name}: declared in the CSS :root block but absent from Theme.h")

    for name in sorted(set(light) - cpp_names - accent_names):
        failures.append(f"{name}: declared for the light theme but absent from Theme.h")

    # ── the 5 instrument accents ────────────────────────────────────────────
    for name, cpp_argb in accent_rows:
        if name not in dark:
            failures.append(f"{name}: in Theme.h but not in the CSS")
            continue

        expected = css_colour_to_argb(dark[name])
        if expected is None:
            failures.append(f"{name}: CSS value {dark[name]!r} is not a colour")
        elif cpp_argb != expected:
            failures.append(f"{name}: Theme.h 0x{cpp_argb:08x} != CSS 0x{expected:08x} ({dark[name]})")

        if name in light:
            failures.append(f"{name}: redefined for the light theme, but Theme.h treats accents as "
                            f"theme-independent — one of the two is wrong")

    # ── the 4 bateria kit colours, from app.js ──────────────────────────────
    app_js = APP_JS.read_text(encoding="utf-8")
    subs_match = re.search(r"SUBCOLORS\s*=\s*\[([^\]]*)\]", app_js)
    if subs_match is None:
        failures.append("could not find SUBCOLORS in app.js")
    else:
        expected_subs = [0xFF000000 | int(h, 16)
                         for h in re.findall(r"#([0-9a-fA-F]{6})", subs_match.group(1))]

        cpp_subs = [int(h, 16) for h in
                    re.findall(r"0x([0-9a-fA-F]{8})",
                               re.search(r"subColours\s*\{([^}]*)\}", header).group(1))]

        if len(cpp_subs) != len(expected_subs):
            failures.append(f"bateria kit colours: Theme.h has {len(cpp_subs)}, app.js has {len(expected_subs)}")
        else:
            for index, (got, want) in enumerate(zip(cpp_subs, expected_subs)):
                if got != want:
                    lane = ("BB", "CX", "HH", "TOM")[index] if index < 4 else str(index)
                    failures.append(f"bateria {lane}: Theme.h 0x{got:08x} != app.js 0x{want:08x}")

    # ── the two tweakables ──────────────────────────────────────────────────
    for cpp_name, css_name in (("kCornerRadius", "--r"), ("kAccentIntensity", "--accent-i")):
        if css_name not in dark:
            failures.append(f"{css_name}: not declared in the CSS :root block")
            continue

        cpp_value = parse_float_constant(header, cpp_name)
        css_value = parse_css_length(dark[css_name])
        if abs(cpp_value - css_value) > 1e-6:
            failures.append(f"{css_name}: Theme.h {cpp_name} = {cpp_value} != CSS {css_value}")

    # ── the two `inset 0 1px 0` raised-edge highlights, both themes ─────────
    #
    # The header and the side/footer share the MECHANISM and not the value.
    # A single C++ field for both is what shipped the light header 10x too
    # bright, so both are pinned here, per theme, against their own CSS rule.
    theme_cpp = THEME_CPP.read_text(encoding="utf-8")

    highlight_rules = [
        ("headerHighlight", "dark",  ".fb-window > .header"),
        ("headerHighlight", "light", ".fb-window > .header"),
        ("raisedHighlight", "dark",  ".side, .footer"),
        ("raisedHighlight", "light", '[data-theme="light"] .side, [data-theme="light"] .footer'),
    ]

    for field, mode, selector in highlight_rules:
        css_colour = css_box_shadow_colour(css, selector)
        if css_colour is None:
            failures.append(f"{field} {mode}: no `inset 0 1px 0` box-shadow on `{selector}` in the CSS")
            continue

        expected = css_colour_to_argb(css_colour)
        if expected is None:
            failures.append(f"{field} {mode}: CSS value {css_colour!r} is not a colour")
            continue

        if (expected & 0x00FFFFFF) != 0x00FFFFFF:
            failures.append(f"{field} {mode}: CSS {css_colour} is not a white highlight")
            continue

        cpp_alpha = parse_shadow_field(theme_cpp, field, mode)
        if cpp_alpha is None:
            failures.append(f"{field} {mode}: could not read a `white (a)` value out of shadowsFor()")
            continue

        if float_to_uint8(cpp_alpha) != (expected >> 24):
            failures.append(f"{field} {mode}: Theme.cpp white({cpp_alpha}) != CSS {css_colour}"
                            f" [{selector}]")

    if failures:
        return exit_with(failures)

    print(f"Theme cross-check OK — {len(token_rows)} tokens x 2 themes, "
          f"{len(accent_names)} accents, 4 bateria colours, 2 tweakables, "
          f"{len(highlight_rules)} raised-edge highlights")
    return 0


def exit_with(failures: list[str]) -> int:
    print("Theme cross-check FAILED", file=sys.stderr)
    for line in failures:
        print(f"  {line}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
