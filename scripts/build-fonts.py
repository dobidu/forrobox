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

# Fetched as-is; upstream publishes real statics at these three weights.
PM_STATICS = ["Regular", "Medium", "SemiBold"]

# Name IDs rewritten per instance. 1/2 are the legacy family/subfamily pair,
# 4 the full name, 6 the PostScript name, 16/17 the typographic pair.
NAME_IDS = (1, 2, 4, 6, 16, 17)
ENCODINGS = ((3, 1, 0x409), (1, 0, 0))   # Windows/Unicode BMP, Mac/Roman

# Checked, reported, and deliberately not acted on in 04-01: the Portuguese UI
# copy needs these, and Phase 8's "NO PONTO" easter egg needs the eighth note.
REQUIRED_GLYPHS = "ÓÂÁÇÃàéíúü"
PHASE8_GLYPH = "♪"


def fetch(url: str) -> bytes:
    with urllib.request.urlopen(url, timeout=60) as response:
        return response.read()


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def instance_space_grotesk(variable: bytes, weight: int, style: str) -> bytes:
    """One static instance of the variable font, with its name table corrected."""
    from fontTools import ttLib
    from fontTools.varLib import instancer

    font = ttLib.TTFont(io.BytesIO(variable))
    instancer.instantiateVariableFont(font, {"wght": weight}, inplace=True)

    family = "Space Grotesk"
    full = family if style == "Regular" else f"{family} {style}"
    values = {
        1: family,
        2: style,
        4: full,
        6: f"SpaceGrotesk-{style}",
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


def report_glyph_coverage(data: bytes, label: str) -> None:
    from fontTools import ttLib

    font = ttLib.TTFont(io.BytesIO(data))
    covered = set()
    for table in font["cmap"].tables:
        covered.update(table.cmap.keys())

    missing = [c for c in REQUIRED_GLYPHS if ord(c) not in covered]
    note = "all present" if not missing else f"MISSING {''.join(missing)}"
    phase8 = "yes" if ord(PHASE8_GLYPH) in covered else "no"
    print(f"  {label}: accented {note}; U+266A (Phase 8) {phase8}")


def build() -> dict[str, bytes]:
    """Every byte of assets/fonts/, derived from upstream in one place."""
    assets: dict[str, bytes] = {}

    print("Space Grotesk — fetching the variable source")
    variable = fetch(SG_VARIABLE)
    print(f"  SpaceGrotesk[wght].ttf {len(variable)} bytes (not committed)")

    for weight, style in SG_WEIGHTS.items():
        data = instance_space_grotesk(variable, weight, style)
        assets[f"SpaceGrotesk-{style}.ttf"] = data
        print(f"  instanced wght={weight} -> {style} ({len(data)} bytes)")

    assets["SpaceGrotesk-OFL.txt"] = fetch(f"{SG_DIR}/OFL.txt")

    print("IBM Plex Mono — fetching statics as published")
    for style in PM_STATICS:
        name = f"IBMPlexMono-{style}.ttf"
        assets[name] = fetch(f"{PM_DIR}/{name}")
        print(f"  {name} ({len(assets[name])} bytes)")

    assets["IBMPlexMono-OFL.txt"] = fetch(f"{PM_DIR}/OFL.txt")

    print("Glyph coverage")
    report_glyph_coverage(assets["SpaceGrotesk-Regular.ttf"], "Space Grotesk")
    report_glyph_coverage(assets["IBMPlexMono-Regular.ttf"], "IBM Plex Mono")

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
