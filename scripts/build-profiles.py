#!/usr/bin/env python3
"""Generate every copy of the regional grooves from `assets/profiles.json`.

THE MUSIC HAS ONE HOME, and this is what makes that true rather than aspirational.
`src/Profiles.cpp` has carried the banner "GENERATED FROM data.js — do not
hand-edit a digit here" since Phase 2, and it was an instruction to humans: the
table was transcribed by hand and a cross-check compared it back. That check
had to regex-parse JavaScript, including a hand-written brace matcher added
because a non-greedy regex matched `CHANNEL_DEFAULTS` instead of `PROFILES`.

`STATE.md` has carried the fix as deferred since Phase 2. 09-01 takes it, before
09-02 multiplies the content that parser reads.

Three consumers are generated from the one JSON:

    assets/profiles.json
        -> src/Profiles.cpp                    the constexpr table the plugin builds against
        -> data.js                             the block the multi-file prototype loads
        -> Forró Box (standalone).html         the same block, inlined

The standalone page is spliced here rather than left to `build_standalone.py`,
which owns every other part of it. It is the one consumer a stale copy could
reach a user through — it is committed, it is what `docs/README-prototype.md`
tells someone to open, and nothing else compares it.

`data.js` IS NOW PARTLY GENERATED, and that is a change of status worth saying
out loud: for nine phases it was the read-only design source of truth. Only its
`PROFILES` block is generated. `INSTRUMENTS`, `CHANNEL_DEFAULTS`, `TIMBRES`,
`PRESETS` and `buildGroove` remain hand-written and remain authoritative — four
other cross-checks read them.

Modes, the same contract `scripts/build-fonts.py` established at 04-01:

    (no flag)   write the generated regions
    --verify    re-derive them and fail if what is on disk differs

`--verify` is what the build runs. A generated file that has drifted is worse
than a hand-written one, because nobody thinks to look at it.
"""
import argparse
import importlib.util
import itertools
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
PROFILES_JSON = ROOT / "assets" / "profiles.json"
PROFILES_CPP = ROOT / "src" / "Profiles.cpp"
DATA_JS = ROOT / "data.js"
PARAM_IDS_H = ROOT / "src" / "ParameterIDs.h"

# The standalone page inlines `data.js` verbatim, so the same rendering splices
# into it. Handled HERE rather than left to `build_standalone.py` because it is
# the one consumer a stale copy could reach a user through: it is committed, it
# is what `README-prototype.md` tells someone to open, and nothing else compares
# it. The bundler still owns every OTHER part of that file.
STANDALONE = ROOT / "Forró Box (standalone).html"

VERIFY_PROFILES_PY = ROOT / "scripts" / "verify-profiles.py"


def verify_profiles_module():
    """`verify-profiles.py`, loaded as a module — the same trick `verify-midi.py`
    uses on this same file, for the same reason.

    THE WRITER MUST NOT HOLD THE SECOND COPY. Four things lived here and again
    there: the timbre index, the mute whitelist, `validate` and the JSON load.
    The copy in THIS file is the dangerous one, because this file is what writes
    `Profiles.cpp` — a stale `{"hifi": 0, "lofi": 1, "ciclo": 2}` here emits a
    wrong index into the plugin, and `--verify` compares the output against this
    same stale table and passes. The checker's copy is DERIVED, from
    `MixBus.h`'s `timbreSpecs`; the generator's was hand-written. Importing it
    means the one derivation feeds the writer too. /simplify.

    Loaded through importlib because the filename is hyphenated. That module is
    constants and defs with a guarded main, so importing it runs nothing.
    """
    spec = importlib.util.spec_from_file_location("verify_profiles", VERIFY_PROFILES_PY)
    if spec is None or spec.loader is None:
        print(f"could not load {VERIFY_PROFILES_PY}", file=sys.stderr)
        sys.exit(1)

    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


_vp = verify_profiles_module()

# All four now have ONE home, in the gate that owns validity. `TIMBRE_INDEX` is
# derived from `MixBus.h`; `validate` refuses a non-`bateria` mute and reports
# what widening it would take; `load_profiles` validates on the way out, so
# neither this script nor the checker can render unvalidated data.
TIMBRE_INDEX = _vp.TIMBRE_INDEX
load = _vp.load_profiles
fail = _vp.fail


def cpp_float(value, what: str) -> str:
    """A C++ float literal, formatted rather than concatenated.

    This was `f"{p['swing']}.0f"`, which glues `.0f` onto whatever `json.load`
    returned — so a swing of `54.5`, which `Profiles.h` documents as legitimate
    (`float swing;  ///< 0..100`), generated `54.5.0f`: a syntax error written
    into a source file, and then an uncaught ValueError out of the gate that
    should have reported it. /code-review.
    """
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        fail(f"{what}: expected a number, got {value!r}")

    # `repr`, NOT `%g`. `%g` is six significant digits, so a swing of 61.53125
    # reached the plugin as 61.5312f while data.js and the standalone page got
    # 61.53125 — the plugin and the prototypes playing different numbers. Worse,
    # the gate that reports it could not be cleared: `verify-profiles` went red
    # while `--verify` said "up to date" for all four targets, so the remedy it
    # prints ("regenerate them") left the build stuck. `repr` is the shortest
    # string that round-trips. /code-review.
    text = repr(float(value))

    return f"{text}f" if "." in text or "e" in text else f"{text}.0f"


def splice(source: str, begin: str, end: str, rendered: str) -> str:
    """Replace the region between two literal markers.

    ONE helper. There were four — `splice_cpp`, `splice_js`, `splice_order` and
    a `splice_js_and_order` that existed only because `data.js` has TWO generated
    regions and the target list insisted on one render/splice pair per file. It
    joined two rendered blocks with a `"\\x00"` sentinel and split them apart
    again: a positional contract with no name on it, which a wrong count would
    have reported as a bare ValueError from inside the splitter. A target now
    owns a list of regions. /simplify.
    """
    start = source.index(begin)
    finish = source.index(end, start) + len(end)
    return source[:start] + rendered + source[finish:]


# ── ids::profileInfos ───────────────────────────────────────────────────────

INFOS_BEGIN = "inline constexpr std::array<ProfileInfo, "
INFOS_END = "}};\n"

# The committed layout puts the four identity literals at these ABSOLUTE columns.
# Not `max(len) + 2` like `render_js`: this block was hand-aligned to fixed
# columns and it is not this plan's business to re-flow a file it is only
# starting to generate. A name longer than its column pushes the rest of the row
# rather than being truncated.
INFO_COLUMNS = (6, 20, 47, 61)


def render_infos(data: dict) -> str:
    """`ids::profileInfos`, from the same JSON as everything else.

    THE CONSUMER 09-01 SKIPPED. The identity strings and all twelve description
    lines lived hand-transcribed here and were compared back by a regex parse of
    this project's own C++ — the arrangement 09-01 existed to end, left standing
    for the one field 09-02 rewrites. `src/Profiles.cpp` has carried a comment
    naming this gap since that plan closed; generating it is what removes the
    comment.

    Byte-identical to the committed array, for the reason `render_cpp` gives.
    """
    order = data["profileOrder"]
    out = [f"{INFOS_BEGIN}{len(order)}> profileInfos {{{{\n"]

    for key in order:
        p = data["profiles"][key]

        row = " " * 4 + "{ "
        for col, value in zip(INFO_COLUMNS, (p["id"], p["name"], p["short"], p["code"])):
            row = row.ljust(col) + f'"{value}",'

        out.append(row.rstrip() + "\n")

        for i, line in enumerate(p["desc"]):
            lead = " " * 6 + "{ " if i == 0 else " " * 8
            tail = "," if i + 1 < len(p["desc"]) else " } },"
            out.append(f'{lead}"{line}"{tail}\n')

    out.append(INFOS_END)
    return "".join(out)


# ── the C++ table ───────────────────────────────────────────────────────────

CPP_BEGIN = "    constexpr std::array<Profile, "
CPP_END = "    }};\n"


def render_cpp(data: dict) -> str:
    """The `kProfiles` initialiser, formatted exactly as the committed table.

    BYTE-IDENTICAL IS THE POINT. This plan's whole claim is that it moved the
    music without changing it, and the proof is that `git diff` on this file
    shows nothing. A generator that produced equivalent-but-differently-spaced
    C++ would have proved nothing and left a diff nobody can read.
    """
    order = data["profileOrder"]
    lanes = data["laneOrder"]

    out = [f"{CPP_BEGIN}{len(order)}> kProfiles {{{{\n"]

    for index, key in enumerate(order):
        p = data["profiles"][key]

        out.append("    {\n")
        out.append(f"        &ids::profileInfos[{index}],   /* {key} */\n")
        out.append(f"        {TIMBRE_INDEX[p['timbre']]}, "
                   f"{'true' if 'bateria' in p['muted'] else 'false'},\n")
        # THE BANK. Only the real entries are written; `std::array`'s remaining
        # elements are value-initialised by aggregate init, and `grooveCount`
        # is what stops anything reading them. `grooves()` is the only iterator.
        out.append(f"        {{{{   /* {len(p['grooves'])} of 8 grooves */\n")

        for g, groove in enumerate(p["grooves"]):
            out.append("          {\n")
            out.append(f'            "{groove["id"]}", "{groove["name"]}",\n')
            where = f"{key}/{groove['id']}"
            out.append(f"            {groove['bpm']}, "
                       f"{cpp_float(groove['swing'], where + '.swing')}, "
                       f"{cpp_float(groove['cachaca'], where + '.cachaca')},\n")
            out.append("            {{\n")

            for i, lane in enumerate(lanes):
                comma = "," if i + 1 < len(lanes) else ""
                out.append(f'              "{groove["patterns"][lane]}"   /* {lane} */{comma}\n')

            out.append("            }}\n")
            out.append("          }," if g + 1 < len(p["grooves"]) else "          }")
            out.append("\n")

        out.append("        }},\n")
        out.append(f"        {len(p['grooves'])},\n")
        out.append("    },\n")

    out.append(CPP_END)
    return "".join(out)



# ── data.js's PROFILES block ────────────────────────────────────────────────

JS_BEGIN = "  const PROFILES = {\n"
JS_END = "  };\n"


# Spliced INSIDE the generated region, not above it. Above the marker it would
# be ordinary hand-written text that anyone could delete while every gate stayed
# green; inside, it is part of what `--verify` compares, so removing it fails the
# build. The file is otherwise authoritative and hand-written, and nothing in it
# said which two blocks had stopped being. /simplify.
JS_NOTICE = ("    // GENERATED — this block and PROFILE_ORDER below, from\n"
             "    // assets/profiles.json, by scripts/build-profiles.py. Edit the JSON.\n"
             "    // Everything else in this file is hand-written and authoritative.\n")


# WHY THE DEFAULT GROOVE IS WRITTEN TWICE INTO data.js.
#
# `app.js` is READ-ONLY design source and it reads four things off a PROFILE
# that now live on a groove:
#
#     app.js:135      S(p.patterns[key])        inside buildGroove
#     app.js:526-528  setBPM(p.bpm), p.swing, p.cachaca   inside loadProfile
#
# Moving any of them under `grooves` breaks both prototypes on boot. So the
# generated block keeps all four at profile level, rendered from `grooves[0]`,
# and emits the full bank beside them — which the prototype ignores.
#
# This is the one place the consumers differ in SHAPE rather than only in
# syntax. 09-02 met it with `patterns` and explained it there; 09-03 met it
# again with the feel, which makes it a pattern rather than an exception, so it
# is stated once here instead of twice inline.
#
# CHECKED, not merely declared. The first version of this constant was read by
# nothing: dropping `cachaca` from the profile-level line passed all six gates —
# `--verify` compares the generator against its own output, and `verify-profiles`
# stopped reading data.js's PROFILES at 09-01 — while `app.js:528` read
# `p.cachaca` as undefined and the prototype booted with an undefined CACHAÇA
# knob. A constant that looks load-bearing and enforces nothing is worse than a
# sentence. `render_js` now asserts every name here appears at profile level.
# /code-review.
JS_PROFILE_LEVEL = ("patterns", "bpm", "swing", "cachaca")


def render_js(data: dict) -> str:
    """The `PROFILES` literal, formatted exactly as `data.js` has it.

    Same reason as the C++: the diff is the proof. The pattern keys are padded
    to the longest one so the velocity strings line up, which is how a human
    reads a groove table — and the prototype is still something a human opens.
    """
    lanes = data["laneOrder"]
    width = max(len(lane) for lane in lanes) + 2   # `name:` plus one space

    out = [JS_BEGIN, JS_NOTICE]

    for key in data["profileOrder"]:
        p = data["profiles"][key]

        # grooves[0] IS the profile, for every field the prototype reads at
        # profile level. See JS_PROFILE_LEVEL above.
        default = p["grooves"][0]

        profile_start = len(out)
        out.append(f"    {key}: {{\n")
        out.append(f'      id: "{p["id"]}", name: "{p["name"]}", '
                   f'short: "{p["short"]}", code: "{p["code"]}",\n')

        for i, line in enumerate(p["desc"]):
            lead = "      desc: [" if i == 0 else " " * 13
            tail = "," if i + 1 < len(p["desc"]) else "],"
            out.append(f'{lead}"{line}"{tail}\n')

        out.append(f'      bpm: {default["bpm"]}, swing: {default["swing"]}, '
                   f'cachaca: {default["cachaca"]}, timbre: "{p["timbre"]}",\n')

        muted = ", ".join(f"{name}: true" for name in p["muted"])
        out.append(f"      muted: {{ {muted} }},\n" if muted else "      muted: {},\n")

        # See JS_PROFILE_LEVEL above.
        out.append("      patterns: {\n")
        for lane in lanes:
            out.append(f'        {(lane + ":").ljust(width)}"{default["patterns"][lane]}",\n')
        out.append("      },\n")

        # Every field app.js reads off a profile must BE there. Checked against
        # the rendered text rather than against the code that wrote it, so a
        # deleted line fails here instead of six gates later.
        rendered_profile = "".join(out[profile_start:])

        missing = [name for name in JS_PROFILE_LEVEL
                   if f"\n      {name}:" not in rendered_profile
                   and f" {name}:" not in rendered_profile]
        if missing:
            fail(f"{key}: the generated PROFILES entry is missing {missing} at profile "
                 f"level — app.js reads those off a profile and is read-only, so the "
                 f"prototype would boot with them undefined")

        out.append("      grooves: [\n")
        for groove in p["grooves"]:
            out.append(f'        {{ id: "{groove["id"]}", name: "{groove["name"]}",\n')
            out.append(f'          bpm: {groove["bpm"]}, swing: {groove["swing"]}, '
                       f'cachaca: {groove["cachaca"]},\n')
            out.append("          patterns: {\n")
            for lane in lanes:
                out.append(f'            {(lane + ":").ljust(width)}"{groove["patterns"][lane]}",\n')
            out.append("          } },\n")
        out.append("      ],\n")
        out.append("    },\n")

    out.append(JS_END)
    return "".join(out)



ORDER_BEGIN = "  const PROFILE_ORDER = ["
ORDER_END = "];\n"


def render_order(data: dict) -> str:
    """`PROFILE_ORDER`, which decides what either prototype can actually reach.

    GENERATED, because leaving it hand-written left a hole that 09-02 walks
    straight into. `app.js:111` and `:279` iterate this list, not `PROFILES` —
    so a fifth profile added to the JSON appeared in both prototypes' data and
    in neither prototype's UI, with every gate exiting 0. A RENAME was worse:
    `PROFILES` took the new key, this kept the old, and `loadProfile`'s
    `PROFILES[id]` came back undefined — the page throws on boot.

    The old `read_data_js` asserted the two agreed. Reading JSON instead dropped
    the assertion along with the parser. /code-review.
    """
    names = ", ".join(f'"{key}"' for key in data["profileOrder"])
    return f"{ORDER_BEGIN}{names}{ORDER_END}"





# ── driver ──────────────────────────────────────────────────────────────────

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify", action="store_true",
                        help="re-derive and fail on any difference; write nothing")
    args = parser.parse_args()

    data = load()

    # Each target names the regions it owns. `data.js` and the standalone page
    # have two apiece — the grooves and the order they are offered in.
    js_regions = [(JS_BEGIN, JS_END, render_js), (ORDER_BEGIN, ORDER_END, render_order)]

    targets = [
        (PROFILES_CPP, [(CPP_BEGIN, CPP_END, render_cpp)],
         "the constexpr table the plugin builds against"),
        (PARAM_IDS_H, [(INFOS_BEGIN, INFOS_END, render_infos)],
         "ids::profileInfos — the identity strings and the side panel's three lines"),
        (DATA_JS, js_regions,
         "the PROFILES block and PROFILE_ORDER the multi-file prototype loads"),
        (STANDALONE, js_regions,
         "the same two blocks, inlined in the standalone page"),
    ]

    stale = []

    for path, regions, what in targets:
        current = path.read_text(encoding="utf-8")
        wanted = current

        for begin, end, render in regions:
            wanted = splice(wanted, begin, end, render(data))

        if current == wanted:
            print(f"  up to date   {path.relative_to(ROOT)}  — {what}")
            continue

        if args.verify:
            stale.append((path, current, wanted))
            continue

        path.write_text(wanted, encoding="utf-8")
        print(f"  WRITTEN      {path.relative_to(ROOT)}  — {what}")

    if stale:
        print("\nGenerated files do not match assets/profiles.json:", file=sys.stderr)

        for path, current, wanted in stale:
            print(f"\n  {path.relative_to(ROOT)}", file=sys.stderr)

            # NAME THE LINES. "A file differs" sends a reader to a diff tool;
            # the first differing line usually names the profile and the lane.
            # `zip_longest`, not `zip`: if the only difference is content the
            # regeneration ADDS past the end of the file on disk, `zip` stops at
            # the shorter one, finds no differing line and prints a filename
            # with no detail. /code-review.
            for n, (a, b) in enumerate(itertools.zip_longest(current.splitlines(),
                                                             wanted.splitlines(),
                                                             fillvalue="<end of file>"),
                                       start=1):
                if a != b:
                    print(f"    line {n}:", file=sys.stderr)
                    print(f"      on disk:  {a.strip()}", file=sys.stderr)
                    print(f"      expected: {b.strip()}", file=sys.stderr)
                    break

        print("\nRun `python3 scripts/build-profiles.py` to regenerate them.",
              file=sys.stderr)

        if any(path == STANDALONE for path, _, _ in stale):
            # NAMED SEPARATELY, because regenerating only the groove blocks
            # would leave the rest of that page stale against its own sources
            # and then turn this gate green — hiding the staleness behind a
            # passing check. `build_standalone.py` rebuilds the whole file.
            print("The standalone page also inlines the CSS, audio.js, "
                  "controls.js and app.js. If any of THOSE moved, run "
                  "`python3 build_standalone.py` instead — this script only "
                  "splices the groove blocks.", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
