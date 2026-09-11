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
# Read every header that declares design geometry. Knob.h arrives with 04-02's
# Task 2; a missing file is a hard failure rather than a silent skip, because a
# skip would make every knob expectation below a check that cannot fail.
GEOMETRY_HEADERS = [ROOT / "src" / "Chassis.h", ROOT / "src" / "Knob.h"]


def css_rule(css: str, selector: str) -> str:
    """The declaration block of one selector, brace-matched.

    Brace-matched rather than a lazy `\\{(.*?)\\}`: verify-profiles.py records
    that a lazy match silently captured the wrong block once already, and these
    blocks contain `calc(...)` and nested functions.
    """
    start = css.find(selector)
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
        sys.exit(f"FAIL: could not find a rule beginning with `{selector}` in {CSS.name}")

    open_brace = css.index("{", start)
    depth, i = 0, open_brace
    while i < len(css):
        if css[i] == "{":
            depth += 1
        elif css[i] == "}":
            depth -= 1
            if depth == 0:
                return css[open_brace + 1 : i]
        i += 1

    sys.exit(f"FAIL: unbalanced braces after `{selector}` in {CSS.name}")


def js_number(source: str, pattern: str, what: str) -> float:
    """One number out of controls.js, by regex, or a recorded failure."""
    match = re.search(pattern, source)

    if match is None:
        MISSING.append(f"{what}: could not be read out of {CONTROLS_JS.name} — the knob geometry "
                       f"there has moved, and these constants are policed by nothing else")
        return float("nan")

    return float(match.group(1))


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


def cpp_constant(header: str, name: str) -> float | None:
    """The value of one constexpr int/float called <name>.

    Accepts both spellings the project uses: `static constexpr` for a class
    member (ChassisLayout) and `inline constexpr` for a namespace-scope
    constant (the knob:: geometry).
    """
    match = re.search(
        r"(?:static|inline)\s+constexpr\s+(?:int|float)\s+"
        + re.escape(name)
        + r"\s*=\s*([^;]+);",
        header,
    )
    if match is None:
        return None

    expression = match.group(1).strip()

    # The text-row heights are declared as sums (`9 + 8 + 2`) so the padding and
    # border are visible at the definition. Evaluate only digits and + signs.
    # A leading minus is allowed: kSweepStartDeg is -135.0f, and rejecting it
    # reported the constant as "not found" rather than comparing it.
    if re.fullmatch(r"-?[\d\s+.f]+", expression):
        return float(eval(expression.replace("f", "")))  # noqa: S307 — digits only

    return None


def main() -> int:
    css = CSS.read_text(encoding="utf-8")
    controls = CONTROLS_JS.read_text(encoding="utf-8")
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

    print(f"Strip geometry cross-check OK — {len(expectations) + 1} lengths "
          f"against forrobox.css and controls.js")
    return 0


if __name__ == "__main__":
    sys.exit(main())
