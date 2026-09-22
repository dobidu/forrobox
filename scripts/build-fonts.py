#!/usr/bin/env python3
"""Build assets/fonts/ from upstream sources, reproducibly.

Why this script exists rather than a note saying where the fonts came from: four
of the seven embedded weights do not exist upstream as files. Space Grotesk is
published as a VARIABLE font only -- google/fonts carries no statics for it, and
floriankarsten/space-grotesk has Light/Regular/Medium/Bold but no SemiBold at
all, while PLANNING.md's type scale uses 600 for five things (instrument names,
sequencer row labels, profile names, timbre names, global knob labels).

Loading the variable font directly is not an alternative. JUCE 8.0.12 bundles
HarfBuzz with the variable tables, but juce::Typeface's public API exposes no
variation-axis setter, so createSystemTypefaceFor loads the face's DEFAULT
instance -- and Space Grotesk's fvar default is wght=300, Light. Every weight in
the UI would render at the thinnest one, with nothing failing.

So the four statics are instanced offline here and committed. Two traps, both hit
during planning and both worth stating because the failure is silent:

  1. `--update-name-table` does not work: fontTools raises
     "ValueError: Cannot find Axis Values {'wght': 600.0}" because the STAT table
     declares no named axis value at 600.
  2. Without it, the instancer sets usWeightClass correctly but leaves name ID 1
     as "Space Grotesk Light" and name ID 2 as "Regular" for EVERY weight, so
     JUCE reports all four as the same family and style. The name records are
     therefore written explicitly, in both the Windows (3,1,0x409) and Mac
     (1,0,0) encodings.

IBM Plex Mono does ship real statics at exactly the three weights the spec asks
for, so those are used as fetched, unmodified.

fonttools is a build-time asset tool run rarely and its output committed. It is
NOT linked into the plugin; the plugin's only dependency remains JUCE.

Usage:
  build-fonts.py            fetch, build, write assets/fonts/ and its manifest
  build-fonts.py --verify   re-derive everything and prove the committed files
                            are byte-identical (needs network)
  build-fonts.py --check    offline: verify the committed files against the
                            manifest's SHA-256 digests
"""
from __future__ import annotations

import argparse
import hashlib
import io
import pathlib
import sys
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
FONT_DIR = ROOT / "assets" / "fonts"
MANIFEST = FONT_DIR / "MANIFEST.sha256"

GF = "https://raw.githubusercontent.com/google/fonts/main/ofl"
SG_DIR = f"{GF}/spacegrotesk"
PM_DIR = f"{GF}/ibmplexmono"

# The variable source. Deliberately NOT committed: a committed VF invites
# someone to load it directly and get Light for every weight.
SG_VARIABLE = f"{SG_DIR}/SpaceGrotesk%5Bwght%5D.ttf"

# wght -> the style name JUCE will report. PLANNING.md's type scale uses all four.
SG_WEIGHTS = {400: "Regular", 500: "Medium", 600: "SemiBold", 700: "Bold"}

# Fetched as-is; upstream publishes real statics at both weights the type scale
# asks for.
#
# SEMIBOLD WAS DROPPED AT 08-03. It was embedded from 04-01 and registered in
# Typography.cpp's resource table, and no row of `typeSpecs` ever asked for it —
# only monoRegular and monoMedium appear. 140 KB of binary for a weight nothing
# drew.
PM_STATICS = ["Regular", "Medium"]

# ── the two optional display fonts, added at 08-03 ─────────────────────────
#
# `PLANNING.md:860` offers three mono families as a user setting and `:885` calls
# these two "optional". They differ in exactly the way 04-01 already met:
#
#   JetBrains Mono is published as a VARIABLE font only, so it is instanced
#   offline here at the two weights the type scale asks for. Loading the VF
#   directly would render every weight at its fvar default, silently — which is
#   the Space Grotesk trap this file was written for.
#
#   Space Mono ships real statics and is NOT variable, so its Regular is used as
#   fetched. It has no 500 and there is nothing to instance; `Typography.cpp`
#   resolves a monoMedium request against it by falling to 400, which is what CSS
#   Fonts 4 section 5.2 has a browser do with {400, 700}. That mapping lives in
#   the C++ and is NOT faked here — synthesising a weight would put a font in
#   assets/ that no foundry published.
JB_DIR = f"{GF}/jetbrainsmono"
JB_VARIABLE = f"{JB_DIR}/JetBrainsMono%5Bwght%5D.ttf"
JB_WEIGHTS = {400: "Regular", 500: "Medium"}

SM_DIR = f"{GF}/spacemono"
SM_STATICS = ["Regular"]

# Name IDs rewritten per instance. 1/2 are the legacy family/subfamily pair,
# 4 the full name, 6 the PostScript name, 16/17 the typographic pair.
NAME_IDS = (1, 2, 4, 6, 16, 17)
ENCODINGS = ((3, 1, 0x409), (1, 0, 0))   # Windows/Unicode BMP, Mac/Roman

# THE REPORT'S SAMPLE, not the contract. `verify-charset.py`'s `ALLOWED` is the
# contract — 36 characters, pinned against tests/UiTest.cpp's own array — and it
# is what fails a build. This is a readable subset for the printed line, and it
# is deliberately not the authority: a second hand-kept copy of a repertoire is
# what src/ParameterIDs.h names as the failure, and 08-03 briefly made this one
# authoritative over a user-selectable font while it covered 10 of the 36.
REQUIRED_GLYPHS = "ÓÂÁÇÃàéíúü"
PHASE8_GLYPH = "♪"


def fetch(url: str) -> bytes:
    with urllib.request.urlopen(url, timeout=60) as response:
        return response.read()


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def instance_variable_font(variable: bytes, weight: int, style: str,
                           family: str) -> bytes:
    """One static instance of a variable font, with its name table corrected.

    GENERALISED AT 08-03 from `instance_space_grotesk`. JetBrains Mono needs
    exactly this and a second copy would have diverged at the first fix — the
    timestamp pinning below took two runs of `--verify` to find, and it would
    have had to be found twice."""
    from fontTools import ttLib
    from fontTools.varLib import instancer

    font = ttLib.TTFont(io.BytesIO(variable))
    instancer.instantiateVariableFont(font, {"wght": weight}, inplace=True)

    full = family if style == "Regular" else f"{family} {style}"
    values = {
        1: family,
        2: style,
        4: full,
        6: f"{family.replace(' ', '')}-{style}",   # the PostScript name has no spaces
        16: family,
        17: style,
    }

    names = font["name"]
    for name_id in NAME_IDS:
        for platform, encoding, language in ENCODINGS:
            names.setName(values[name_id], name_id, platform, encoding, language)

    # The instancer already sets this, but stating it makes the file's declared
    # weight independent of that behaviour rather than dependent on it.
    font["OS/2"].usWeightClass = weight

    # Pin the timestamps to the SOURCE's, or the output is not reproducible:
    # fontTools stamps head.modified with the save time, so two runs a second
    # apart produce different bytes and --verify fails on correct output. Found
    # by --verify on this script's own first run -- the fetched IBM Plex files
    # matched and all four instanced ones did not.
    #
    # The source's own created/modified are the honest values: an instance of
    # a font was not authored later than the font it came from. Deliberately not
    # SOURCE_DATE_EPOCH (which fontTools does honour): an asset build whose
    # output depends on an environment variable is reproducible only for whoever
    # remembers to set it.
    source = ttLib.TTFont(io.BytesIO(variable))
    font["head"].created = source["head"].created
    font["head"].modified = source["head"].modified

    # Assigning head.modified is NOT enough on its own: fontTools recomputes it
    # from the clock inside the head table's compile step, so the assignment is
    # overwritten on save and head.checkSumAdjustment follows it. Measured while
    # chasing this: two instances built one second apart differed in exactly
    # three bytes -- two in head at +11 (modified) and one in the checksum.
    font.recalcTimestamp = False

    out = io.BytesIO()
    font.save(out)
    return out.getvalue()


def report_glyph_coverage(data: bytes, label: str) -> str:
    """Prints coverage and returns the missing characters.

    A REPORT, not a gate — `verify-charset.py` owns the gate, against the full
    36-character repertoire and on every build. This exists so that whoever adds
    a family sees the answer while they are adding it, rather than on the next
    compile."""
    from fontTools import ttLib

    font = ttLib.TTFont(io.BytesIO(data))
    covered = set()
    for table in font["cmap"].tables:
        covered.update(table.cmap.keys())

    missing = "".join(c for c in REQUIRED_GLYPHS if ord(c) not in covered)
    note = "all present" if not missing else f"MISSING {missing}"
    phase8 = "yes" if ord(PHASE8_GLYPH) in covered else "no"
    print(f"  {label}: accented {note}; U+266A (Phase 8) {phase8}")

    # U+266A is deliberately NOT a failure: 04-01 recorded that no shipped family
    # carries it and the `NO PONTO` easter egg has to solve that its own way.
    return missing


def build() -> dict[str, bytes]:
    """Every byte of assets/fonts/, derived from upstream in one place."""
    assets: dict[str, bytes] = {}

    print("Space Grotesk — fetching the variable source")
    variable = fetch(SG_VARIABLE)
    print(f"  SpaceGrotesk[wght].ttf {len(variable)} bytes (not committed)")

    for weight, style in SG_WEIGHTS.items():
        data = instance_variable_font(variable, weight, style, "Space Grotesk")
        assets[f"SpaceGrotesk-{style}.ttf"] = data
        print(f"  instanced wght={weight} -> {style} ({len(data)} bytes)")

    assets["SpaceGrotesk-OFL.txt"] = fetch(f"{SG_DIR}/OFL.txt")

    print("IBM Plex Mono — fetching statics as published")
    for style in PM_STATICS:
        name = f"IBMPlexMono-{style}.ttf"
        assets[name] = fetch(f"{PM_DIR}/{name}")
        print(f"  {name} ({len(assets[name])} bytes)")

    assets["IBMPlexMono-OFL.txt"] = fetch(f"{PM_DIR}/OFL.txt")

    print("JetBrains Mono — fetching the variable source")
    jb_variable = fetch(JB_VARIABLE)
    print(f"  JetBrainsMono[wght].ttf {len(jb_variable)} bytes (not committed)")

    for weight, style in JB_WEIGHTS.items():
        data = instance_variable_font(jb_variable, weight, style, "JetBrains Mono")
        assets[f"JetBrainsMono-{style}.ttf"] = data
        print(f"  instanced wght={weight} -> {style} ({len(data)} bytes)")

    assets["JetBrainsMono-OFL.txt"] = fetch(f"{JB_DIR}/OFL.txt")

    print("Space Mono — fetching statics as published (no variable source exists)")
    for style in SM_STATICS:
        name = f"SpaceMono-{style}.ttf"
        assets[name] = fetch(f"{SM_DIR}/{name}")
        print(f"  {name} ({len(assets[name])} bytes)")

    assets["SpaceMono-OFL.txt"] = fetch(f"{SM_DIR}/OFL.txt")

    # EVERY EMBEDDED FAMILY, not the two that happened to be here first. A
    # display font the user can select and that cannot draw `Ç` renders the
    # Portuguese UI as boxes the moment it is chosen, and nothing in the C++
    # would notice.
    print("Glyph coverage")

    # REPORTED HERE, GATED IN `verify-charset.py`. This print is a convenience
    # for whoever is adding a family; the check that can FAIL lives beside the
    # repertoire it checks against, runs on every build rather than only on a
    # networked font rebuild, and compares all 36 drawn characters rather than
    # the 10 this file used to keep. /simplify caught both halves.
    #
    # The roster is DERIVED from what was just built, not hand-listed: a family
    # added to the constants but forgotten in a tuple would have been a gate
    # that could not fail for the family it was added for.
    for file in sorted(f for f in assets if f.endswith("-Regular.ttf")):
        report_glyph_coverage(assets[file], file[: -len("-Regular.ttf")])

    return assets


def write(assets: dict[str, bytes]) -> None:
    FONT_DIR.mkdir(parents=True, exist_ok=True)
    for name, data in sorted(assets.items()):
        (FONT_DIR / name).write_bytes(data)

    lines = [f"{sha(data)}  {name}" for name, data in sorted(assets.items())]
    MANIFEST.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"\nWrote {len(assets)} files and MANIFEST.sha256 to {FONT_DIR}")


def verify(assets: dict[str, bytes]) -> int:
    """Re-derived bytes against the committed ones. Exit 1 naming every mismatch."""
    failures = []

    for name, data in sorted(assets.items()):
        path = FONT_DIR / name
        if not path.exists():
            failures.append(f"{name}: missing from {FONT_DIR}")
        elif path.read_bytes() != data:
            failures.append(f"{name}: differs from the re-derived bytes "
                            f"(committed {sha(path.read_bytes())[:12]}, "
                            f"rebuilt {sha(data)[:12]})")

    extra = {p.name for p in FONT_DIR.iterdir() if p.is_file()} - set(assets) - {MANIFEST.name}
    failures.extend(f"{name}: present but not produced by this script" for name in sorted(extra))

    if failures:
        print("\nFAIL")
        for line in failures:
            print(f"  {line}")
        return 1

    print(f"\nOK — {len(assets)} files byte-identical to the re-derived output")
    return 0


def check() -> int:
    """Offline integrity: committed files against the committed digests."""
    if not MANIFEST.exists():
        print(f"FAIL: {MANIFEST} is missing — run build-fonts.py to create it")
        return 1

    expected = {}
    for line in MANIFEST.read_text(encoding="utf-8").splitlines():
        if line.strip():
            digest, name = line.split(maxsplit=1)
            expected[name.strip()] = digest

    failures = []
    for name, digest in sorted(expected.items()):
        path = FONT_DIR / name
        if not path.exists():
            failures.append(f"{name}: missing")
        elif sha(path.read_bytes()) != digest:
            failures.append(f"{name}: digest mismatch")

    present = {p.name for p in FONT_DIR.iterdir() if p.is_file()} - {MANIFEST.name}
    failures.extend(f"{name}: not listed in the manifest" for name in sorted(present - set(expected)))

    if failures:
        print("FAIL")
        for line in failures:
            print(f"  {line}")
        return 1

    print(f"OK — {len(expected)} font assets match MANIFEST.sha256")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--verify", action="store_true",
                       help="re-derive from upstream and prove the committed files match")
    group.add_argument("--check", action="store_true",
                       help="offline: committed files against MANIFEST.sha256")
    args = parser.parse_args()

    if args.check:
        return check()

    assets = build()

    if args.verify:
        return verify(assets)

    write(assets)
    return 0


if __name__ == "__main__":
    sys.exit(main())
