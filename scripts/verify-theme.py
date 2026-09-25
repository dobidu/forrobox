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

import gate_inputs

ROOT = pathlib.Path(__file__).resolve().parent.parent
CSS = ROOT / "forrobox.css"
APP_JS = ROOT / "app.js"
THEME_H = ROOT / "src" / "Theme.h"
THEME_CPP = ROOT / "src" / "Theme.cpp"
# The easter egg's wash gradients live here. Read since 08-04 and undeclared until 10-02 —
# an edit to them did not re-run this gate.
EFFECT_OVERLAY_CPP = ROOT / "src" / "EffectOverlay.cpp"

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


def parse_css_declarations(css: str, selector: str) -> dict[str, str]:
    """EVERY declaration in one selector's block, not only the custom properties.

    `parse_css_block` above reads `--name: value` pairs because that is all the
    token cross-check ever wanted. The drunk wash is declared with ordinary
    properties — `background`, `mix-blend-mode` — so it needs the general form.

    Kept separate rather than widening that one, and the REASON matters because
    the first version of this docstring gave the wrong one: it said a dict also
    holding `box-shadow` "would answer a lookup that used to be a KeyError",
    which cannot happen — every caller indexes by a `--`-prefixed key and an
    ordinary property can never collide with one. The real reason is that
    `main()` ITERATES the `:root` dict (`sorted(set(dark) - cpp_names - ...)`)
    to report tokens the CSS declares and Theme.h does not, and every ordinary
    property would be reported as an unknown token. /simplify."""
    match = re.search(re.escape(selector) + r"\s*\{(.*?)\}", css, re.S)
    if match is None:
        sys.exit(f"FAIL: could not find the `{selector}` block in {CSS.name}")

    return {
        name.strip(): value.strip()
        for name, value in re.findall(r"([a-z-]+)\s*:\s*([^;]+);", match.group(1))
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


def check_drunk_wash(css: str, failures: list[str]) -> int:
    """`.fb-window::after` — the easter egg's wash — against EffectOverlay.cpp.

    Every number in those three gradients is the design's: two colours, three
    alphas at stop 0, two end stops, four ellipse radii and four centres, and
    the 65/35 that `--drunk` is clamped against. They are typed into C++ and
    nowhere else compares them — which is the shape of the menu-id collision
    08-02 shipped, where every term of the guard was written in the test file
    that was supposed to catch it.

    The two colours are checked as IDENTITY against the accent table rather than
    as literals: css writes `rgba(232,101,10,…)` where it means `--c-zabumba`,
    and EffectOverlay.cpp reads `theme::accent`. If the stylesheet ever moves one
    without moving the other, this is what notices.
    """
    source = EFFECT_OVERLAY_CPP.read_text(encoding="utf-8")

    rule = parse_css_declarations(css, ".fb-window::after")
    background = rule.get("background")

    if background is None:
        failures.append("drunk wash: no `background` on `.fb-window::after` in the CSS")
        return 0

    if "screen" not in rule.get("mix-blend-mode", ""):
        failures.append("drunk wash: `.fb-window::after` no longer asks for mix-blend-mode: screen, "
                        "which is the whole reason EffectOverlay owns a pixel pass")

    root = parse_css_block(css, ":root")
    checked = 0

    # ── the two radial layers ────────────────────────────────────────────────
    radials = re.findall(
        r"radial-gradient\(\s*([\d.]+)%\s+([\d.]+)%\s+at\s+(-?[\d.]+)%\s+(-?[\d.]+)%\s*,"
        r"\s*rgba\(([^)]*)\)\s*,\s*transparent\s+([\d.]+)%",
        background)

    cpp_radials = re.findall(
        r"\{\s*theme::Accent::(\w+),\s*([\d.]+)f,\s*([\d.]+)f,\s*"
        r"(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f\s*\}",
        source)

    if len(radials) != 2 or len(cpp_radials) != 2:
        failures.append(f"drunk wash: found {len(radials)} radial gradients in the CSS and "
                        f"{len(cpp_radials)} rows in kRadialLayers — expected 2 of each")
        return 0

    for index, (css_layer, cpp_layer) in enumerate(zip(radials, cpp_radials)):
        rx, ry, cx, cy, rgba, end = css_layer
        accent, alpha, cpp_end, cpp_cx, cpp_cy, cpp_rx, cpp_ry = cpp_layer

        css_argb = css_colour_to_argb(f"rgba({rgba})")
        accent_value = root.get(f"--c-{accent}")
        expected = css_colour_to_argb(accent_value) if accent_value else None

        if expected is None:
            failures.append(f"drunk wash layer {index}: --c-{accent} is not declared in :root")
        elif css_argb is None or (css_argb & 0x00FFFFFF) != (expected & 0x00FFFFFF):
            failures.append(f"drunk wash layer {index}: css rgba({rgba}) is not --c-{accent} "
                            f"({accent_value}) — EffectOverlay.cpp reads the accent")
        checked += 1

        pairs = [("stop-0 alpha", (css_argb >> 24) / 255.0 if css_argb else 0.0, float(alpha)),
                 ("end stop",     float(end) / 100.0,  float(cpp_end)),
                 ("centre x",     float(cx) / 100.0,   float(cpp_cx)),
                 ("centre y",     float(cy) / 100.0,   float(cpp_cy)),
                 ("radius x",     float(rx) / 100.0,   float(cpp_rx)),
                 ("radius y",     float(ry) / 100.0,   float(cpp_ry))]

        for name, want, got in pairs:
            checked += 1
            if abs(want - got) > 0.005:
                failures.append(f"drunk wash layer {index} {name}: EffectOverlay.cpp {got} "
                                f"!= CSS {want}")

    # ── the linear layer ─────────────────────────────────────────────────────
    linear = re.search(r"linear-gradient\(\s*180deg\s*,\s*rgba\(([^)]*)\)\s*,"
                       r"\s*rgba\(([^)]*)\)", background)

    if linear is None:
        failures.append("drunk wash: no `linear-gradient(180deg, …)` layer in `.fb-window::after`")
    else:
        for name, group, cpp_name in (("top", 1, "kLinearTopAlpha"), ("end", 2, "kLinearEndAlpha")):
            argb = css_colour_to_argb(f"rgba({linear.group(group)})")
            got = parse_float_constant(source, cpp_name)
            checked += 1

            if argb is None:
                failures.append(f"drunk wash linear {name}: rgba({linear.group(group)}) is not a colour")
            elif abs((argb >> 24) / 255.0 - got) > 0.005:
                failures.append(f"drunk wash linear {name} alpha: EffectOverlay.cpp {got} "
                                f"!= CSS {(argb >> 24) / 255.0:.4f}")

    # `--drunk`'s own clamp used to be read here, out of `DrunkOverlay.h`, as it then was —
    # because that is where the two constants happened to live, not because this
    # script is about clamps. 08-05 moved them to `src/Effects.h` beside every
    # other number of the same feature, and `verify-geometry.py` compares them
    # there against the same app.js line. This script keeps the colour and
    # gradient work it is for.
    return checked


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

    drunk_values = check_drunk_wash(css, failures)

    if failures:
        return exit_with(failures)

    print(f"Theme cross-check OK — {len(token_rows)} tokens x 2 themes, "
          f"{len(accent_names)} accents, 4 bateria colours, 2 tweakables, "
          f"{len(highlight_rules)} raised-edge highlights, "
          f"{drunk_values} drunk-wash values")
    return 0


def exit_with(failures: list[str]) -> int:
    print("Theme cross-check FAILED", file=sys.stderr)
    for line in failures:
        print(f"  {line}", file=sys.stderr)
    return 1


# Everything this gate reads, in one place — CMake depends on exactly this (gate_inputs.py).
INPUTS = gate_inputs.declare(__name__, files=[CSS, APP_JS, THEME_H, THEME_CPP, EFFECT_OVERLAY_CPP])


if __name__ == "__main__":
    sys.exit(gate_inputs.run(main))
