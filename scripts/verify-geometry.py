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

import math
import pathlib
import collections
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
                    ROOT / "src" / "Fader.h", ROOT / "src" / "Segmented.h",
                    ROOT / "src" / "LogoMark.h", ROOT / "src" / "BpmField.h",
                    ROOT / "src" / "FooterBar.h", ROOT / "src" / "GainReductionMeter.h",
                    ROOT / "src" / "DragMidiButton.h", ROOT / "src" / "SequencerGrid.h",
                    ROOT / "src" / "HitVisualiser.h", ROOT / "src" / "Playhead.h",
                    ROOT / "src" / "KitOverlay.h", ROOT / "src" / "SidePanel.h",
                    # 06-05 hoisted the three 30 Hz poll rates out of HeaderBar.h,
                    # FooterBar.h and SidePanel.h into ONE kUiPollHz here. Two of
                    # those three headers are enrolled, so without this line the
                    # hoist would have moved a constant OUT of the gate's reach
                    # and the gate would have kept passing — the exact silent
                    # coverage loss `check_enrolment_coverage` exists to stop.
                    # /simplify.
                    ROOT / "src" / "Surface.h"]

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


def keyframe(css: str, name: str, stop: str) -> str:
    """One stop out of an `@keyframes` block — e.g. the `50%` of `midipulse`.

    FOUR of the pulse constants live ONLY in keyframes — the ring and glow
    weights, the glow blur and the arrow's bob. (`kPulseSeconds` does not: it is
    read from `.drag-midi`'s own `animation` declaration.) Without this they
    would have to be excused as having no design source, which would be false.

    Returns "" on a miss and leaves the REPORTING to `indexed`, the way
    `function_args`, `alphas`, `percents` and `px_list` do. The first version of
    this reader called `fail()` — which is defined in verify-profiles.py and NOT
    in this file. That is the SEVENTH instance of the same NameError;
    `function_args`' own docstring records /simplify removing six, and this one
    was dormant, waiting for the first day a keyframe stop got renamed.
    """
    block = css_rule(css, "@keyframes " + name)

    # The stops are `0%,100% { ... }` and `50% { ... }`; match the stop as a
    # whole token so `0%` cannot match inside `100%`.
    m = re.search(r"(?:^|[\s,])" + re.escape(stop) + r"\s*(?:,[^{]*)?\{([^}]*)\}", block)

    return m.group(1) if m else ""


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


def declaration(block: str, prop: str) -> str | None:
    """The value text of one declaration, or None.

    The negative lookbehind is load-bearing: without it `padding` matches inside
    `padding-top`. It used to be written out in four readers, three of which had
    no test that would notice if it were dropped — and a reader that finds
    nothing returns an empty list, which `indexed` turns into a recorded failure
    but `em_one` used to turn into a silent zero.
    """
    match = re.search(r"(?<![\w-])" + re.escape(prop) + r"\s*:\s*([^;}]+)", block)

    return match.group(1) if match is not None else None


def alphas(block: str, prop: str) -> list[float]:
    """Every `rgba(r,g,b,A)` alpha in one declaration, in source order.

    The pad's recessed look is four of these across two themes, and they are
    the only numbers that distinguish the light ground from the dark one.
    """
    value = declaration(block, prop)

    if value is None:
        return []

    return [float(a) for a in re.findall(r"rgba\s*\([^)]*?,\s*([\d.]+)\s*\)", value)]


def percents(block: str, prop: str) -> list[float]:
    """Every `N%` in one declaration, in source order.

    The lit pad is written almost entirely in percentages — the ellipse's two
    radii and its origin, both `color-mix` weights, the gradient's outer stop
    and the glow's accent scale — and not one of them is a px length, so
    `px_list` reads the whole rule as empty.
    """
    value = declaration(block, prop)

    if value is None:
        return []

    return [float(v) for v in re.findall(r"(-?[\d.]+)%", value)]


def indexed(values: list[float], index: int, what: str, scale: float = 1.0,
            prop: str | None = None) -> float:
    """One entry of a list, or a recorded failure rather than an IndexError.

    px_one's guarantee, for the two readers above: the script promises to exit
    naming every constant that diverged, and a traceback out of a CMake custom
    command is not that.
    """
    if index >= len(values):
        where = f"`{prop}` is no longer declared" if prop else "it is no longer declared"
        MISSING.append(f"{what}: {where} in forrobox.css where this script reads it")
        return float("nan")

    return values[index] * scale


# ── constants in an enrolled header that NO expectation compares ────────────
#
# Enrolling a header only lets `cpp_constant` FIND a name. The comparison loop
# iterates the EXPECTATIONS, so a constant nobody listed is never read, and the
# script stays green while saying nothing about it. That happened three times:
# 05-02 enrolled Playhead.h with nine constants checked by nothing (kTrailGap
# shipped as 0 under a comment saying 3px); 05-04 enrolled KitOverlay.h and left
# six more; and the four easing control points were invisible because they were
# `double`.
#
# ONE list, mapping each excused name to its reason. There were briefly two —
# one for "predates the gate", one for "has no design source" — and the script
# subtracted them identically, so the split existed only in prose and a name
# added to the "wrong" one was accepted in silence. A taxonomy a check cannot
# enforce is the shape this gate was built to replace. /simplify.
#
# The 94 names carrying "predates the gate" are a BASELINE, not an audit: nobody
# has been through deciding which have a CSS source and which are genuinely
# derived. Shrinking that set is its own job. A name added with any other reason
# is a claim that the constant has no machine-readable source in forrobox.css,
# controls.js or app.js — if it has one, write the expectation instead.
PREDATES_GATE = "predates the enrolment gate; not audited"

NOT_COMPARED = {
    "kAccentGlowOpacity": PREDATES_GATE,
    "kAccentGlowRadius": PREDATES_GATE,
    "kAnchorAccentWeight": PREDATES_GATE,
    "kArrowPress": PREDATES_GATE,
    "kBasePress": PREDATES_GATE,
    "kBorderWidth": PREDATES_GATE,
    "kCentreDeg": PREDATES_GATE,
    "kDecayPerFrame": PREDATES_GATE,
    "kDividerWidth": PREDATES_GATE,
    "kFaderHeight": PREDATES_GATE,
    # The smallest sway rotation worth committing. NOT a design number: the CSS
    # gives an amplitude and a duration and says nothing about how finely the
    # browser steps between them. It is the angle at which the chassis's
    # furthest corner moves a quarter of a device pixel, chosen so that frames
    # which move nothing do not cost a full-chassis invalidation. 08-04.
    "kSwayCommitDegrees": "a rendering cadence, not a design value — the CSS "
                          "specifies the amplitude and the duration and nothing "
                          "about the step between frames",
    # `kPadY * 2 + kTrackHeight`, and BOTH terms are compared against
    # `.fb-fader` and `.fb-fader-track` below — but `cpp_constant` evaluates
    # digits and `+` only, so it cannot read a product of two identifiers. The
    # sum itself is compared as an expectation; this excuse is what stops the
    # unreadable DECLARATION from counting against coverage. Qualified, so the
    # four other kHeights still have to answer for themselves.
    "fader::kHeight": "kPadY * 2 + kTrackHeight, and BOTH terms are compared against "
                      ".fb-fader and .fb-fader-track below. css:376 gives the fader no "
                      "height of its own, so there is no third number to compare the sum "
                      "against — and cpp_constant evaluates digits and + only, so it "
                      "cannot read a product of two identifiers anyway. QUALIFIED, so the "
                      "four other kHeights still answer for themselves",
    "kFillBase": PREDATES_GATE,
    "kFillFarAlpha": PREDATES_GATE,
    "kFillSaturationBase": PREDATES_GATE,
    "kFillSaturationSpan": PREDATES_GATE,
    "kFillSpan": PREDATES_GATE,
    "kFooterHeight": PREDATES_GATE,
    "kGhostLabelHeight": PREDATES_GATE,
    "kGlobalKnobDividerHeight": PREDATES_GATE,
    "kGlobalKnobDividerWidth": PREDATES_GATE,
    "kGlobalKnobMetaGap": PREDATES_GATE,
    "kGlobalKnobReadMinWidth": PREDATES_GATE,
    "kGlobalKnobReadPadX": PREDATES_GATE,
    "kGlobalKnobReadPadY": PREDATES_GATE,
    "kGlobalKnobSize": PREDATES_GATE,
    "kGlobalKnobStackGap": PREDATES_GATE,
    "kGlobalKnobsBorderPct": PREDATES_GATE,
    "kGlobalKnobsGap": PREDATES_GATE,
    "kGlobalKnobsGlowRadius": PREDATES_GATE,
    "kGlobalKnobsInsetAlpha": PREDATES_GATE,
    "kGlobalKnobsInsetAlphaLight": PREDATES_GATE,
    "kGlobalKnobsOriginX": PREDATES_GATE,
    "kGlobalKnobsOriginY": PREDATES_GATE,
    "kGlobalKnobsPadBottom": PREDATES_GATE,
    "kGlobalKnobsPadTop": PREDATES_GATE,
    "kGlobalKnobsPadX": PREDATES_GATE,
    "kGlobalKnobsRadiusExtra": PREDATES_GATE,
    "kGlobalKnobsRadiusX": PREDATES_GATE,
    "kGlobalKnobsRadiusY": PREDATES_GATE,
    "kGlobalKnobsTintPct": PREDATES_GATE,
    "kGlowMargin": PREDATES_GATE,
    "kHeadRowHeight": PREDATES_GATE,
    "kHeaderGap": PREDATES_GATE,
    "kHeaderGradientWeight": PREDATES_GATE,
    "kHeaderHeight": PREDATES_GATE,
    "kHeaderPadX": PREDATES_GATE,
    "kKnobCellHeight": PREDATES_GATE,
    "kKnobGridCols": PREDATES_GATE,
    "kKnobGridHeight": PREDATES_GATE,
    "kLabelHeight": PREDATES_GATE,
    "kLedGlowBase": PREDATES_GATE,
    "kLedGlowSpan": PREDATES_GATE,
    "kLedLitBase": PREDATES_GATE,
    "kLedLitSpan": PREDATES_GATE,
    "kLedRestingAlpha": PREDATES_GATE,
    "kMainHeight": PREDATES_GATE,
    "kMiniPress": PREDATES_GATE,
    "kMuteSoloHeight": PREDATES_GATE,
    "kNoPress": PREDATES_GATE,
    "kNumAutoMargins": PREDATES_GATE,
    "kNumStrips": PREDATES_GATE,
    "kNumSubDots": PREDATES_GATE,
    "kOutRadiusExtra": PREDATES_GATE,
    "kPatternRowHeight": PREDATES_GATE,
    "kPatternScreenBorder": PREDATES_GATE,
    "kPatternScreenHeight": PREDATES_GATE,
    "kPollSeconds": PREDATES_GATE,
    "kPresetGap": PREDATES_GATE,
    "kPresetScreenMinWidth": PREDATES_GATE,
    "kPresetScreenPadX": PREDATES_GATE,
    "kPresetScreenPadY": PREDATES_GATE,
    "kRadiusExtra": PREDATES_GATE,
    "kRangeDb": PREDATES_GATE,
    "kSampleSlotHeight": PREDATES_GATE,
    "kSequencerHeight": PREDATES_GATE,
    "kSidePanelWidth": PREDATES_GATE,
    "kSilenceLevel": PREDATES_GATE,
    "kStripGap": PREDATES_GATE,
    "kStripKnobSize": PREDATES_GATE,
    "kStyleGap": PREDATES_GATE,
    "kSubDotsRowHeight": PREDATES_GATE,
    "kThumbOverhang": PREDATES_GATE,
    "kTickAlpha": PREDATES_GATE,
    "kTickDivisions": PREDATES_GATE,
    "kTickGroundMix": PREDATES_GATE,
    "kTopWhiteMix": PREDATES_GATE,
    "kTransportGap": PREDATES_GATE,
    "kTransportIconViewBox": PREDATES_GATE,
    "kTransportPress": PREDATES_GATE,
    "kTrianguloStroke": PREDATES_GATE,
    "kWellShadowDepth": PREDATES_GATE,

    # A UI refresh rate. forrobox.css has no equivalent — the prototype's
    # rendering cadence is the browser's, not a declared number.
    "kUiPollHz": "a UI poll rate, not a declared length",

    # An engineering threshold, not a design value: below it a pad composites
    # through a transparency layer. forrobox.css has no equivalent — the browser
    # decides when an element opacity needs its own layer.
    "kGroupOpacityThreshold": "a rasteriser threshold, not a declared opacity",

    # An INPUT threshold, not a length the design declares. The browser decides
    # when a mousedown becomes a `dragstart`; forrobox.css and app.js say
    # nothing about it, and `draggable="true"` (app.js:422) leaves it entirely
    # to the user agent. 8 px is JUCE's own default, the one
    # DragAndDropContainer::startDragging uses.
    "kDragThresholdPx": "a pointer-travel threshold, not a declared length",

}


def check_enrolment_coverage(header: str, expectations: list) -> list[str]:
    """Every constexpr in an enrolled header is compared, or excused by name.

    Coverage is matched on the BARE name, because most expectations are written
    unqualified — and fourteen names are declared in more than one namespace on
    purpose (`pad::kHeight` and `ChassisLayout::kHeight` are 26 and 780). So a
    bare match is not enough: the invariant is ONE EXPECTATION PER DECLARATION.

    08-04 declared `Chassis::kPulseSeconds` (1.6 s) while `dragmidi::kPulseSeconds`
    (2.6 s) already had an expectation, and a set-difference counted the new
    constant as compared — enrolled, unchecked, and green. Counting rather than
    set-differencing is what catches that, and it needs no renaming of the
    thirteen honest duplicates.

    A name in NOT_COMPARED excuses every declaration of it, as it always has.
    """
    declarations = collections.Counter(re.findall(
        r"(?:inline|static)\s+constexpr\s+(?:int|float|double)\s+(k\w+)\s*(?:=|{)", header))

    compared = collections.Counter(name.rpartition("::")[2] for name, _, _ in expectations)

    out: list[str] = []

    # An excuse written BARE covers every declaration of that name, as it always
    # has. One written QUALIFIED — `fader::kHeight` — covers exactly one, so a
    # derivation can be excused without excusing the 780 px chassis beside it.
    excused = collections.Counter()

    for key in NOT_COMPARED:
        scope, _, bare = key.rpartition("::")
        excused[bare] += declarations[bare] if not scope else 1

    # An excuse that matches NOTHING is a trap, not a nuisance: it sits there
    # until a constant takes that name in an enrolled header, and then silently
    # covers it — the same shape as the bare-name collision this counting rule
    # was written to close, one level up. Three were dead when this was added
    # (kPlayheadPollHz, kToggleOffVelocity, kToggleOnVelocity, all declared in
    # PatternPads.h, which is not enrolled). /simplify.
    for key in sorted(NOT_COMPARED):
        bare = key.rpartition("::")[2]

        if declarations[bare] == 0:
            out.append(f"{key}: excused in NOT_COMPARED but declared in no enrolled geometry "
                       f"header — delete the excuse, or enrol the header it belongs to")

    for name, count in sorted(declarations.items()):
        if excused[name] >= count:
            continue

        if compared[name] + excused[name] == 0:
            out.append(f"{name}: declared in an enrolled geometry header and compared against "
                       f"nothing — write an expectation for it, or add it to NOT_COMPARED with "
                       f"the reason it has no design source")
        elif compared[name] + excused[name] < count:
            out.append(f"{name}: declared {count} times across the enrolled geometry headers but "
                       f"compared or excused {compared[name] + excused[name]} time(s) — one of "
                       f"them is enrolled and checked by nothing. Qualify the expectations and "
                       f"write the missing one")

    return out


def unitless(block: str, prop: str) -> list[float]:
    """Every bare number in one declaration, e.g. `line-height: 1.4` -> [1.4].

    `px_list` matches only tokens carrying `px`, so a ratio is invisible to it —
    and a ratio is exactly what `line-height` is.
    """
    value = declaration(block, prop)

    if value is None:
        return []

    return [float(n) for n in re.findall(r"(?<![\w.])(\d+(?:\.\d+)?)(?![\w.%])", value)]


def function_args(block: str, prop: str, name: str) -> list[float]:
    """The numeric arguments of one CSS function inside one declaration.

    `translateX(24px)`, `cubic-bezier(.2,.7,.3,1)` and `scale(0.94)` are the
    same shape: `px_list` splits on whitespace and matches bare tokens, so a
    value wrapped in a function is invisible to it.

    Returns [] on any miss and leaves the REPORTING to `indexed`, the way
    `alphas`, `percents` and `px_list` do. The three readers this replaced each
    called a `fail()` that exists in verify-profiles.py and NOT in this file —
    six live NameError paths, each of which would have thrown a traceback out of
    a CMake custom command on the first day the stylesheet moved. That is
    precisely what `indexed`'s own docstring forbids. Found by /simplify.
    """
    value = declaration(block, prop)

    if value is None:
        return []

    match = re.search(re.escape(name) + r"\s*\(([^)]*)\)", value)

    if match is None:
        return []

    return [float(n) for n in re.findall(r"-?[\d.]+", match.group(1))]


def px_one(block: str, prop: str, index: int, what: str) -> float:
    """One px length, or a recorded failure rather than an IndexError.

    The table below used to index `px_list(...)[0]` eagerly, so deleting or
    renaming any of the ~25 declarations it reads crashed the verifier with a
    traceback inside a CMake custom command — much harder to read than the
    clean "exit 1 naming every constant that diverged" this script promises.
    """
    return indexed(px_list(block, prop), index, what, prop=prop)


def seconds_list(block: str, prop: str) -> list[float]:
    """Every duration of one property, in SECONDS. `0.06s` -> 0.06, `120ms` -> 0.12.

    The GR meter's decay is the only time in the design that reaches a C++
    constant, and px_list reads `0.06s` as nothing at all — its fullmatch
    demands a bare number or a px. Silently nothing, which is the shape this
    script exists to refuse.
    """
    value = declaration(block, prop)

    if value is None:
        return []

    out = []
    for number, unit in re.findall(r"(-?[\d.]+)(ms|s)\b", value):
        out.append(float(number) / (1000.0 if unit == "ms" else 1.0))
    return out


def scale_one(block: str, what: str) -> float:
    """The factor of a `transform: scale(N)`, or a recorded failure."""
    value = declaration(block, "transform")
    match = re.search(r"scale\s*\(\s*([\d.]+)\s*\)", value) if value else None

    if match is None:
        MISSING.append(f"{what}: no `transform: scale(...)` in forrobox.css where this "
                       f"script reads it")
        return float("nan")

    return float(match.group(1))


def px_list(block: str, prop: str) -> list[float]:
    """The px lengths of one property, e.g. `margin: 8px 0 9px` -> [8, 0, 9]."""
    value = declaration(block, prop)

    if value is None:
        return []

    out = []
    for token in value.split():
        number = re.fullmatch(r"(-?[\d.]+)(px)?", token.strip())
        if number:
            out.append(float(number.group(1)))
    return out


def scope_block(header: str, name: str) -> str:
    """The body of `namespace|struct|class <name> { ... }`, brace-matched, or "".

    Namespaces only until 08-04, which is why `ChassisLayout::kWidth` and
    `::kHeight` — the 1200x780 every layout number in this project is expressed
    in — could not be written as expectations at all: the qualified lookup found
    no block, and the bare lookup found three `kWidth`s and refused to guess.
    They sat enrolled and compared by nothing, behind `logo::kWidth` and
    `grmeter::kWidth` satisfying the bare name in the coverage set.
    """
    match = re.search(r"\b(?:namespace|struct|class)\s+" + re.escape(name)
                      + r"\s*(?:final\s*)?(?::[^{]*)?\{", header)

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
    haystack = scope_block(header, scope) if scope else header

    if scope and not haystack:
        return None

    # `double` as well as int/float. The easing control points are doubles
    # because the curve is solved in double, and a reader that silently cannot
    # SEE a constant reports it as unenrolled rather than as unchecked — which
    # is a better failure than passing, but only because the enrolment was
    # attempted. /code-review.
    pattern = (r"(?:static|inline)\s+constexpr\s+(?:int|float|double)\s+"
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
    declared = declaration(block, "letter-spacing")

    if declared is None:
        return 0.0

    value = declared.strip()
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
    seq_row_dimmed = css_rule(css, ".seq-row.dimmed")
    pad_active = css_rule(css, ".pad:active")
    pat_screen = css_rule(css, ".pat-screen")
    mini_btn = css_rule(css, ".mini-btn")
    tp_btn = css_rule(css, ".tp-btn")
    # `.tp-btn.play.on` is declared TWICE — css:197 for its colours and css:637
    # for the glow — so the FIRST block carries no box-shadow at all. Fourth
    # element in this stylesheet with a second block in the "dimensional" section
    # at css:615; `.pad`, `.pad.on` and `[data-theme="light"] .pad` were the
    # others.
    tp_play_blocks = css_rules(css, ".tp-btn.play.on")
    tp_play_on = tp_play_blocks[1] if len(tp_play_blocks) > 1 else ""
    qs_btn = css_rule(css, ".qs-btn")
    quick_switch = css_rule(css, ".quick-switch")
    logo_mark = css_rule(css, ".logo-mark")
    bpm_rule = css_rule(css, ".bpm")
    bpm_cluster = css_rule(css, ".bpm-cluster")
    mini_btns = css_rule(css, ".mini-btns")

    # ── the footer, 04-05 ──────────────────────────────────────────────────
    footer_rule = css_rule(css, ".footer")
    foot_group = css_rule(css, ".foot-group")
    master_fader = css_rule(css, ".master-fader")
    gr_meter = css_rule(css, ".gr-meter")
    gr_fill = css_rule(css, ".gr-fill")
    drag_midi = css_rule(css, ".drag-midi")
    # `.drag-midi:hover, .drag-midi.hot` — one block, two selectors, and
    # css_rule matches the one that BEGINS the rule.
    drag_hover = css_rule(css, ".drag-midi:hover")
    drag_active = css_rule(css, ".drag-midi:active")

    # The breath's numbers are in the keyframes, not on the element.
    midipulse_peak = keyframe(css, "midipulse", "50%")

    # ── the easter egg's sway, 08-04 ────────────────────────────────────────
    #
    # The duration is on `.fb-window.tipsy`'s own `animation` shorthand; the
    # amplitude lives only in the 25% keyframe, which is the shape
    # `keyframe`'s docstring above records for the drag-MIDI pulse.
    tipsy_rule = css_rule(css, ".fb-window.tipsy")
    sway_peak = keyframe(css, "sway", "25%")

    # The `♪ NO PONTO` label's breath — css:100-101. The duration is on
    # `.gk-name.drunk-on`'s own `animation` shorthand; the two opacities live
    # only in the keyframes, which is the shape the drag-MIDI pulse has.
    drunk_on = css_rule(css, ".gk-name.drunk-on")
    drunkpulse_low = keyframe(css, "drunkpulse", "0%")
    drunkpulse_high = keyframe(css, "drunkpulse", "50%")
    midiarrow_peak = keyframe(css, "midiarrow", "50%")
    out_toggle_btn = css_rule(css, ".out-toggle .ot")

    # ── the sequencer, 05-01 ───────────────────────────────────────────────
    seq_rule = css_rule(css, ".seq")
    seq_head = css_rule(css, ".seq-head")
    sh_left = css_rule(css, ".seq-head .sh-left")
    seq_wrap = css_rule(css, ".seq-grid-wrap")
    seq_row = css_rule(css, ".seq-row")
    seq_rowlabel = css_rule(css, ".seq-rowlabel")
    rl_chip = css_rule(css, ".seq-rowlabel .rl-chip")
    pads_rule = css_rule(css, ".pads")
    playhead_rule = css_rule(css, ".playhead")
    subview = css_rule(css, ".subview")
    side          = css_rule(css, ".side")
    side_sect     = css_rule(css, ".side-sect")
    profiles      = css_rule(css, ".profiles")
    profile       = css_rule(css, ".profile")
    fb_window     = css_rule(css, ".fb-window")
    profile_desc  = css_rule(css, ".profile .pf-desc")
    profile_dot   = css_rule(css, ".profile.active .pf-name::after")
    timbre_opts   = css_rule(css, ".timbre-opts")
    timbre        = css_rule(css, ".timbre")
    timbre_led    = css_rule(css, ".timbre .tb-led")
    timbre_lit    = css_rule(css, ".timbre.active .tb-led")
    timbre_active = css_rule(css, ".timbre.active")
    custom_tag    = css_rule(css, ".custom-tag")
    mix_row       = css_rule(css, ".mix-row")
    bundle        = css_rule(css, ".bundle")
    bundle_dot    = css_rule(css, ".bundle .bdot")

    subview_panel = css_rule(css, ".subview-panel")

    # Read ONCE, not once per control point: four calls parsed the same
    # declaration four times, and a reader bound at its use site is a reader
    # nobody notices is being called four times. /simplify.
    entrance_ease = function_args(subview_panel, "transition", "cubic-bezier")
    subview_head = css_rule(css, ".subview-head")
    subview_sub = css_rule(css, ".subview-sub")
    subclose = css_rule(css, ".subclose")
    sub_rows = css_rule(css, ".sub-rows")
    sub_row = css_rule(css, ".sub-row")
    sub_rowlabel = css_rule(css, ".sub-rowlabel")
    playhead_trail = css_rule(css, ".playhead::before")
    seq_len = css_rule(css, ".seq-len")

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

        ("knob::kLabelGap",          px_one(knob, "gap", 0, "knob"), ".fb-knob gap"),

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
        ("kArrowWidth",              px_one(css_rule(css, ".arrow-btn"), "width", 0, ".arrow-btn"),
                                     ".arrow-btn width"),
        ("kArrowHeight",             px_one(css_rule(css, ".arrow-btn"), "height", 0, ".arrow-btn"),
                                     ".arrow-btn height"),

        # ── the bateria kit overlay, from forrobox.css ─────────────────────
        #
        # The HEADER is enrolled in GEOMETRY_HEADERS above, not merely these
        # constants remembered — 05-02 shipped Playhead.h with nine constants
        # policed by nothing because only the constants were thought of.
        ("kit::kScrimOpacity",       indexed(percents(subview, "background"), 0,
                                             "kit::kScrimOpacity", 0.01),
                                     ".subview scrim colour-mix weight"),
        ("kit::kPanelWidth",         px_one(subview_panel, "width", 0, ".subview-panel"),
                                     ".subview-panel width"),
        ("kit::kPanelPadY",          px_one(subview_panel, "padding", 0, ".subview-panel"),
                                     ".subview-panel padding, vertical"),
        ("kit::kPanelPadX",          px_one(subview_panel, "padding", 1, ".subview-panel"),
                                     ".subview-panel padding, horizontal"),
        ("kit::kPanelBorder",        px_one(subview_panel, "border-left", 0, ".subview-panel"),
                                     ".subview-panel left border"),
        ("kit::kPanelShadowRadius",  px_one(subview_panel, "box-shadow", 2, ".subview-panel"),
                                     ".subview-panel shadow blur"),
        ("kit::kHeadMarginBottom",   px_one(subview_head, "margin-bottom", 0, ".subview-head"),
                                     ".subview-head margin-bottom"),
        ("kit::kCloseSize",          px_one(subclose, "width", 0, ".subclose"),
                                     ".subclose width"),
        ("kit::kSubLineMarginBottom", px_one(subview_sub, "margin-bottom", 0, ".subview-sub"),
                                     ".subview-sub margin-bottom"),
        ("kit::kRowGap",             px_one(sub_rows, "gap", 0, ".sub-rows"),
                                     ".sub-rows gap"),
        ("kit::kRowLabelWidth",      px_one(sub_row, "grid-template-columns", 0, ".sub-row"),
                                     ".sub-row label column"),
        ("kit::kRowLabelGap",        px_one(sub_row, "gap", 0, ".sub-row"),
                                     ".sub-row gap"),
        ("kit::kRowLabelLineGap",    px_one(sub_rowlabel, "gap", 0, ".sub-rowlabel"),
                                     ".sub-rowlabel gap"),
        # translateX(24px) is not a bare px token, so px_list cannot read it —
        # the function wrapper has to come off first. Extracted rather than left
        # unpoliced: an unenrolled constant is a check that cannot fail, which is
        # how kTrailGap shipped as 0 under a comment saying 3px.
        ("kit::kEntranceOffset",     indexed(function_args(subview_panel, "transform", "translateX"),
                                             0, "kit::kEntranceOffset"),
                                     ".subview-panel entrance translateX"),

        # Six MORE, each declared with a css: citation and policed by nothing.
        # Enrolling the header only lets cpp_constant FIND a name; the loop
        # iterates the expectations, so an unlisted constant is never compared —
        # kPadHeight could have been set to 21 and both this script and the
        # geometry test stayed green. /code-review.
        ("kit::kPadHeight",          px_one(css_rule(css, ".sub-row .pads .pad"), "height", 0,
                                            ".sub-row .pads .pad"),
                                     ".sub-row pad height"),
        ("kit::kPanelShadowOffsetX", px_one(subview_panel, "box-shadow", 0, ".subview-panel"),
                                     ".subview-panel shadow x offset"),
        ("kit::kPanelShadowOpacity", indexed(alphas(subview_panel, "box-shadow"), 0,
                                             "kit::kPanelShadowOpacity"),
                                     ".subview-panel shadow alpha"),
        ("kit::kEntranceSeconds",    indexed(seconds_list(subview_panel, "transition"), 0,
                                             "kit::kEntranceSeconds"),
                                     ".subview-panel transition duration"),
        ("kit::kEaseX1",             indexed(entrance_ease, 0, "kit::kEaseX1"),
                                     ".subview-panel easing x1"),
        ("kit::kEaseY1",             indexed(entrance_ease, 1, "kit::kEaseY1"),
                                     ".subview-panel easing y1"),
        ("kit::kEaseX2",             indexed(entrance_ease, 2, "kit::kEaseX2"),
                                     ".subview-panel easing x2"),
        ("kit::kEaseY2",             indexed(entrance_ease, 3, "kit::kEaseY2"),
                                     ".subview-panel easing y2"),

        # ── the side panel, from forrobox.css ──────────────────────────────
        #
        # 06-02, and the first region written under `check_enrolment_coverage`:
        # enrolling SidePanel.h refused the build until all 21 of these had an
        # expectation, which is exactly what that gate is for.
        ("side::kPadY",              px_one(side, "padding", 0, ".side"),
                                     ".side padding-block"),
        ("side::kPadX",              px_one(side, "padding", 1, ".side"),
                                     ".side padding-inline"),
        ("side::kSectionGap",        px_one(side, "gap", 0, ".side"),
                                     ".side gap"),
        ("side::kSectionInnerGap",   px_one(side_sect, "gap", 0, ".side-sect"),
                                     ".side-sect gap"),

        # The 1 px `.profile` and `.timbre` both draw. Enrolled since 06-02 and
        # compared by nothing until coverage started counting declarations
        # rather than set-differencing them — `grmeter::kBorder` and
        # `dragmidi::kBorder` between them satisfied the bare name. 08-04.
        ("side::kBorder",            px_one(profile, "border", 0, ".profile"),
                                     ".profile border width"),
        ("side::kProfileGap",        px_one(profiles, "gap", 0, ".profiles"),
                                     ".profiles gap"),
        ("side::kProfilePadY",       px_one(profile, "padding", 0, ".profile"),
                                     ".profile padding-block"),
        ("side::kProfilePadX",       px_one(profile, "padding", 1, ".profile"),
                                     ".profile padding-inline"),
        ("side::kDescriptionMarginTop",
                                     px_one(profile_desc, "margin-top", 0, ".pf-desc"),
                                     ".profile .pf-desc margin-top"),
        ("side::kDescriptionLineHeight",
                                     indexed(unitless(profile_desc, "line-height"), 0,
                                             "side::kDescriptionLineHeight"),
                                     ".profile .pf-desc line-height"),
        ("side::kDescriptionAlpha",  indexed(alphas(css_rule(css, ".profile.active .pf-desc"),
                                                    "color"), 0, "side::kDescriptionAlpha"),
                                     ".profile.active .pf-desc colour alpha"),
        # The LIGHT override, which is a separate rule with its own colour AND
        # its own alpha. Enrolling only the dark one is how the light theme
        # shipped the wrong value in the first place.
        ("side::kDescriptionAlphaLight",
                                     indexed(alphas(css_rule(css,
                                         '[data-theme="light"] .profile.active .pf-desc'),
                                         "color"), 0, "side::kDescriptionAlphaLight"),
                                     "light .profile.active .pf-desc colour alpha"),
        ("side::kActiveDotSize",     px_one(profile_dot, "font-size", 0, ".pf-name::after"),
                                     ".profile.active .pf-name::after font-size"),

        ("side::kTimbreGap",         px_one(timbre_opts, "gap", 0, ".timbre-opts"),
                                     ".timbre-opts gap"),
        ("side::kTimbrePadY",        px_one(timbre, "padding", 0, ".timbre"),
                                     ".timbre padding-block"),
        ("side::kTimbrePadX",        px_one(timbre, "padding", 1, ".timbre"),
                                     ".timbre padding-inline"),
        ("side::kTimbreLedSize",     px_one(timbre_led, "width", 0, ".tb-led"),
                                     ".timbre .tb-led width"),
        ("side::kTimbreLedGlowRadius",
                                     indexed(px_list(timbre_lit, "box-shadow"), 2,
                                             "side::kTimbreLedGlowRadius"),
                                     ".timbre.active .tb-led glow blur"),
        ("side::kTimbreActiveMix",   1.0 - indexed(percents(timbre_active, "background"), 0,
                                                  "side::kTimbreActiveMix", 0.01),
                                     ".timbre.active background color-mix remainder"),

        ("side::kMixGap",            px_one(mix_row, "gap", 0, ".mix-row"),
                                     ".mix-row gap"),
        # Anchored to the MIX knob's own construction, not to the first `size:`
        # in the file — app.js builds several knobs.
        ("side::kMixKnobSize",       js_number(app,
                                               r'size:\s*(\d+),\s*color:\s*"var\(--c-triangulo\)",'
                                               r'\s*label:\s*"MIX"',
                                               "side::kMixKnobSize", "app.js"),
                                     "app.js MIX knob size"),

        ("side::kCustomTagFadeSeconds",
                                     indexed(seconds_list(custom_tag, "transition"), 0,
                                             "side::kCustomTagFadeSeconds"),
                                     ".custom-tag transition duration"),

        ("side::kBundleGap",         px_one(bundle, "gap", 0, ".bundle"),
                                     ".bundle gap"),
        ("side::kBundlePadTop",      px_one(bundle, "padding-top", 0, ".bundle"),
                                     ".bundle padding-top"),
        ("side::kBundleDotSize",     px_one(bundle_dot, "width", 0, ".bdot"),
                                     ".bundle .bdot width"),
        ("side::kBundleDotGlowRadius",
                                     indexed(px_list(bundle_dot, "box-shadow"), 2,
                                             "side::kBundleDotGlowRadius"),
                                     ".bundle .bdot glow blur"),

        # ── the playhead, from forrobox.css ────────────────────────────────
        #
        # NINE constants citing css:486-498, and until /simplify at 05-02 not one
        # of them was read here — Playhead.h was not in GEOMETRY_HEADERS at all.
        # The cost was concrete: `kTrailGap` shipped as 0 under a comment saying
        # `right: 3px`, dead and contradicting itself, and nothing could see it.
        ("playhead::kLineWidth",          px_one(playhead_rule, "width", 0, ".playhead"),
                                          ".playhead width"),
        # `top: -3px` — the overhang is declared as a positive OUTWARD expansion
        # here and as a negative inset there, so the magnitude is what matches.
        ("playhead::kOverhang",           abs(px_one(playhead_rule, "top", 0, ".playhead")),
                                          ".playhead top overhang"),
        ("playhead::kCornerRadius",       px_one(playhead_rule, "border-radius", 0, ".playhead"),
                                          ".playhead border-radius"),
        ("playhead::kGlowRadius",         px_one(playhead_rule, "box-shadow", 2, ".playhead"),
                                          ".playhead box-shadow blur"),
        ("playhead::kCoreGlowRadius",     px_one(playhead_rule, "box-shadow", 5, ".playhead"),
                                          ".playhead white core blur"),
        ("playhead::kTrailWidth",         px_one(playhead_trail, "width", 0, ".playhead::before"),
                                          ".playhead::before width"),
        ("playhead::kTrailAlpha",         indexed(percents(playhead_trail, "background"), 0,
                                                  "playhead::kTrailAlpha", 0.01),
                                          ".playhead::before gradient colour-mix weight"),

        # ── the trigger LED, from forrobox.css ─────────────────────────────
        #
        # 04-02 reserved every box in the strip's interior stack and missed this
        # one, because it sits INSIDE the head row rather than in the stack.
        # `.strip-head-r` is a flex row holding the LED and the index.
        ("hitviz::kLedDiameter",          px_one(css_rule(css, ".trig-led"), "width", 0,
                                                 ".trig-led"), ".trig-led width"),
        ("hitviz::kLedGap",               px_one(css_rule(css, ".strip-head-r"), "gap", 0,
                                                 ".strip-head-r"), ".strip-head-r gap"),

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

        # The profile reload's confirmation flash — app.js:546's
        # `flashPad(p, 1.6, 340)`, which PLANNING.md:615 states as
        # "brightness 1.6 -> 1 over 340ms". Read from the call itself, so the
        # strength and the duration cannot drift apart from the one site that
        # sets them.
        ("pad::kFlashStrength",           js_number(app, r"flashPad\(p,\s*([\d.]+),\s*\d+\)",
                                               "pad::kFlashStrength", "app.js"),
                                     "app.js flashPads strength"),
        ("pad::kFlashSeconds",            js_number(app, r"flashPad\(p,\s*[\d.]+,\s*(\d+)\)",
                                               "pad::kFlashSeconds", "app.js") / 1000.0,
                                     "app.js flashPads duration"),
        ("pad::kDimmedAlpha",             px_one(seq_row_dimmed, "opacity", 0, "seq_row_dimmed"),
                                     ".seq-row.dimmed opacity (mute and isolate)"),

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
        # The design size every layout number in this project is a fraction of.
        # Enrolled since the gate existed and compared by nothing until 08-04:
        # the qualified lookup could not see inside a struct, and the bare one
        # found three kWidths.
        ("ChassisLayout::kWidth",    px_one(fb_window, "width", 0, ".fb-window"),
                                     ".fb-window width"),
        ("ChassisLayout::kHeight",   px_one(fb_window, "height", 0, ".fb-window"),
                                     ".fb-window height"),

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

        # ── the BPM field ──────────────────────────────────────────────────
        ("bpmfield::kPadX",          px_one(bpm_rule, "padding", 1, ".bpm"),
                                     ".bpm padding, horizontal"),
        ("bpmfield::kPadY",          px_one(bpm_rule, "padding", 0, ".bpm"),
                                     ".bpm padding, vertical"),
        ("bpmfield::kMinWidth",      px_one(bpm_rule, "min-width", 0, ".bpm"), ".bpm min-width"),
        ("bpmfield::kClusterGap",    px_one(bpm_cluster, "gap", 0, ".bpm-cluster"),
                                     ".bpm-cluster gap"),
        ("bpmfield::kMiniGap",       px_one(mini_btns, "gap", 0, ".mini-btns"), ".mini-btns gap"),

        # The drag law itself, from app.js — behaviour, in no stylesheet, and
        # the C++ asserts 40 px -> 20 BPM against the same constant it is built
        # from. Same argument the velocity law earned in 04-03.
        ("bpmfield::kBpmPerPixel",   js_number(app, r"\(sy - ev\.clientY\) \* ([\d.]+)",
                                               "bpmfield::kBpmPerPixel", "app.js"),
                                     "app.js wireBPM BPM-per-pixel"),

        # ── the header's two new button variants ───────────────────────────
        ("kMiniPadX",                px_one(mini_btn, "padding", 1, ".mini-btn"),
                                     ".mini-btn padding, horizontal"),
        ("kMiniPadY",                px_one(mini_btn, "padding", 0, ".mini-btn"),
                                     ".mini-btn padding, vertical"),
        ("kTransportSize",           px_one(tp_btn, "width", 0, ".tp-btn"), ".tp-btn width"),
        ("kTransportIcon",           px_one(css_rule(css, ".tp-btn svg"), "width", 0, ".tp-btn svg"),
                                     ".tp-btn svg width"),
        ("kTransportGlowRadius",     indexed(px_list(tp_play_on, "box-shadow"), 2,
                                             "kTransportGlowRadius"),
                                     ".tp-btn.play.on box-shadow blur"),
        ("kTransportGlowOpacity",    indexed(percents(tp_play_on, "box-shadow"), 0,
                                             "kTransportGlowOpacity", 0.01),
                                     ".tp-btn.play.on glow colour-mix weight"),

        # ── the segmented control ──────────────────────────────────────────
        ("segmented::kPadX",         px_one(qs_btn, "padding", 1, ".qs-btn"),
                                     ".qs-btn padding, horizontal"),
        ("segmented::kPadY",         px_one(qs_btn, "padding", 0, ".qs-btn"),
                                     ".qs-btn padding, vertical"),
        ("segmented::kHoverGroundPct", indexed(percents(css_rule(css, ".qs-btn:hover"),
                                                        "background"), 0,
                                               "segmented::kHoverGroundPct"),
                                     ".qs-btn:hover background colour-mix weight"),
        ("segmented::kInsetAlpha",   indexed(alphas(quick_switch, "box-shadow"), 0,
                                             "segmented::kInsetAlpha"),
                                     ".quick-switch box-shadow alpha"),

        # ── the logo mark ──────────────────────────────────────────────────
        #
        # The stroke widths are what make it a MONOLINE mark, and the three
        # differ on purpose: 1.5 / 1.7 + 1.0 / 1.9, one per sub-mark.
        ("logo::kWidth",             px_one(logo_mark, "width", 0, ".logo-mark"),
                                     ".logo-mark width"),
        ("logo::kHeight",            px_one(logo_mark, "height", 0, ".logo-mark"),
                                     ".logo-mark height"),
        ("logo::kSanfonaStroke",     px_one(css_rule(css, ".logo-mark .lm-sanfona path"),
                                            "stroke-width", 0, ".lm-sanfona path"),
                                     ".lm-sanfona stroke-width"),
        ("logo::kZabumbaStroke",     px_one(css_rule(css, ".logo-mark .lm-zabumba"),
                                            "stroke-width", 0, ".lm-zabumba"),
                                     ".lm-zabumba stroke-width"),
        ("logo::kRodStroke",         px_one(css_rule(css, ".logo-mark .lm-zabumba-rods"),
                                            "stroke-width", 0, ".lm-zabumba-rods"),
                                     ".lm-zabumba-rods stroke-width"),
        ("logo::kLockupGap",         px_one(css_rule(css, ".logo-lockup"), "gap", 0,
                                            ".logo-lockup"),
                                     ".logo-lockup gap"),

        # The mark's SVG geometry, from app.js — the same argument controls.js
        # earned for the knob's radii: it is path geometry, so no stylesheet
        # can carry it, and nothing else would police it.
        ("logo::kViewBoxWidth",      js_number(app, r'class="logo-mark" viewBox="0 0 ([\d.]+) [\d.]+"',
                                               "logo::kViewBoxWidth", "app.js"),
                                     "app.js logo-mark viewBox width"),
        ("logo::kViewBoxHeight",     js_number(app, r'class="logo-mark" viewBox="0 0 [\d.]+ ([\d.]+)"',
                                               "logo::kViewBoxHeight", "app.js"),
                                     "app.js logo-mark viewBox height"),
        ("logo::kZabumbaCx",         js_number(app, r'class="lm-zabumba" cx="([\d.]+)"',
                                               "logo::kZabumbaCx", "app.js"),
                                     'app.js lm-zabumba cx'),
        ("logo::kZabumbaCy",         js_number(app, r'class="lm-zabumba" cx="[\d.]+" cy="([\d.]+)"',
                                               "logo::kZabumbaCy", "app.js"),
                                     'app.js lm-zabumba cy'),
        ("logo::kZabumbaR",          js_number(app, r'class="lm-zabumba" cx="[\d.]+" cy="[\d.]+" rx="([\d.]+)"',
                                               "logo::kZabumbaR", "app.js"),
                                     'app.js lm-zabumba rx'),

        # ── the footer, 04-05 ─────────────────────────────────────────────
        ("footer::kPadX",            px_one(footer_rule, "padding", 1, ".footer"),
                                     ".footer padding, horizontal"),
        ("footer::kGap",             px_one(footer_rule, "gap", 0, ".footer"), ".footer gap"),
        ("footer::kGroupGap",        px_one(foot_group, "gap", 0, ".foot-group"),
                                     ".foot-group gap"),
        ("footer::kMasterFaderWidth", px_one(master_fader, "width", 0, ".master-fader"),
                                     ".master-fader width"),

        ("grmeter::kWidth",          px_one(gr_meter, "width", 0, ".gr-meter"), ".gr-meter width"),
        ("grmeter::kHeight",         px_one(gr_meter, "height", 0, ".gr-meter"),
                                     ".gr-meter height"),
        ("grmeter::kBorder",         px_one(gr_meter, "border", 0, ".gr-meter"),
                                     ".gr-meter border width"),
        # The one DURATION in the design that reaches a C++ constant.
        ("grmeter::kDecaySeconds",   indexed(seconds_list(gr_fill, "transition"), 0,
                                             "grmeter::kDecaySeconds"),
                                     ".gr-fill transition duration"),

        # ── DRAG MIDI ─────────────────────────────────────────────────────
        ("dragmidi::kPadY",    px_one(drag_midi, "padding", 0, ".drag-midi"),
                                     ".drag-midi padding, vertical"),
        ("dragmidi::kPadX",    px_one(drag_midi, "padding", 1, ".drag-midi"),
                                     ".drag-midi padding, horizontal"),
        ("dragmidi::kGap",     px_one(drag_midi, "gap", 0, ".drag-midi"), ".drag-midi gap"),
        ("dragmidi::kBorder",  px_one(drag_midi, "border", 0, ".drag-midi"),
                                     ".drag-midi border width"),
        ("dragmidi::kTintPct", indexed(percents(drag_midi, "background"), 0,
                                             "dragmidi::kTintPct"),
                                     ".drag-midi background colour-mix weight"),
        ("dragmidi::kBorderPct", indexed(percents(drag_midi, "border"), 0,
                                               "dragmidi::kBorderPct"),
                                     ".drag-midi border colour-mix weight"),
        ("dragmidi::kRingPct", indexed(percents(drag_midi, "box-shadow"), 0,
                                             "dragmidi::kRingPct"),
                                     ".drag-midi box-shadow ring colour-mix weight"),
        # `0 0 0 1px <colour>` — offset-x, offset-y, blur, SPREAD, so the ring
        # width is the fourth length and not the third. This script caught that
        # off-by-one the first time it ran, which is what it is for.
        ("dragmidi::kRingWidth", px_one(drag_midi, "box-shadow", 3, ".drag-midi"),
                                     ".drag-midi box-shadow ring spread"),
        ("dragmidi::kInsetAlpha", indexed(alphas(drag_midi, "box-shadow"), 0,
                                                "dragmidi::kInsetAlpha"),
                                     ".drag-midi box-shadow inset alpha"),
        ("dragmidi::kHoverTintPct", indexed(percents(drag_hover, "background"), 0,
                                                  "dragmidi::kHoverTintPct"),
                                     ".drag-midi:hover background colour-mix weight"),
        ("dragmidi::kHoverRingWidth", px_one(drag_hover, "box-shadow", 3, ".drag-midi:hover"),
                                     ".drag-midi:hover box-shadow ring spread"),
        ("dragmidi::kHoverGlowRadius", px_one(drag_hover, "box-shadow", 6,
                                                    ".drag-midi:hover"),
                                     ".drag-midi:hover box-shadow glow blur"),
        ("dragmidi::kHoverGlowPct", indexed(percents(drag_hover, "box-shadow"), 0,
                                                  "dragmidi::kHoverGlowPct"),
                                     ".drag-midi:hover glow colour-mix weight"),
        ("dragmidi::kPressScale", scale_one(drag_active, "dragmidi::kPressScale"),
                                     ".drag-midi:active transform scale"),

        # ── the idle pulse ─────────────────────────────────────────────────
        #
        # kPulseSeconds comes from `.drag-midi`'s own `animation` shorthand; the
        # other four live only in the keyframes.
        #
        # PLANNING.md:499 describes the breath as the outer glow alone. The
        # stylesheet also takes the 1px RING from 18% to 35% on the same cycle,
        # which is why kPulseRingPct is compared here rather than trusted.
        ("dragmidi::kPulseSeconds", indexed(seconds_list(drag_midi, "animation"), 0,
                                                  "dragmidi::kPulseSeconds"),
                                     ".drag-midi animation duration"),
        # `0 0 0 1px <35%>, 0 0 20px <28%>, inset ...` — the colour-mix weights
        # in source order, so the ring's is first and the glow's second.
        ("dragmidi::kPulseRingPct", indexed(percents(midipulse_peak, "box-shadow"), 0,
                                                  "dragmidi::kPulseRingPct"),
                                     "@keyframes midipulse 50% ring colour-mix weight"),
        ("dragmidi::kPulseGlowPct", indexed(percents(midipulse_peak, "box-shadow"), 1,
                                                  "dragmidi::kPulseGlowPct"),
                                     "@keyframes midipulse 50% glow colour-mix weight"),
        # The glow's BLUR is the seventh length: `0 0 0 1px` is four, then
        # `0 0 20px` puts the blur at index six. Same off-by-one the ring spread
        # caught when this script first ran.
        ("dragmidi::kPulseGlowRadius", px_one(midipulse_peak, "box-shadow", 6,
                                                    "@keyframes midipulse 50%"),
                                     "@keyframes midipulse 50% glow blur"),
        # ── the CACHAÇA easter egg's sway, 08-04 ───────────────────────────
        #
        # The THRESHOLD is behaviour rather than geometry, so it comes from
        # app.js where the wash's own 65/35 does — `verify-theme` reads that
        # pair from the same line. It is here because it is declared in an
        # enrolled header, and a constant in one of those is compared or
        # excused; excusing it would have been false, since it has a design
        # source.
        ("kTipsyPercent", js_number(app, r"const tipsy\s*=\s*c\s*>=\s*(\d+)",
                                    "kTipsyPercent", "app.js"),
                                     "app.js drunk easter egg sway threshold"),
        ("kSwaySeconds", indexed(seconds_list(tipsy_rule, "animation"), 0, "kSwaySeconds"),
                                     ".fb-window.tipsy animation duration"),
        # `rotate(0.18deg)` — the same shape `function_args` reads `scale(0.94)`
        # and `translateX(24px)` as. The 75% keyframe is its negation, which the
        # C++ writes as `-kSwayDegrees` rather than as a second constant.
        ("kSwayDegrees", indexed(function_args(sway_peak, "transform", "rotate"), 0,
                                 "kSwayDegrees"),
                                     "@keyframes sway 25% rotation"),
        ("kLabelPulseSeconds", indexed(seconds_list(drunk_on, "animation"), 0,
                                       "kLabelPulseSeconds"),
                                     ".gk-name.drunk-on animation duration"),
        ("kLabelPulseLowOpacity", indexed(unitless(drunkpulse_low, "opacity"), 0,
                                          "kLabelPulseLowOpacity"),
                                     "@keyframes drunkpulse 0% opacity"),
        ("kLabelPulseHighOpacity", indexed(unitless(drunkpulse_high, "opacity"), 0,
                                           "kLabelPulseHighOpacity"),
                                     "@keyframes drunkpulse 50% opacity"),

        # `function_args`, not a fourth bespoke transform reader: its own
        # docstring names `translateX(24px)` as the same shape, and the
        # kEntranceOffset row 400 lines above reads its curve exactly this way.
        ("dragmidi::kArrowBobPx", indexed(function_args(midiarrow_peak, "transform",
                                                              "translateY"), 0,
                                                "dragmidi::kArrowBobPx"),
                                     "@keyframes midiarrow 50% translateY"),

        # ── the OUTPUT toggle's box model ─────────────────────────────────
        ("segmented::kOutPadY",      px_one(out_toggle_btn, "padding", 0, ".out-toggle .ot"),
                                     ".out-toggle .ot padding, vertical"),
        ("segmented::kOutPadX",      px_one(out_toggle_btn, "padding", 1, ".out-toggle .ot"),
                                     ".out-toggle .ot padding, horizontal"),

        # ── the sequencer, 05-01 ──────────────────────────────────────────
        ("seq::kPadTop",             px_one(seq_rule, "padding", 0, ".seq"),
                                     ".seq padding, top"),
        ("seq::kPadSide",            px_one(seq_rule, "padding", 1, ".seq"),
                                     ".seq padding, sides"),
        ("seq::kPadBottom",          px_one(seq_rule, "padding", 2, ".seq"),
                                     ".seq padding, bottom"),
        ("seq::kHeadMarginBottom",   px_one(seq_head, "margin-bottom", 0, ".seq-head"),
                                     ".seq-head margin-bottom"),
        ("seq::kHeadGap",            px_one(sh_left, "gap", 0, ".seq-head .sh-left"),
                                     ".sh-left gap"),
        ("seq::kStepsGap",           px_one(seq_len, "gap", 0, ".seq-len"), ".seq-len gap"),

        # The DECLARED row gap, which does not fit and is deliberately not what
        # is drawn — see seq::kDeclaredRowGap. Cross-checked anyway: the constant
        # names the stylesheet's number, so it must still BE the stylesheet's.
        ("seq::kDeclaredRowGap",     px_one(seq_wrap, "gap", 0, ".seq-grid-wrap"),
                                     ".seq-grid-wrap gap"),

        ("seq::kLabelWidth",         px_one(seq_row, "grid-template-columns", 0, ".seq-row"),
                                     ".seq-row label column"),
        ("seq::kLabelGap",           px_one(seq_row, "gap", 0, ".seq-row"), ".seq-row gap"),
        ("seq::kChipGap",            px_one(seq_rowlabel, "gap", 0, ".seq-rowlabel"),
                                     ".seq-rowlabel gap"),
        ("seq::kChipWidth",          px_one(rl_chip, "width", 0, ".rl-chip"), ".rl-chip width"),
        ("seq::kChipHeight",         px_one(rl_chip, "height", 0, ".rl-chip"), ".rl-chip height"),
        ("seq::kChipRadius",         px_one(rl_chip, "border-radius", 0, ".rl-chip"),
                                     ".rl-chip border-radius"),

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
        elif math.isnan(expected):   # already recorded by px_one / indexed
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
        ("miniButtonLabel", ".mini-btn",            "css:181"),
        ("presetScreen",    ".preset .pscreen",     "css:225"),
        ("quickSwitchCode", ".qs-btn",              "css:246"),
        ("styleLabel",      ".style-label",         "css:239"),
        ("bpmReadout",      ".bpm",                 "css:172"),
        ("globalKnobReadout", ".gk .gk-read",       "css:214"),
        ("globalKnobName",  ".gk .gk-name",         "css:219"),
        ("wordmark",        ".wordmark",            "css:165"),
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

        # The footer's five, 04-05.
        ("footerLabel",     ".foot-label",          "css:511"),
        ("dragMidiArrow",   ".drag-midi .dm-arrow", "css:539"),
        ("dragMidiLabel",   ".drag-midi .dm-text",  "css:542"),
        ("dragMidiSub",     ".drag-midi .dm-sub",   "css:543"),
        ("outToggleLabel",  ".out-toggle .ot",      "css:546"),
        ("seqHint",         ".seq-len",             "css:500"),
        ("sectionLabel",    ".sect-label",          "css:125"),
        # 06-02: four rows PLANNING.md's table carries and forrobox.css also
        # declares. The script only ever read the css-declared rows, and these
        # four were css-declared all along — nothing was comparing them.
        ("profileName",     ".profile .pf-name",    "css:399"),
        ("profileDescription", ".profile .pf-desc", "css:400"),
        ("timbreName",      ".timbre .tb-name",     "css:423"),
        ("timbreSubLabel",  ".timbre .tb-sub",      "css:424"),
        # And two the type table does NOT carry — forrobox.css is their only
        # source, as with the four kit rows.
        ("customTag",       ".custom-tag",          "css:409"),
        ("bundleText",      ".bundle .btxt",        "css:440"),
        ("sequencerRowLabel", ".seq-rowlabel",      "css:453"),

        # The kit overlay's four. Added in the same commit as the styles
        # themselves — 05-02 enrolled a header's constants and forgot its type
        # scale is policed separately, and the count not moving is what showed it.
        ("kitTitle",        ".subview-head h3",     "css:569"),
        ("kitSubLine",      ".subview-sub",         "css:571"),
        ("kitRowName",      ".sub-rowlabel .srl-name", "css:581"),
        ("kitRowFull",      ".sub-rowlabel .srl-full", "css:582"),
    ]

    for style, selector, source in type_rules:
        row = type_row(typography, style)

        if row is None:
            MISSING.append(f"type::Style::{style}: no row found in {TYPOGRAPHY_HEADER.name}")
            continue

        block = css_rule(css, selector)
        expected_px = px_one(block, "font-size", 0, f"{selector} font-size")
        expected_em = em_one(block, selector)

        if not math.isnan(expected_px) and abs(row[0] - expected_px) > 1e-6:
            failures.append(f"type::Style::{style}: size {row[0]:g} != spec {expected_px:g}"
                            f"  [{selector}, {source}]")

        if not math.isnan(expected_em) and abs(row[1] - expected_em) > 1e-6:
            failures.append(f"type::Style::{style}: tracking {row[1]:g}em != spec {expected_em:g}em"
                            f"  [{selector}, {source}]")

    # No bespoke total check for the fader's box any more. It used to compare
    # ChassisLayout::kFaderHeight against `2 * padding + track height`, which
    # only existed because that constant was a literal 20 duplicating
    # fader::kHeight. It is now `= fader::kHeight`, so the compiler derives the
    # total and the two halves are each checked above against their own rule —
    # which is all the old check's "a 20 made of 6+8 would pass" argument asked
    # for. Found by /simplify.

    # MISSING is copied LAST, so a px_one/js_number failure recorded anywhere
    # above still reaches the report. It used to be copied before the fader
    # check ran, so a missing .fb-fader-track height produced a NaN that made
    # `abs(cpp - nan) > 1e-6` false — the comparison silently passed and the
    # recorded failure was already out of scope. A check that could not fail.
    # The gate on the expectations table itself — see UNCHECKED_BASELINE. Run
    # here rather than beside the loop, because it reads the same `expectations`
    # the loop consumed and must not be able to disagree with it.
    failures += check_enrolment_coverage(header, expectations)

    failures = MISSING + failures

    if failures:
        print("Strip geometry cross-check FAILED", file=sys.stderr)
        for line in failures:
            print(f"  {line}", file=sys.stderr)
        return 1

    print(f"Strip geometry cross-check OK — {len(expectations)} lengths and "
          f"{len(type_rules) * 2} type-scale values "
          f"against forrobox.css, controls.js and app.js")
    return 0


if __name__ == "__main__":
    sys.exit(main())
