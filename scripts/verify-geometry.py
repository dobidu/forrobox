#!/usr/bin/env python3
"""Prove the C++ strip geometry still matches forrobox.css, the design source.

Why this exists, and why it is not the same as verify-theme.py:

  04-02 added ~350 assertions over ChassisLayout's strip stack — order,
  non-overlap, containment, the declared gaps. Three negative controls then
  showed every NUMERIC one of them to be structurally incapable of failing:
  the tests compare the derived layout against the SAME constants the layout
  is derived from, so changing kStripPadTop from 12 to 13 moves both sides
  together and 1695 checks stay green.

  That is 04-01's "a value cross-check does not prove the value reaches a
  pixel" lesson running in the other direction: a C++ test cannot police a
  constant it also consumes. The colour tables already learned this — they
  are compared against the stylesheet, never transcribed and trusted. This is
  the same instrument for the box model.

  So the division of labour is deliberate:
    * this script      — are the NUMBERS the stylesheet's numbers?
    * tests/UiTest.cpp — do the boxes stack in the right ORDER, without
                         overlapping, inside the strip? (controlled: swapping
                         two rows in the derivation fails 15 checks)

What it covers: every strip-stack length that forrobox.css declares
explicitly. Content-sized text rows are NOT covered and cannot be — the CSS
lets the font size them, so Chassis.h derives those from the type scale plus
the declared padding, and each carries the source line in a comment.

Exit 0 when everything matches; exit 1 naming every constant that diverged.
"""
from __future__ import annotations

import pathlib
import re
import sys

MISSING: list[str] = []

ROOT = pathlib.Path(__file__).resolve().parent.parent
CSS = ROOT / "forrobox.css"

# controls.js carries the knob's SVG geometry — the radii, the sweep and the
# indicator tip. Those are not in the stylesheet and cannot be: they are path
# geometry, not style. Until they were read here, the five numbers PLANNING.md
# calls the knob's identity were policed by nothing at all — every C++
# assertion about them multiplies the same constant it checks, and the
# indicator tests profile the angular SECTOR, so they measure direction and
# never length. kIndicatorTipY could have been 40 and stayed green.
CONTROLS_JS = ROOT / "controls.js"
# app.js carries the velocity law and the ghost threshold. Those are BEHAVIOUR
# in the prototype, not style, so they appear in no stylesheet — and the C++
# tests assert `opacityForVelocity` against the same two constants it is built
# from, so before this file was read they were policed by nothing. The ghost
# threshold is the one that matters: at 42 it is the prototype's, at any other
# value the pad silently disagrees with the design about which hits are ghosts.
APP_JS = ROOT / "app.js"
# Read every header that declares design geometry. Knob.h arrives with 04-02's
# Task 2; a missing file is a hard failure rather than a silent skip, because a
# skip would make every knob expectation below a check that cannot fail.
GEOMETRY_HEADERS = [ROOT / "src" / "Chassis.h", ROOT / "src" / "Knob.h",
                    ROOT / "src" / "Button.h", ROOT / "src" / "StepPad.h",
                    ROOT / "src" / "Fader.h"]

# The type scale is a table of rows, not a list of named constants, so it needs
# its own reader. Before this, the only thing policing a font size was the row's
# own comment: PLANNING.md's table covers 21 rows and five of the strip's come
# from forrobox.css alone, where a transcribed 9 for a declared 10 is invisible.
TYPOGRAPHY_HEADER = ROOT / "src" / "Typography.h"


def css_rules(css: str, selector: str) -> list[str]:
    """EVERY declaration block for one selector, in source order.

    The step pad needs this: `.pad`, `.pad.on` and `[data-theme="light"] .pad`
    are each declared TWICE — once in the layout section and again in the
    "EP-133 feel" section at css:615, which is where the gradients and shadows
    live. `css_rule` returns the first block, which for the pad is the one
    WITHOUT any of the numbers this script is checking.
    """
    blocks, start = [], 0

    while True:
        block, start = _next_rule(css, selector, start)

        if block is None:
            break

        blocks.append(block)

    if not blocks:
        sys.exit(f"FAIL: could not find a rule beginning with `{selector}` in {CSS.name}")

    return blocks


def css_rule(css: str, selector: str) -> str:
    """The FIRST declaration block of one selector, brace-matched."""
    return css_rules(css, selector)[0]


def _next_rule(css: str, selector: str, search_from: int) -> tuple[str | None, int]:
    """The next block for `selector` at or after `search_from`, and where to resume.

    Brace-matched rather than a lazy `\\{(.*?)\\}`: verify-profiles.py records
    that a lazy match silently captured the wrong block once already, and these
    blocks contain `calc(...)` and nested functions.
    """
    start = css.find(selector, search_from)
    while start >= 0:
        # Two rejections, both of which bit this script while it was written:
        #   * a PREFIX match — ".strip" must not match ".strip-div"
        #   * a match inside a LONGER selector — ".subdots-label" must not
        #     match the ".subdots:hover .subdots-label" rule three lines
        #     above it, which declares only a colour. So the selector has to
        #     BEGIN its rule, i.e. sit at the start of its own line.
        if start + len(selector) >= len(css):
            break

        after = css[start + len(selector)]
        line_start = css.rfind("\n", 0, start) + 1
        begins_rule = css[line_start:start].strip() == ""

        if after in " \t\n{," and begins_rule:
            break

        start = css.find(selector, start + 1)

    if start < 0:
        return None, len(css)

    open_brace = css.index("{", start)
    depth, i = 0, open_brace
    while i < len(css):
        if css[i] == "{":
            depth += 1
        elif css[i] == "}":
            depth -= 1
            if depth == 0:
                return css[open_brace + 1 : i], i + 1
        i += 1

    sys.exit(f"FAIL: unbalanced braces after `{selector}` in {CSS.name}")


def js_number(source: str, pattern: str, what: str, where: str = "controls.js") -> float:
    """One number out of a prototype script, by regex, or a recorded failure.

    `where` names the file in the failure, because two files are read now and
    "the knob geometry has moved" is a confusing thing to be told about the
    step pad's velocity law.
    """
    match = re.search(pattern, source)

    if match is None:
        MISSING.append(f"{what}: could not be read out of {where} — the design source "
                       f"there has moved, and this constant is policed by nothing else")
        return float("nan")

    return float(match.group(1))


def alphas(block: str, prop: str) -> list[float]:
    """Every `rgba(r,g,b,A)` alpha in one declaration, in source order.

    The pad's recessed look is four of these across two themes, and they are
    the only numbers that distinguish the light ground from the dark one.
    """
    match = re.search(r"(?<![\w-])" + re.escape(prop) + r"\s*:\s*([^;}]+)", block)

    if match is None:
        return []

    return [float(a) for a in re.findall(r"rgba\s*\([^)]*?,\s*([\d.]+)\s*\)", match.group(1))]


def percents(block: str, prop: str) -> list[float]:
    """Every `N%` in one declaration, in source order.

    The lit pad is written almost entirely in percentages — the ellipse's two
    radii and its origin, both `color-mix` weights, the gradient's outer stop
    and the glow's accent scale — and not one of them is a px length, so
    `px_list` reads the whole rule as empty.
    """
    match = re.search(r"(?<![\w-])" + re.escape(prop) + r"\s*:\s*([^;}]+)", block)

    if match is None:
        return []

    return [float(v) for v in re.findall(r"(-?[\d.]+)%", match.group(1))]


def indexed(values: list[float], index: int, what: str, scale: float = 1.0) -> float:
    """One entry of a list, or a recorded failure rather than an IndexError.

    px_one's guarantee, for the two readers above: the script promises to exit
    naming every constant that diverged, and a traceback out of a CMake custom
    command is not that.
    """
    if index >= len(values):
        MISSING.append(f"{what}: forrobox.css no longer declares it where this script reads it")
        return float("nan")

    return values[index] * scale


def px_one(block: str, prop: str, index: int, what: str) -> float:
    """One px length, or a recorded failure rather than an IndexError.

    The table below used to index `px_list(...)[0]` eagerly, so deleting or
    renaming any of the ~25 declarations it reads crashed the verifier with a
    traceback inside a CMake custom command — much harder to read than the
    clean "exit 1 naming every constant that diverged" this script promises.
    """
    values = px_list(block, prop)

    if index >= len(values):
        MISSING.append(f"{what}: `{prop}` is no longer declared in forrobox.css where this "
                       f"script reads it")
        return float("nan")

    return values[index]


def px_list(block: str, prop: str) -> list[float]:
    """The px lengths of one property, e.g. `margin: 8px 0 9px` -> [8, 0, 9]."""
    match = re.search(r"(?<![\w-])" + re.escape(prop) + r"\s*:\s*([^;}]+)", block)
    if match is None:
        return []

    out = []
    for token in match.group(1).split():
        number = re.fullmatch(r"(-?[\d.]+)(px)?", token.strip())
        if number:
            out.append(float(number.group(1)))
    return out


def namespace_block(header: str, name: str) -> str:
    """The body of `namespace <name> { ... }`, brace-matched, or "" if absent."""
    match = re.search(r"\bnamespace\s+" + re.escape(name) + r"\s*\{", header)

    if match is None:
        return ""

    open_brace = match.end() - 1
    depth, i = 0, open_brace

    while i < len(header):
        if header[i] == "{":
            depth += 1
        elif header[i] == "}":
            depth -= 1
            if depth == 0:
                return header[open_brace + 1 : i]
        i += 1

    return ""


def cpp_constant(header: str, name: str) -> float | None:
    """The value of one constexpr int/float called <name>.

    Accepts both spellings the project uses: `static constexpr` for a class
    member (ChassisLayout) and `inline constexpr` for a namespace-scope
    constant (the knob:: and pad:: geometry).

    A `ns::name` is looked up inside that namespace's block only. This is not
    decoration: every geometry header is concatenated into one string here, and
    `pad::kHeight` (26) and `ChassisLayout::kHeight` (780) are both spelled
    kHeight. Searching the whole corpus found the chassis and reported the pad
    as 780 px tall — a cross-check reading the wrong constant entirely, which is
    worse than no cross-check because it is green on the wrong thing. A
    duplicate UNQUALIFIED name is now a hard failure for the same reason.
    """
    scope, _, bare = name.rpartition("::")
    haystack = namespace_block(header, scope) if scope else header

    if scope and not haystack:
        return None

    pattern = (r"(?:static|inline)\s+constexpr\s+(?:int|float)\s+"
               + re.escape(bare) + r"\s*=\s*([^;]+);")

    matches = re.findall(pattern, haystack)

    if not matches:
        return None

    if len(matches) > 1:
        MISSING.append(f"{name}: declared {len(matches)} times in the geometry headers, so this "
                       f"script cannot tell which one it is checking — qualify it with its "
                       f"namespace")
        return float("nan")

    expression = matches[0].strip()

    # The text-row heights are declared as sums (`9 + 8 + 2`) so the padding and
    # border are visible at the definition. Evaluate only digits and + signs.
    # A leading minus is allowed: kSweepStartDeg is -135.0f, and rejecting it
    # reported the constant as "not found" rather than comparing it.
    if re.fullmatch(r"-?[\d\s+.f]+", expression):
        return float(eval(expression.replace("f", "")))  # noqa: S307 — digits only

    return None


def type_row(header: str, style: str) -> tuple[float, float] | None:
    """One textStyles row's (size px, letter-spacing em), by its Style:: name."""
    match = re.search(
        r'\{\s*"[^"]*"\s*,\s*Style::' + re.escape(style)
        + r"\s*,\s*([\d.]+)f\s*,\s*Face::\w+\s*,\s*(-?[\d.]+)f",
        header,
    )

    return (float(match.group(1)), float(match.group(2))) if match else None


def em_one(block: str, what: str) -> float:
    """One `letter-spacing: <n>em`, or 0 when the rule declares none.

    Zero for an ABSENT declaration, because `.ms-btn`, `.strip-idx` and
    `.ghost-row .gl b` genuinely declare no tracking and their C++ rows say 0 —
    so absence is the value, not a missing one.

    But a declaration in another unit is a recorded FAILURE rather than a
    second zero: `letter-spacing: 0.5px` or `normal` would otherwise keep a
    `0.00f` row green while the real tracking is not zero, which is the exact
    inverse of what this check was added for.
    """
    declared = re.search(r"(?<![\w-])letter-spacing\s*:\s*([^;}]+)", block)

    if declared is None:
        return 0.0

    value = declared.group(1).strip()
    match = re.fullmatch(r"(-?[\d.]+)em", value)

    if match is None:
        MISSING.append(f"{what}: letter-spacing is declared as `{value}`, which this script only "
                       f"understands in em — the C++ row would be compared against a silent 0")
        return float("nan")

    return float(match.group(1))


def main() -> int:
    css = CSS.read_text(encoding="utf-8")
    controls = CONTROLS_JS.read_text(encoding="utf-8")
    app = APP_JS.read_text(encoding="utf-8")
    header = ""
    for path in GEOMETRY_HEADERS:
        if not path.exists():
            sys.exit(f"FAIL: {path.name} is missing, so its constants cannot be cross-checked")
        header += path.read_text(encoding="utf-8")

    strip = css_rule(css, ".strip")
    accent_bar = css_rule(css, ".accent-bar")
    sample_slot = css_rule(css, ".sample-slot")
    hitviz = css_rule(css, ".hitviz")
    strip_div = css_rule(css, ".strip-div")
    knob_grid = css_rule(css, ".knob-grid")
    pattern_row = css_rule(css, ".pattern-row")
    ms_row = css_rule(css, ".ms-row")
    ghost_row = css_rule(css, ".ghost-row")
    ghost_label = css_rule(css, ".ghost-row .gl")
    fader = css_rule(css, ".fb-fader")
    fader_track = css_rule(css, ".fb-fader-track")
    subdots = css_rule(css, ".subdots")
    subdot = css_rule(css, ".subdot")
    knob = css_rule(css, ".fb-knob")
    knob_track = css_rule(css, ".fb-knob-track")

    # The pad's second block — css:616 — is the one carrying the gradients and
    # shadows. The first, css:464, carries only the height.
    pads = css_rule(css, ".pads")
    pad_box = css_rules(css, ".pad")
    pad_light = css_rules(css, '[data-theme="light"] .pad')
    pad_on = css_rules(css, ".pad.on")
    pad_ghost = css_rule(css, ".pad.ghost::after")
    pad_active = css_rule(css, ".pad:active")
    pat_screen = css_rule(css, ".pat-screen")

    # An empty block when the second declaration is gone: every reader below
    # then records a clean MISSING rather than raising an IndexError inside a
    # CMake custom command, which is px_one's standing rule.
    pad_recessed = pad_box[1] if len(pad_box) > 1 else ""
    pad_recessed_light = pad_light[1] if len(pad_light) > 1 else ""
    pad_backlit = pad_on[1] if len(pad_on) > 1 else ""

    # `radial-gradient(120% 100% at 50% 22%, color-mix(... 100%, white 22%), var(--c) 70%)`
    # — seven percentages, and every one of them is a constant in StepPad.h.
    lit_pcts = percents(pad_backlit, "background")
    # `inset 0 1px 0 color-mix(... 100%, white 35%), 0 0 9px color-mix(... * 45%, transparent)`
    lit_shadow_pcts = percents(pad_backlit, "box-shadow")

    fader_fill = css_rule(css, ".fb-fader-fill")
    fader_thumb = css_rule(css, ".fb-fader-thumb")
    thumb_shadow = px_list(fader_thumb, "box-shadow")     # 0 1px 3px
    knob_val = css_rule(css, ".fb-knob-val")

    strip_padding = px_list(strip, "padding")          # 12px 11px 10px
    bar_margin = px_list(accent_bar, "margin")         # 8px 0 9px
    div_margin = px_list(strip_div, "margin")          # 11px 0
    grid_gap = px_list(knob_grid, "gap")               # 9px 6px
    fader_padding = px_list(fader, "padding")          # 8px 0

    # (C++ constant, expected value, what the stylesheet says)
    expectations: list[tuple[str, float, str]] = [
        ("kStripPadTop",             strip_padding[0], ".strip padding, 1st"),
        ("kStripPadSide",            strip_padding[1], ".strip padding, 2nd"),
        ("kStripPadBottom",          strip_padding[2], ".strip padding, 3rd"),

        ("kAccentBarHeight",         px_one(accent_bar, "height", 0, "accent_bar"), ".accent-bar height"),
        ("kAccentBarMarginTop",      bar_margin[0], ".accent-bar margin, 1st"),
        ("kAccentBarMarginBottom",   bar_margin[2], ".accent-bar margin, 3rd"),

        ("kSampleSlotMarginTop",     px_one(sample_slot, "margin-top", 0, "sample_slot"), ".sample-slot margin-top"),
        ("kSampleSlotGap",           px_one(sample_slot, "gap", 0, "sample_slot"), ".sample-slot gap"),

        ("kHitVisualiserHeight",     px_one(hitviz, "height", 0, "hitviz"), ".hitviz height"),
        ("kHitVisualiserMarginTop",  px_one(hitviz, "margin-top", 0, "hitviz"), ".hitviz margin-top"),

        ("kStripDividerHeight",      px_one(strip_div, "height", 0, "strip_div"), ".strip-div height"),
        ("kStripDividerMargin",      div_margin[0], ".strip-div margin, 1st"),

        ("kKnobGridRowGap",          grid_gap[0], ".knob-grid gap, row"),
        ("kKnobGridColGap",          grid_gap[1], ".knob-grid gap, column"),

        ("kPatternRowMarginTop",     px_one(pattern_row, "margin-top", 0, "pattern_row"), ".pattern-row margin-top"),
        ("kPatternRowGap",           px_one(pattern_row, "gap", 0, "pattern_row"), ".pattern-row gap"),

        ("kMuteSoloMarginTop",       px_one(ms_row, "margin-top", 0, "ms_row"), ".ms-row margin-top"),
        ("kMuteSoloGap",             px_one(ms_row, "gap", 0, "ms_row"), ".ms-row gap"),

        ("kGhostRowMarginTop",       px_one(ghost_row, "margin-top", 0, "ghost_row"), ".ghost-row margin-top"),
        ("kGhostLabelGap",           px_one(ghost_label, "margin-bottom", 0, "ghost_label"), ".ghost-row .gl margin-bottom"),

        ("kSubDotsMarginTop",        px_one(subdots, "margin-top", 0, "subdots"), ".subdots margin-top"),
        ("kSubDotGap",               px_one(subdots, "gap", 0, "subdots"), ".subdots gap"),
        ("kSubDotSize",              px_one(subdot, "width", 0, "subdot"), ".subdot width"),
        ("kSubDotsLabelInset",       px_one(css_rule(css, ".subdots-label"), "margin-left", 0, ".subdots-label"),
                                     ".subdots-label margin-left"),

        ("kLabelGap",                px_one(knob, "gap", 0, "knob"), ".fb-knob gap"),

        # ── the knob's own geometry, declared in Knob.h by Task 2 ───────────
        ("kArcStroke",               px_one(knob_track, "stroke-width", 0, "knob_track"), ".fb-knob-track stroke-width"),
        ("kHubStroke",               px_one(css_rule(css, ".fb-knob-hub"), "stroke-width", 0, ".fb-knob-hub"),
                                     ".fb-knob-hub stroke-width"),
        ("kIndicatorStroke",         px_one(css_rule(css, ".fb-knob-line"), "stroke-width", 0, ".fb-knob-line"),
                                     ".fb-knob-line stroke-width"),

        # ── the knob's SVG geometry, from controls.js ───────────────────────
        #
        # PLANNING.md:349-357 calls these the knob's identity. They are the
        # numbers three knob sizes and three later plans all inherit, and the
        # only ones a wrong value ships at 28, 32 AND 54 px simultaneously.
        ("kArcRadius",               js_number(controls, r"_arcPath\s*\(\s*this\.A0\s*,\s*this\.A1\s*,\s*([\d.]+)\s*\)",
                                               "kArcRadius"),
                                     "controls.js _arcPath(A0, A1, r)"),
        ("kHubRadius",               js_number(controls, r'hub\.setAttribute\s*\(\s*"r"\s*,\s*"([\d.]+)"\s*\)',
                                               "kHubRadius"),
                                     'controls.js hub r="30"'),
        ("kIndicatorTipY",           js_number(controls, r'line\.setAttribute\s*\(\s*"y2"\s*,\s*"([\d.]+)"\s*\)',
                                               "kIndicatorTipY"),
                                     'controls.js line y2="16"'),
        ("kSweepStartDeg",           js_number(controls, r"this\.A0\s*=\s*(-?[\d.]+)", "kSweepStartDeg"),
                                     "controls.js A0"),
        # ── the button family, from forrobox.css ───────────────────────────
        ("kBasePadX",                px_one(css_rule(css, ".btn"), "padding", 1, ".btn"),
                                     ".btn padding, horizontal"),
        ("kBasePadY",                px_one(css_rule(css, ".btn"), "padding", 0, ".btn"),
                                     ".btn padding, vertical"),
        ("kMuteSoloPadY",            px_one(css_rule(css, ".ms-btn"), "padding", 0, ".ms-btn"),
                                     ".ms-btn padding, vertical"),
        ("kMuteSoloGapPx",           px_one(css_rule(css, ".ms-row"), "gap", 0, ".ms-row"),
                                     ".ms-row gap"),
        ("kArrowWidth",              px_one(css_rule(css, ".arrow-btn"), "width", 0, ".arrow-btn"),
                                     ".arrow-btn width"),
        ("kArrowHeight",             px_one(css_rule(css, ".arrow-btn"), "height", 0, ".arrow-btn"),
                                     ".arrow-btn height"),

        # ── the step pad, from forrobox.css and app.js ─────────────────────
        ("pad::kHeight",                  px_one(pad_box[0], "height", 0, "pad"), ".pad height"),
        ("pad::kGap",                     px_one(pads, "gap", 0, "pads"), ".pads gap"),

        # The recessed ground: two rows, not one row at two alphas. Reading all
        # four proves the light theme has its OWN gradient rather than the dark
        # one dimmed — the confusion that shipped 04-01's header ten times too
        # bright.
        ("pad::kOffTopWhite",             indexed(alphas(pad_recessed, "background"), 0, "kOffTopWhite"),
                                     ".pad background gradient, dark top"),
        ("pad::kOffBottomBlack",          indexed(alphas(pad_recessed, "background"), 1, "kOffBottomBlack"),
                                     ".pad background gradient, dark bottom"),
        ("pad::kOffTopBlackLight",        indexed(alphas(pad_recessed_light, "background"), 0, "kOffTopBlackLight"),
                                     "[light] .pad background gradient, top"),
        ("pad::kOffBottomBlackLight",     indexed(alphas(pad_recessed_light, "background"), 1, "kOffBottomBlackLight"),
                                     "[light] .pad background gradient, bottom"),

        ("pad::kInsetTopAlphaDark",       indexed(alphas(pad_recessed, "box-shadow"), 0, "kInsetTopAlphaDark"),
                                     ".pad box-shadow, inset top edge"),
        ("pad::kInsetRingAlpha",          indexed(alphas(pad_recessed, "box-shadow"), 1, "kInsetRingAlpha"),
                                     ".pad box-shadow, inset ring"),
        ("pad::kInsetTopAlphaLight",      indexed(alphas(pad_recessed_light, "box-shadow"), 0, "kInsetTopAlphaLight"),
                                     "[light] .pad box-shadow, inset top edge"),

        # The ellipse. rx and ry are what make it an ellipse at all, and the
        # origin is what AC-2 measures at 22% of height — all four policed
        # here, because the C++ derives the measurement from the same numbers.
        ("pad::kLitRadiusX",              indexed(lit_pcts, 0, "kLitRadiusX", 0.01), ".pad.on radial-gradient rx"),
        ("pad::kLitRadiusY",              indexed(lit_pcts, 1, "kLitRadiusY", 0.01), ".pad.on radial-gradient ry"),
        ("pad::kLitOriginX",              indexed(lit_pcts, 2, "kLitOriginX", 0.01), ".pad.on radial-gradient origin x"),
        ("pad::kLitOriginY",              indexed(lit_pcts, 3, "kLitOriginY", 0.01), ".pad.on radial-gradient origin y"),
        ("pad::kLitBasePct",              indexed(lit_pcts, 4, "kLitBasePct"), ".pad.on color-mix base"),
        ("pad::kLitCentreWhitePct",       indexed(lit_pcts, 5, "kLitCentreWhitePct"), ".pad.on color-mix white"),
        ("pad::kLitOuterStop",            indexed(lit_pcts, 6, "kLitOuterStop", 0.01), ".pad.on gradient outer stop"),

        ("pad::kLitSheenWhitePct",        indexed(lit_shadow_pcts, 1, "kLitSheenWhitePct"),
                                     ".pad.on sheen color-mix white"),
        ("pad::kLitGlowOpacity",          indexed(lit_shadow_pcts, 2, "kLitGlowOpacity", 0.01),
                                     ".pad.on glow accent-i scale"),
        ("pad::kLitGlowRadius",           indexed(px_list(pad_backlit, "box-shadow"), 5, "kLitGlowRadius"),
                                     ".pad.on box-shadow glow blur"),

        ("pad::kGhostDotSize",            px_one(pad_ghost, "width", 0, "pad_ghost"), ".pad.ghost::after width"),
        ("pad::kGhostDotOpacity",         px_one(pad_ghost, "opacity", 0, "pad_ghost"), ".pad.ghost::after opacity"),

        ("pad::kPressScale",              js_number(pad_active, r"scale\s*\(\s*([\d.]+)\s*\)",
                                               "kPressScale", "forrobox.css .pad:active"),
                                     ".pad:active transform scale"),

        ("pad::kVelocityOpacityFloor",    js_number(app, r"const b = ([\d.]+) \+ \(v / 127\)",
                                               "kVelocityOpacityFloor", "app.js"),
                                     "app.js applyPadVisual velocity floor"),
        ("pad::kVelocityOpacityRange",    js_number(app, r"\(v / 127\) \* ([\d.]+)",
                                               "kVelocityOpacityRange", "app.js"),
                                     "app.js applyPadVisual velocity range"),
        ("pad::kGhostVelocityMax",        js_number(app, r"if \(v <= ([\d.]+)\) pad\.classList\.add\(\"ghost\"\)",
                                               "kGhostVelocityMax", "app.js"),
                                     "app.js ghost threshold"),

        # ── the fader, from forrobox.css ───────────────────────────────────
        ("fader::kPadY",             fader_padding[0], ".fb-fader padding, vertical"),
        ("fader::kTrackHeight",      px_one(fader_track, "height", 0, "fader_track"),
                                     ".fb-fader-track height"),
        ("fader::kThumbSize",        px_one(fader_thumb, "width", 0, "fader_thumb"),
                                     ".fb-fader-thumb width"),
        ("fader::kThumbShadowY",     indexed(thumb_shadow, 1, "fader::kThumbShadowY"),
                                     ".fb-fader-thumb box-shadow y offset"),
        ("fader::kThumbShadowRadius", indexed(thumb_shadow, 2, "fader::kThumbShadowRadius"),
                                     ".fb-fader-thumb box-shadow blur"),
        ("fader::kThumbShadowAlpha", indexed(alphas(fader_thumb, "box-shadow"), 0,
                                             "fader::kThumbShadowAlpha"),
                                     ".fb-fader-thumb box-shadow alpha"),

        # `saturate(calc(<floor> + var(--accent-i) * <range>))`. Two elements
        # declare this law with the SAME numbers — css:378 for the fader and
        # css:613 for the knob — and each is read from its own rule, so the day
        # one of them moves the other is not quietly dragged along with it.
        ("fader::kSaturationFloor",  js_number(fader_fill, r"saturate\(calc\(([\d.]+)",
                                               "fader::kSaturationFloor", "forrobox.css .fb-fader-fill"),
                                     ".fb-fader-fill saturate floor"),
        ("fader::kSaturationRange",  js_number(fader_fill, r"accent-i\)\s*\*\s*([\d.]+)\)",
                                               "fader::kSaturationRange", "forrobox.css .fb-fader-fill"),
                                     ".fb-fader-fill saturate range"),
        ("knob::kSaturationFloor",   js_number(knob_val, r"saturate\(calc\(([\d.]+)",
                                               "knob::kSaturationFloor", "forrobox.css .fb-knob-val"),
                                     ".fb-knob-val saturate floor"),
        ("knob::kSaturationRange",   js_number(knob_val, r"accent-i\)\s*\*\s*([\d.]+)\)",
                                               "knob::kSaturationRange", "forrobox.css .fb-knob-val"),
                                     ".fb-knob-val saturate range"),

        # ── the strip's remaining boxes ────────────────────────────────────
        ("kLoadPadX",                px_one(css_rule(css, ".load-btn"), "padding", 1, ".load-btn"),
                                     ".load-btn padding, horizontal"),
        ("kLoadPadY",                px_one(css_rule(css, ".load-btn"), "padding", 0, ".load-btn"),
                                     ".load-btn padding, vertical"),
        ("kSubDotOpacity",           px_one(subdot, "opacity", 0, "subdot"), ".subdot opacity"),
        ("kPatternScreenPadY",       px_one(pat_screen, "padding", 0, ".pat-screen"),
                                     ".pat-screen padding, vertical"),
        ("kPatternScreenPadX",       px_one(pat_screen, "padding", 1, ".pat-screen"),
                                     ".pat-screen padding, horizontal"),

        ("kSweepEndDeg",             js_number(controls, r"this\.A1\s*=\s*(-?[\d.]+)", "kSweepEndDeg"),
                                     "controls.js A1"),
        ("kViewBox",                 js_number(controls, r'viewBox"\s*,\s*"0 0 ([\d.]+) [\d.]+"', "kViewBox"),
                                     "controls.js svg viewBox"),
    ]

    failures: list[str] = []

    for name, expected, source in expectations:
        actual = cpp_constant(header, name)

        if actual is None:
            failures.append(f"{name}: not found as a numeric constexpr in any geometry header")
        elif expected != expected:   # NaN: already recorded by px_one
            pass
        elif abs(actual - expected) > 1e-6:
            failures.append(f"{name}: C++ {actual:g} != spec {expected:g}  [{source}]")

    # ── the type scale, against the rules that declare it ──────────────────
    #
    # Only the rows forrobox.css carries. The other seventeen come from
    # PLANNING.md's table, which this script does not read — naming them here
    # would be a check with no source.
    typography = TYPOGRAPHY_HEADER.read_text(encoding="utf-8")

    type_rules: list[tuple[str, str, str]] = [
        ("buttonLabel",     ".btn",                 "css:132"),
        ("stripIndex",      ".strip-idx",           "css:276"),
        ("sampleName",      ".sample-name",         "css:291"),
        ("patternScreen",   ".pat-screen",          "css:330"),
        ("muteSoloLabel",   ".ms-btn",              "css:337"),
        ("loadLabel",       ".load-btn",            "css:296"),
        ("ghostValue",      ".ghost-row .gl b",     "css:349"),
        # ONE row, TWO rules. Both are read, so the day either moves away from
        # the other this fails rather than silently following whichever was
        # listed first.
        ("stripMicroLabel", ".ghost-row .gl span",  "css:348"),
        ("stripMicroLabel", ".subdots-label",       "css:354"),
    ]

    for style, selector, source in type_rules:
        row = type_row(typography, style)

        if row is None:
            MISSING.append(f"type::Style::{style}: no row found in {TYPOGRAPHY_HEADER.name}")
            continue

        block = css_rule(css, selector)
        expected_px = px_one(block, "font-size", 0, f"{selector} font-size")
        expected_em = em_one(block, selector)

        if expected_px == expected_px and abs(row[0] - expected_px) > 1e-6:
            failures.append(f"type::Style::{style}: size {row[0]:g} != spec {expected_px:g}"
                            f"  [{selector}, {source}]")

        if expected_em == expected_em and abs(row[1] - expected_em) > 1e-6:
            failures.append(f"type::Style::{style}: tracking {row[1]:g}em != spec {expected_em:g}em"
                            f"  [{selector}, {source}]")

    # The fader's box is padding + track, and BOTH halves must be right — a
    # 20 px total made of 6+8 would pass a total-only check.
    fader_total = 2 * fader_padding[0] + px_one(fader_track, "height", 0, "fader_track")
    cpp_fader = cpp_constant(header, "kFaderHeight")

    if cpp_fader is None:
        failures.append("kFaderHeight: not found in any geometry header")
    elif abs(cpp_fader - fader_total) > 1e-6:
        failures.append(
            f"kFaderHeight: C++ {cpp_fader:g} != spec {fader_total:g}"
            f"  [.fb-fader padding {fader_padding[0]:g} x2 + .fb-fader-track height]"
        )

    # MISSING is copied LAST, so a px_one/js_number failure recorded anywhere
    # above still reaches the report. It used to be copied before the fader
    # check ran, so a missing .fb-fader-track height produced a NaN that made
    # `abs(cpp - nan) > 1e-6` false — the comparison silently passed and the
    # recorded failure was already out of scope. A check that could not fail.
    failures = MISSING + failures

    if failures:
        print("Strip geometry cross-check FAILED", file=sys.stderr)
        for line in failures:
            print(f"  {line}", file=sys.stderr)
        return 1

    print(f"Strip geometry cross-check OK — {len(expectations) + 1} lengths and "
          f"{len(type_rules) * 2} type-scale values "
          f"against forrobox.css, controls.js and app.js")
    return 0


if __name__ == "__main__":
    sys.exit(main())
