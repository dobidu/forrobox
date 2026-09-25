#!/usr/bin/env python3
"""Prove the C++ groove tables still match `assets/profiles.json`, their source.

Why this exists: a transcription error in the musical content produces no crash,
no failed build and no failing test — only a groove that is subtly wrong, with no
way to tell which digit. A unit test that embedded the expected patterns by hand
would just duplicate the same typo risk. Comparing against the source is the only
check here with real signal.

The source MOVED at 09-01, from `data.js` to `assets/profiles.json`, and the C++
is now generated from it — so this script and `build-profiles.py --verify` ask
two different questions. That one asks whether the generated files match their
source.

THIS ONE OWNS THREE THINGS THE GENERATOR DOES NOT GENERATE, and they are the
reason it survives rather than the "a generator could emit something
self-consistent and wrong" line this docstring used to lead with. That defence
was the thinnest available, and a later reader could fairly have called the gate
redundant and deleted it. /simplify.

  1. THE PROSE AGAINST THE DATA. `ids::profileInfos` stopped being hand-written
     at 09-02 — `build-profiles.py` generates it — so this script no longer
     guards a transcription there. What it guards instead is that each
     profile's three DESCRIPTION lines agree with its own numbers, which no
     generator can check: generating the prose from the JSON makes the text
     consistent with the JSON, not TRUE of it. See `check_descriptions`.
  2. THE LANE MAPPING. `build-profiles.py` writes the eight patterns into the
     C++ table by POSITION, from `profiles.json`'s `laneOrder`. `read_lane_order`
     below reads `ids::lanes` out of `ParameterIDs.h` and maps the C++ back by
     NAME. A reorder of either would otherwise move every pattern into the wrong
     lane silently.
  3. `timbreSpecs` against `data.js`'s TIMBRES — entirely outside the
     generator's remit.

It also checks something no script did before: each profile's own DESCRIPTION
against its own data. See `check_descriptions`.

Exit 0 when every pattern, scalar and claim matches; exit 1 naming the profile
and the field that diverged.
"""
from __future__ import annotations

import json
import pathlib
import re
import struct
import sys

import gate_inputs

ROOT = pathlib.Path(__file__).resolve().parent.parent
PROFILES_JSON = ROOT / "assets" / "profiles.json"

# Still read, and only for the TIMBRES table: `check_timbres` compares
# `timbreSpecs` against it. The grooves left this file at 09-01; the timbres,
# the instruments, the channel defaults and the preset labels did not.
DATA_JS = ROOT / "data.js"
PROFILES_CPP = ROOT / "src" / "Profiles.cpp"
PARAM_IDS_H = ROOT / "src" / "ParameterIDs.h"
PROFILES_H = ROOT / "src" / "Profiles.h"
MIXBUS_H = ROOT / "src" / "MixBus.h"


def read_timbre_index() -> dict[str, int]:
    """`{cssId: choice index}`, out of `timbreSpecs` — never a hand copy.

    This WAS `{"hifi": 0, "lofi": 1, "ciclo": 2}`, written here and again in
    `build-profiles.py`. The second copy was the dangerous one: it is what
    `render_cpp` writes into `Profiles.cpp`, and nothing pinned it. Reorder
    `timbreSpecs` and the generator emits a stale index, `--verify` passes
    because it re-derives the same wrong number, and the intended remedy —
    regenerate — writes it again.

    `MixBus.h:86` IS the definition: position is the index and `cssId` is the
    id, which is what `ciclotronTimbreIndex()` states in C++ one function down.
    `read_lane_order` above refuses a hand copy of `ids::lanes` for exactly this
    reason and says so; this is the same rule, applied to the table next to it.
    /simplify.
    """
    src = MIXBUS_H.read_text(encoding="utf-8")
    body = match_braces(src, src.index("{", src.index("timbreSpecs")))

    ids = [m.group(1) for m in re.finditer(r'\{\s*"(\w+)"\s*,', body)]

    if not ids:
        fail("timbreSpecs parsed empty — could not read the timbre ids out of MixBus.h")

    return {name: index for index, name in enumerate(ids)}


def read_constant(name: str, where: pathlib.Path = None, kind: str = "int"):
    """An `inline constexpr` scalar out of a header — never a digit typed here.

    Five constants come this way now: `kPatternLength` and
    `kMaxGroovesPerProfile` from `Profiles.h`, and `kMinBpm`, `kMaxBpm` and
    `kPercentMax` from `ParameterIDs.h`. Same rule `read_lane_order` and
    `read_timbre_index` follow: the C++ owns the number, this file reads it.

    THE BPM AND PERCENT BOUNDS MATTER MORE THAN THE OTHERS, because they are the
    PARAMETER ranges. 09-06 writes a groove's feel into `ids::bpm`, `ids::swing`
    and `ids::cachaca`; a value outside them is silently clamped on load, so a
    groove would play at a tempo its own source does not state.
    """
    where = where or PROFILES_H
    pattern = (rf"inline constexpr int\s+{name}\s*=\s*(-?\d+);" if kind == "int"
               else rf"inline constexpr float\s+{name}\s*=\s*(-?[\d.]+)f?;")
    m = re.search(pattern, where.read_text(encoding="utf-8"))

    if m is None:
        fail(f"could not find {kind} {name} in {where.name}")

    return int(m.group(1)) if kind == "int" else float(m.group(1))


def read_pattern_length() -> int:
    """`kPatternLength`, out of `Profiles.h` — never the digit 16 typed here.

    `decodePattern` REFUSES a pattern of any other length (Profiles.h:58, and
    deliberately, rather than padding), so this is the length a groove must be
    to reach the plugin at all. Written as a literal it was a fourth copy of a
    number the C++ already owns, in the file whose whole job is to not
    transcribe. /simplify.
    """
    return read_constant("kPatternLength")


def read_lane_order(src: str) -> list[str]:
    """Lane order comes from ids::lanes, never a copy of it.

    Profiles.cpp stores patterns POSITIONALLY; the only thing linking position
    to lane name is that array. Hand-copying it here would key both sides of the
    comparison off the same stale list, so reordering ids::lanes would silently
    move every pattern into the wrong lane while this script still reported OK.
    """
    # match_braces, not a hand-rolled [^}]* — this file's own helper exists
    # because a naive brace capture "silently captures the wrong block", and
    # using it inconsistently is how the wrong-block bug got in here once already.
    # Anchored on the DECLARATION, not the word: "lanes" also appears in the
    # doc comment above the array, so searching for the bare word and taking the
    # next "{" picked up whatever declaration happened to sit between the two.
    # Adding an unrelated array there made this parse empty.
    m = re.search(r"\blanes\s*\{", src)
    if not m:
        fail("could not find the ids::lanes declaration in ParameterIDs.h")
    body = match_braces(src, src.index("{", m.start()))
    lanes = re.findall(r'"(\w+)"', body)
    if not lanes:
        fail("ids::lanes parsed empty")
    return lanes



def fail(msg: str) -> None:
    print(f"FAIL: {msg}", file=sys.stderr)
    sys.exit(1)


def brace_span(text: str, open_at: int) -> tuple[str, int]:
    """`(body, index just past the matching close brace)`.

    Brace matching, not a non-greedy regex: `data.js` has other 4-space-indented
    objects (CHANNEL_DEFAULTS among them) and a lazy match silently captures the
    wrong block. 09-02 needed the END position too — the groove bank is followed
    by `grooveCount`, and a regex cannot find it without knowing where the bank
    stopped — so the walker returns both and `match_braces` keeps the old name
    for the four callers that only want the body.
    """
    depth = 0
    for i in range(open_at, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[open_at + 1 : i], i + 1
    fail("unbalanced braces")
    return "", 0


def match_braces(text: str, open_at: int) -> str:
    return brace_span(text, open_at)[0]


def top_level_entries(body: str):
    """Each `{...}` child of `body` that is not nested inside another.

    The groove bank is a list of braced entries, each of which contains its own
    braced pattern array — so "split on braces" and "non-greedy regex" both find
    the inner one. This walks depth instead.
    """
    depth = 0
    start = -1

    for i, ch in enumerate(body):
        if ch == "{":
            if depth == 0:
                start = i
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                yield body[start + 1 : i]


# Derived once, at import. `verify-midi.py` imports this module and
# `build-profiles.py` now does too, so all three read one answer.
TIMBRE_INDEX = read_timbre_index()
PATTERN_LENGTH = read_pattern_length()
MAX_GROOVES = read_constant("kMaxGroovesPerProfile")

# The PARAMETER ranges a groove's feel must fit, read from where the parameters
# themselves are declared. See `read_constant`.
MIN_BPM = read_constant("kMinBpm", PARAM_IDS_H)
MAX_BPM = read_constant("kMaxBpm", PARAM_IDS_H)
MAX_PERCENT = read_constant("kPercentMax", PARAM_IDS_H, "float")

# The only mute a `Profile` can carry: `Profiles.h` gives it one
# `bool bateriaMuted`. See `validate`.
CPP_MUTABLE = {"bateria"}


def validate(data: dict) -> None:
    """Refuse data that cannot survive the journey to every consumer.

    OWNED BY THE READER, so every caller gets it. It lived in
    `build-profiles.py` for one revision, which meant `verify-profiles.py` run
    on its own accepted data the project had decided was invalid — and the
    lesson had already been recorded one file over: `verify-midi.py`'s reader
    docstring says "through verify-profiles.py's OWN reader, not a second one",
    because a private parser there mislabelled campina's patterns. /simplify.

    THE MUTE IS THE ONE THAT MATTERS. `render_cpp` used to project `muted` to
    `"bateria" in muted`, and this script computed the identical projection for
    its expected value — so the lossy step sat on BOTH sides of the comparison
    and could never be caught. `render_js` meanwhile emits the whole object and
    `app.js:533` honours a mute on any of the five instruments. A
    `muted: ["pandeiro"]` therefore silenced the lane in both prototypes, played
    it in the plugin, and exited 0 everywhere. /code-review.

    Refused rather than supported — and the price of supporting it is smaller
    than the first version of this comment claimed. It said `applyProfile` and
    "what a profile MEANS, which is Phase 3's to make". `applyProfile` does not
    touch mutes at all: `PluginProcessor.cpp:250-258` does, and it already loops
    `ids::channelInfos` computing `profile.bateriaMuted && id == "bateria"`. The
    real cost is one field (`bateriaMuted` becomes a five-lane mask), three lines
    there, one in `render_cpp` and a handful of test sites. A small change, not a
    phase — and `app.js:531-536` already supports the general case. Deferred on
    its true price rather than an inflated one, to whichever plan first wants a
    profile that silences something else. /simplify.
    """
    order = data["profileOrder"]
    profiles = data["profiles"]

    missing = [k for k in order if k not in profiles]
    if missing:
        fail(f"profileOrder names {missing}, which are not in profiles")

    extra = [k for k in profiles if k not in order]
    if extra:
        fail(f"profiles has {extra}, which profileOrder does not name — "
             f"they would reach neither prototype")

    if len(set(order)) != len(order):
        duplicated = sorted({k for k in order if order.count(k) > 1})
        fail(f"profileOrder repeats {duplicated}")

    # The lane list the generator writes the C++ table POSITIONALLY from.
    # `read_lane_order`'s own docstring refuses a hand copy of this; the JSON
    # was one until it was compared here. /simplify.
    lanes = read_lane_order(PARAM_IDS_H.read_text(encoding="utf-8"))

    if data["laneOrder"] != lanes:
        fail(f"laneOrder is {data['laneOrder']} but ids::lanes is {lanes} — the "
             f"generator writes patterns into the C++ table by POSITION")

    for key in order:
        p = profiles[key]

        if p["id"] != key:
            fail(f"{key}: id is {p['id']!r}")

        if p["timbre"] not in TIMBRE_INDEX:
            fail(f"{key}: unknown timbre {p['timbre']!r}, expected one of "
                 f"{sorted(TIMBRE_INDEX)}")

        unsupported = sorted(set(p["muted"]) - CPP_MUTABLE)
        if unsupported:
            fail(f"{key}: muted {unsupported} — a Profile carries only "
                 f"`bateriaMuted`, so this would silence the lane in both "
                 f"prototypes and play it in the plugin, with every gate green. "
                 f"Supporting it means widening Profiles.h and applyProfile.")

        if len(p["desc"]) != 3:
            fail(f"{key}: desc has {len(p['desc'])} lines, expected 3")

        # Refused HERE rather than reaching `check_descriptions` as a KeyError
        # out of CLAIM_RULES. A typo in a declared phrase is a claim that silently
        # checks nothing, which is the failure that function exists against.
        unknown = [c for c in p["descClaims"] if c not in CLAIM_RULES]
        if unknown:
            fail(f"{key}: descClaims names {unknown}, which CLAIM_RULES cannot evaluate — "
                 f"known claims are {sorted(CLAIM_RULES)}")

        # THE FEEL LIVES ON THE GROOVE SINCE 09-03, and a leftover copy up here
        # is read by nothing — `render_cpp`, `render_js` and `read_profiles_json`
        # all take it from `grooves[0]`. In the file that is THE source, a key
        # that looks authoritative and is inert is exactly what someone edits
        # when they mean to change the tempo. /code-review.
        stale = [f for f in ("bpm", "swing", "cachaca") if f in p]
        if stale:
            fail(f"{key}: {stale} at profile level, where nothing reads them — 09-03 moved "
                 f"the feel onto each groove. Edit grooves[0] instead")

        # ── the bank ────────────────────────────────────────────────────
        grooves = p["grooves"]

        if not grooves:
            fail(f"{key}: an empty grooves bank — grooves[0] is what selecting "
                 f"the profile loads, so a profile must carry at least one")

        if len(grooves) > MAX_GROOVES:
            fail(f"{key}: {len(grooves)} grooves, and kMaxGroovesPerProfile is "
                 f"{MAX_GROOVES} — a groove past the end of the bank would not fit "
                 f"the C++ table, and no cycler could select it")

        for kind in ("id", "name"):
            seen = [g[kind] for g in grooves]
            duplicated = sorted({g for g in seen if seen.count(g) > 1})
            if duplicated:
                fail(f"{key}: grooves repeat the {kind} {duplicated} — an id identifies a "
                     f"groove in saved state and a name is all the cycler shows, so two of "
                     f"either make one of them unreachable or indistinguishable")

        # Two grooves that differ only in their labels are one groove offered
        # twice. 09-03 fills these banks by copy-and-edit, which is exactly the
        # shape of mistake that leaves the edit out. /code-review.
        fingerprints: dict[tuple, str] = {}
        for groove in grooves:
            key_ = tuple(groove["patterns"][lane] for lane in lanes)
            if key_ in fingerprints:
                fail(f"{key}: grooves {fingerprints[key_]!r} and {groove['id']!r} have "
                     f"identical patterns in every lane — the cycler would offer the same "
                     f"groove twice under two names")
            fingerprints[key_] = groove["id"]

        for groove in grooves:
            # `Profiles.h` documents the id as "stable, lowercase-kebab; a saved
            # state may hold it", and a documented contract nothing enforces is
            # the kind this project keeps finding. 09-05 writes this string into
            # saved plugin state.
            if not re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", groove["id"]):
                fail(f"{key}: groove id {groove['id']!r} is not lowercase-kebab — it goes "
                     f"into saved plugin state at 09-05 and into two generated languages now")

            # REFUSED rather than escaped, because no groove name needs either and
            # the quiet failure is the dangerous one: a backslash makes `render_cpp`
            # emit an escape the COMPILER consumes, while the reader's regex returns
            # the raw source text — so the shipped string differs from its source
            # with every gate green. A quote is merely loud. /code-review.
            for field in ("id", "name"):
                bad = [c for c in groove[field] if c in '"\\']
                if bad:
                    fail(f"{key}/{groove['id']}: {field} contains {bad} — a quote or backslash "
                         f"is written unescaped into C++ and JavaScript string literals")

        for groove in grooves:
            for lane in lanes:
                if lane not in groove["patterns"]:
                    fail(f"{key}/{groove['id']}: no pattern for lane {lane!r}")

            # Bounded by the PARAMETER ranges rather than by taste: 09-06 writes
            # these into ids::bpm/swing/cachaca, where an out-of-range value is
            # clamped without complaint — so the groove would play at a tempo
            # its own source does not state.
            # AN INT, refused here rather than narrowed by the compiler.
            # `Groove::bpm` is `int`, and `render_cpp` interpolates it raw — so
            # `"bpm": 96.5` passed the range check, wrote `96.5` into an int
            # field, stopped `Profiles.cpp` compiling, and made THIS script
            # report "could not read bpm/swing/cachaca", pointing the reader at
            # the C++ parser instead of at the fractional digit. /code-review.
            if isinstance(groove["bpm"], bool) or not isinstance(groove["bpm"], int):
                fail(f"{key}/{groove['id']}: bpm {groove['bpm']!r} is not a whole number — "
                     f"Groove::bpm is an int, and a fraction here narrows in a constexpr "
                     f"aggregate and stops Profiles.cpp compiling")

            if not MIN_BPM <= groove["bpm"] <= MAX_BPM:
                fail(f"{key}/{groove['id']}: bpm {groove['bpm']} is outside "
                     f"ids::kMinBpm..kMaxBpm ({MIN_BPM}..{MAX_BPM}) and would be clamped "
                     f"on load")

            for field in ("swing", "cachaca"):
                value = groove[field]

                if isinstance(value, bool) or not isinstance(value, (int, float)):
                    fail(f"{key}/{groove['id']}: {field} {value!r} is not a number")

                if not 0 <= value <= MAX_PERCENT:
                    fail(f"{key}/{groove['id']}: {field} {value} is outside "
                         f"0..ids::kPercentMax ({MAX_PERCENT:g}) and would be clamped on load")

                # IT MUST SURVIVE THE TRIP TO A C++ FLOAT. `data.js` gets a
                # double and `Profiles.cpp` gets a `float`, so a value the two
                # cannot both hold exactly makes the plugin and the prototypes
                # play different numbers. 61.53125 is fine; 61.5312501 is not.
                if struct.unpack("f", struct.pack("f", float(value)))[0] != float(value):
                    fail(f"{key}/{groove['id']}: {field} {value!r} is not exactly "
                         f"representable as a C++ float, so the plugin and the prototypes "
                         f"would play different values — round it")


def load_profiles() -> dict:
    """The JSON as written, validated. The shape `build-profiles.py` renders from."""
    with PROFILES_JSON.open(encoding="utf-8") as f:
        data = json.load(f)

    validate(data)
    return data


def read_profiles_json() -> tuple[list[str], dict]:
    r"""The grooves, out of `assets/profiles.json` — with a JSON parser.

    THIS USED TO PARSE JAVASCRIPT. It brace-matched `const PROFILES = {`, walked
    its members with `^    (\w+):\s*\{`, and pulled every field back out with a
    regex per field — about eighty lines whose only job was that the music lived
    in a `.js`. `match_braces` below was written for it after a non-greedy regex
    matched `CHANNEL_DEFAULTS` instead.

    09-01 moved the music to JSON and this is what that bought. `match_braces`
    STAYS: four other readers use it, and all four parse C++ — which is the
    thing this script checks, not the thing it checks against.

    The shape returned is unchanged, so everything downstream is untouched.
    """
    data = load_profiles()

    order = data["profileOrder"]
    profiles: dict = {}

    for key in order:
        p = data["profiles"][key]

        grooves = [{"id": g["id"], "name": g["name"],
                    "feel": {"bpm": float(g["bpm"]),
                             "swing": float(g["swing"]),
                             "cachaca": float(g["cachaca"])},
                    "patterns": dict(g["patterns"])}
                   for g in p["grooves"]]

        profiles[key] = {
            "grooves": grooves,
            # grooves[0] by name, so the scalar/field comparison and the shape
            # checks can speak about "the profile's own groove" without every
            # one of them indexing the bank.
            "patterns": grooves[0]["patterns"],
            # Floats because the C++ side reads `38.0f` out of source text and
            # the two are compared exactly — a tolerance would hide a wrong digit.
            # The PROFILE's scalars are its default groove's, which is what
            # `Profile::bpm()` returns in C++. Not a copy of a separate field —
            # 09-03 deleted that field precisely so there is nothing to disagree.
            "scalars": {
                **grooves[0]["feel"],
                "timbre":  float(TIMBRE_INDEX[p["timbre"]]),
                "muted":   float("bateria" in p["muted"]),
            },
            "identity": {
                "code":        p["code"],
                "displayName": p["name"],
                "shortName":   p["short"],
                # Joined so ONE field compares the whole block — three separate
                # fields would let a line go missing and still compare two.
                "description": "|".join(p["desc"]),
            },
            # What the prose above DECLARES it asserts. See `check_descriptions`.
            "descClaims": list(p["descClaims"]),
        }

    return order, profiles


def join_literals(text: str) -> str:
    """Adjacent C string literals, concatenated the way the compiler does.

    The accented text in `src/` is written as the characters it means, with the
    charset pinned in CMakeLists.txt, so this file and the compiler now read the
    SAME bytes and there is nothing to decode. The join survives the escapes
    that made it necessary: adjacent literals are still legal C++, and a future
    line-length split must not silently make this compare half a word.
    """
    return "".join(re.findall(r'"([^"]*)"', text))


def read_profile_infos(src: str) -> list[dict]:
    """ids::profileInfos — the identity table Profile now points into.

    Also via match_braces: the previous lazy `.*?` capture was the exact
    anti-pattern this file's helper was written to rule out."""
    # Anchored on the DECLARATION, not on the first mention of the name.
    #
    # `src.index("profileInfos")` found whichever came first, and 04-05 added a
    # doc comment above the table that cites it by name — so the brace matcher
    # started inside a comment and reported that profileInfos[0] did not exist.
    # A cross-check that a comment can break is not a cross-check.
    declaration = re.search(r"\bstd::array\s*<[^>]*>\s*profileInfos\b", src)

    if declaration is None:
        fail("could not find the ids::profileInfos declaration in ParameterIDs.h")

    body = match_braces(src, src.index("{", declaration.end()))

    infos = []
    # The four identity strings, then the braced three-line description. The
    # description block is NOT optional in this pattern: 06-02 added it, and a
    # regex that tolerated its absence would silently stop comparing it the day
    # someone deleted one.
    lit = r'(?:"[^"]*"\s*)+'
    pattern = (rf'\{{\s*({lit}),\s*({lit}),\s*({lit}),\s*({lit}),'
               rf'\s*\{{\s*({lit}),\s*({lit}),\s*({lit})\}}\s*\}}')

    for row in re.finditer(pattern, body):
        fields = [join_literals(row.group(i)) for i in range(1, 8)]

        infos.append({
            "id": fields[0],
            "displayName": fields[1],
            "shortName": fields[2],
            "code": fields[3],
            "description": "|".join(fields[4:7]),
        })
    return infos


def read_profiles_cpp(lanes: list[str], infos: list[dict]) -> tuple[list[str], dict]:
    src = PROFILES_CPP.read_text(encoding="utf-8")

    # THE DECLARATION, not the first mention of the name. `read_lane_order` and
    # `read_profile_infos` were both rewritten this way after a doc comment
    # mentioning their symbol broke them — lines 56-59 and 176-181 record both.
    # This one was left on `src.index("kProfiles")`, and 09-01 then put a
    # twelve-line banner directly above the table: one brace in a future
    # sentence about `kProfiles` and `match_braces` starts inside the comment,
    # parses zero entries, and the build fails over prose. Third instance of a
    # class this file has already fixed twice. /code-review.
    declaration = re.search(r"constexpr\s+std::array<Profile,\s*\d+>\s+kProfiles\s*\{", src)

    if declaration is None:
        fail("could not find the `kProfiles` declaration in Profiles.cpp")

    table = match_braces(src, src.index("{", declaration.start()))

    order: list[str] = []
    profiles: dict = {}

    # Each entry: "id", "display", "short", "code", bpm, swing f, cachaca f,
    # timbre, muted, {{ eight pattern strings }}
    # (?:\s|/\*.*?\*/|//[^\n]*\n)* between fields: a harmless trailing comment
    # used to drop a whole entry, which the order check caught loudly but only
    # after failing the build over a comment.
    sep = r"(?:\s|/\*.*?\*/|//[^\n]*\n)*"

    # THE HEADER ONLY, up to the last scalar. The bank that follows is read by
    # brace matching, not by this regex: a groove contains its own `{{ }}`
    # pattern array, so the old `\{\{(?P<pats>.*?)\}\}` tail stopped at the
    # FIRST inner `}}` and parsed one lane of one groove as the whole profile.
    header_re = re.compile(
        r"\{" + sep + r"&ids::profileInfos\[(?P<idx>\d+)\]" + sep + r","
        + sep + r"(?P<timbre>\d+)\s*,\s*(?P<muted>true|false)",
        re.S,
    )

    for m in header_re.finditer(table):
        idx = int(m.group("idx"))
        if idx >= len(infos):
            fail(f"Profile entry references ids::profileInfos[{idx}], which does not exist")
        info = infos[idx]
        pid = info["id"]

        # TWO BRACES, and the first version of this read only one. The member is
        # `std::array<Groove, N>`, so `{{` is the array's brace plus the inner
        # C-array's, and brace-matching the outer one yields a body that IS the
        # inner brace — whose single top-level entry is the whole groove list.
        # A two-groove bank then parsed as one groove with twenty string
        # literals. Caught by checking that an UNMUTATED two-groove bank passes
        # before trusting that a mutated one fails.
        bank_open = table.index("{", m.end())
        outer, after = brace_span(table, bank_open)
        bank, _ = brace_span(outer, outer.index("{"))

        grooves = []

        for entry in top_level_entries(bank):
            literals = re.findall(r'"((?:[^"\\]|\\.)*)"', entry)

            if len(literals) != len(lanes) + 2:
                fail(f"{pid}: a groove has {len(literals)} string literals, expected "
                     f"{len(lanes) + 2} (id, name and {len(lanes)} patterns)")

            feel_m = re.search(r'"\s*,\s*(\d+)\s*,\s*([\d.]+)f\s*,\s*([\d.]+)f\s*,', entry)

            if feel_m is None:
                fail(f"{pid}/{literals[0]}: could not read bpm/swing/cachaca out of the groove")

            grooves.append({
                "id": literals[0],
                "name": literals[1],
                "feel": {"bpm": float(feel_m.group(1)),
                         "swing": float(feel_m.group(2)),
                         "cachaca": float(feel_m.group(3))},
                "patterns": dict(zip(lanes, literals[2:])),
            })

        # `grooveCount` is what stops anything reading the value-initialised
        # tail of the array, so a count that disagrees with the entries written
        # beside it is a profile whose last groove is unreachable or whose first
        # unwritten one is read as a groove. Checked here, where both are known.
        count_m = re.match(r"\s*,\s*(\d+)\s*,", table[after:])

        if count_m is None:
            fail(f"{pid}: no grooveCount after the bank in Profiles.cpp")

        if int(count_m.group(1)) != len(grooves):
            fail(f"{pid}: grooveCount is {count_m.group(1)} but the bank holds "
                 f"{len(grooves)} grooves")

        if not grooves:
            fail(f"{pid}: an empty grooves bank in Profiles.cpp")

        order.append(pid)
        profiles[pid] = {
            "grooves": grooves,
            "patterns": grooves[0]["patterns"],
            "scalars": {
                **grooves[0]["feel"],
                "timbre": float(m.group("timbre")),
                "muted": float(m.group("muted") == "true"),
            },
            "identity": {k: info[k]
                         for k in ("displayName", "shortName", "code", "description")},
        }

    return order, profiles


def check_timbres(js: str, problems: list[str]) -> int:
    """The timbre names and sub-labels, against data.js's TIMBRES table.

    `timbreSpecs` carries the cutoff and drive the bus renders with; 06-02 added
    the sub-label the side panel shows. Both come from data.js and neither was
    compared against it before.

    THE TRADEMARK IS NO LONGER STRIPPED, and 08-05 is why. This used to compare
    the name with the `(tm)` removed, because data.js said CICLOTRON(tm) and the
    C++ said CICLOTRON — `PluginProcessor.cpp` had scheduled the trademark on
    the parameter's choice string for Phase 8, "alongside the visual treatment
    it belongs with", and stripping exactly that one character still caught
    every OTHER divergence. That plan has landed. All three fields are compared
    verbatim now, and dropping the trademark fails here.

    AND THE ID IS COMPARED TOO. It was the key this function looked rows UP by
    and never a value it checked, so `timbreSpecs` could have named a row
    anything. 08-05 gave the table its `cssId` — the field `app.js:294` switches
    on to decide which row gets the Ciclotron treatment — which makes it a value
    worth pinning rather than a convenience.
    """
    cpp = MIXBUS_H.read_text(encoding="utf-8")

    table = js[js.index("const TIMBRES"):]
    expected = {}

    for row in re.finditer(r'id:\s*"(\w+)",\s*name:\s*"([^"]*)",\s*sub:\s*"([^"]*)"', table):
        expected[row.group(1)] = (row.group(2), row.group(3))

    if len(expected) != len(TIMBRE_INDEX):
        fail(f"data.js TIMBRES has {len(expected)} entries, expected {len(TIMBRE_INDEX)}")

    body = match_braces(cpp, cpp.index("{", cpp.index("timbreSpecs")))
    # Through `join_literals`, like the profile reader. This stopped at the first
    # closing quote — the very thing that helper was added to prevent.
    # `timbreSpecs` is a table of accented literals, so a split literal would
    # have made this compare half a word and stay GREEN. /simplify.
    #
    # Named rather than located: the comment used to say "135 lines above", which
    # was already 130 by this diff and would keep rotting.
    lit = r'(?:"[^"]*"\s*)+'
    actual = [(join_literals(m.group(1)), join_literals(m.group(2)), join_literals(m.group(3)))
              for m in re.finditer(rf'\{{\s*({lit}),\s*({lit}),\s*({lit}),', body)]

    if len(actual) != len(TIMBRE_INDEX):
        fail(f"timbreSpecs has {len(actual)} rows, expected {len(TIMBRE_INDEX)}")

    compared = 0

    for timbre_id, index in TIMBRE_INDEX.items():
        want_name, want_sub = expected[timbre_id]
        got_id, got_name, got_sub = actual[index]

        if got_id != timbre_id:
            problems.append(f"timbre[{index}].cssId: data.js {timbre_id!r} vs C++ {got_id!r}")

        if got_name != want_name:
            problems.append(f"timbre[{index}].displayName: data.js {want_name!r} "
                            f"vs C++ {got_name!r}")

        if got_sub != want_sub:
            problems.append(f"timbre[{index}].subLabel: data.js {want_sub!r} vs C++ {got_sub!r}")

        compared += 3

    return compared


def check_pattern_shape(order: list[str], lanes: list[str], profiles: dict,
                        problems: list[str]) -> int:
    """The structural properties every groove must have, whatever it sounds like.

    `decodePattern` already refuses a malformed string at runtime, but it refuses
    it in the PLUGIN — after a build, on a machine, when someone plays that
    profile. This is the same rule applied to the data at the point the data
    changes, which is where 09-02 will be changing it.

    NOT "no lane is silent", which was this check's first draft and would have
    fired on correct data: UNIVERSITÁRIO's `tom` is deliberately empty and its
    bateria is not muted. What IS true of every groove is that the ANCHOR lane
    carries the pulse — `data.js:19` marks zabumba `anchor: true`, and a forró
    groove without one is not a forró groove. That is a claim about the music
    worth pinning; "every lane has a hit" is not.
    """
    checked = 0
    anchor = lanes[0]

    for pid in order:
        for groove in profiles[pid]["grooves"]:
            where = f"{pid}/{groove['id']}"

            for lane in lanes:
                # Not `.get`: `validate` refuses a missing lane before this runs,
                # in both the checker and the generator, so a soft branch here was a
                # third statement of one rule that could only ever be dead. A KeyError
                # naming the lane is the honest failure if that ever stops holding.
                pattern = groove["patterns"][lane]

                checked += 1
                significant = [c for c in pattern if not c.isspace()]

                if len(significant) != PATTERN_LENGTH:
                    problems.append(f"{where}/{lane}: {len(significant)} significant characters, "
                                    f"expected {PATTERN_LENGTH} — a short pattern tiles wrongly "
                                    f"and yields a groove that is merely subtly wrong")

                bad = sorted({c for c in significant if c != "." and c not in "123456789"})

                if bad:
                    problems.append(f"{where}/{lane}: {bad} is not a velocity — only '.' and 1-9")

            checked += 1

            if not any(c in "123456789" for c in groove["patterns"][anchor]):
                problems.append(f"{where}: the anchor lane ({anchor}) is silent — every groove "
                                f"here carries its pulse on it")

    return checked


# Every claim a description is allowed to make, and what each one means in data.
#
# ONE TABLE, replacing four hand-written blocks — a named-timbre loop, two
# bateria ifs, a comparative pair and a superlative pair, each with its own copy
# of "is this phrase in the text, count it, compare it, word the failure".
# /simplify.
#
# Three kinds, and the distinction is what makes any of this checkable:
#
#   exact        a timbre by name, `bateria em silêncio` — compares to a field.
#   comparative  `alto`, `baixa` — no threshold makes swing "high", but they do
#                assert a side of the four-profile average, and that is exact.
#   superlative  `quase zero`, `quantizado` — these really do claim the minimum.
#
# The first version read `cachaça baixa` as "the lowest" and fired on petrolina,
# whose 16 is genuinely low and is not the minimum (sp's 6 is). A checker that
# fails on correct data is the failure this project keeps finding, so the two
# are kept apart deliberately.
CLAIM_RULES: dict[str, tuple[str, str, object]] = {
    "timbre hi-fi":        ("exact",       "timbre",  "hifi"),
    "timbre lo-fi":        ("exact",       "timbre",  "lofi"),
    "timbre ciclotron":    ("exact",       "timbre",  "ciclo"),
    "bateria em silêncio": ("exact",       "muted",   True),
    "bateria presente":    ("exact",       "muted",   False),
    "swing alto":          ("comparative", "swing",   "above"),
    "cachaça baixa":       ("comparative", "cachaca", "below"),
    "cachaça quase zero":  ("superlative", "cachaca", None),
    "quantizado":          ("superlative", "swing",   None),
}


def check_descriptions(order: list[str], profiles: dict, problems: list[str]) -> int:
    """Each profile's own description, against its own data — and against what
    it DECLARES it says.

    THE DESCRIPTIONS MAKE CLAIMS AND NOTHING CHECKED THEM BEFORE 09-01. These
    three lines are what the side panel shows under the active profile, and they
    say things that are true or false about the numbers beside them:

        campina    "Timbre HI-FI, bateria em silêncio."   hifi, bateria muted
        caruaru    "Peso extra na zabumba, swing alto."    swing 54, above average
        petrolina  "Timbre LO-FI, cachaça baixa."          lofi, cachaca 16
        sp         "Quantizado, cachaça quase zero."       swing 16 and cachaca 6, both lowest

    THE CLAIMS ARE NOW DECLARED IN THE JSON, and that closes a hole rather than
    removing the parsing. 09-01's `/simplify` read this function as deriving
    what it could be told — but generating the prose does NOT let the substring
    matching go: something still has to tie Portuguese text to a numeric field,
    and looking for the phrase is that something.

    What WAS wrong is that the tie ran one way. A description reworded past a
    phrase silently stopped being checked — and 09-03 rewords descriptions. So
    `descClaims` names what each profile asserts, and this function checks BOTH
    directions:

      - every declared claim must appear in the prose, or the two have drifted;
      - every claim phrase appearing in the prose must be declared, or a claim
        has slipped in unchecked.

    The count is therefore `sum(len(descClaims))` and is asserted in `main`,
    not merely printed — 09-01's `/code-review` found these counts printed and
    never asserted, which is "reports OK while a class of check is broken".
    """
    checked = 0

    def mean(field: str) -> float:
        return sum(profiles[k]["scalars"][field] for k in order) / len(order)

    def is_lowest(field: str, pid: str) -> bool:
        mine = profiles[pid]["scalars"][field]
        return all(profiles[k]["scalars"][field] >= mine for k in order)

    for pid in order:
        desc = profiles[pid]["identity"]["description"].lower()
        claims = profiles[pid]["descClaims"]
        scalars = profiles[pid]["scalars"]

        # ── the two directions ──────────────────────────────────────────────
        for phrase in claims:
            if phrase not in desc:
                problems.append(f"{pid}: descClaims names {phrase!r} but the description "
                                f"does not say it — reword one to match the other")

        for phrase in CLAIM_RULES:
            if phrase in desc and phrase not in claims:
                problems.append(f"{pid}: the description says {phrase!r} and descClaims does "
                                f"not declare it, so the claim is not checked — add it")

        # ── and what each one asserts ───────────────────────────────────────
        for phrase in claims:
            kind, field, arg = CLAIM_RULES[phrase]
            checked += 1
            mine = scalars[field]

            if kind == "exact":
                want = float(TIMBRE_INDEX[arg]) if field == "timbre" else float(arg)

                if mine != want:
                    if field == "timbre":
                        actual = next(k for k, v in TIMBRE_INDEX.items() if v == mine)
                        problems.append(f"{pid}: says {phrase!r} but timbre is {actual!r}")
                    else:
                        problems.append(f"{pid}: says {phrase!r} but bateria is "
                                        f"{'muted' if mine else 'not muted'}")

            elif kind == "comparative":
                average = mean(field)
                # EQUALITY FAILS ON BOTH SIDES, and collapsing the two branches
                # into `(arg == "above") != (mine > average)` quietly lost that
                # for `below`: a value exactly ON the average then passed, while
                # the `above` side still rejected it. A profile claiming
                # `cachaça baixa` at precisely the mean is not making a true
                # statement in either direction. /code-review.
                ok = mine > average if arg == "above" else mine < average

                if not ok:
                    problems.append(f"{pid}: says {phrase!r} but {field} is {mine:g}, not "
                                    f"{arg} the four-profile average of {average:g}")

            elif not is_lowest(field, pid):
                lower = [k for k in order if profiles[k]["scalars"][field] < mine]
                problems.append(f"{pid}: says {phrase!r} but {lower} have less {field}")

    return checked


def main() -> int:
    # Read once, used twice.
    param_ids_src = PARAM_IDS_H.read_text(encoding="utf-8")
    lanes = read_lane_order(param_ids_src)
    infos = read_profile_infos(param_ids_src)
    js_order, js = read_profiles_json()
    cpp_order, cpp = read_profiles_cpp(lanes, infos)

    print(f"lane order:   {lanes}   (read from ids::lanes)")
    print(f"profiles.json: {len(js)} profiles {js_order}")
    print(f"Profiles.cpp: {len(cpp)} profiles {cpp_order}")

    if len(cpp_order) != len(infos):
        fail(f"parsed {len(cpp_order)} Profile entries from Profiles.cpp but "
             f"ids::profileInfos has {len(infos)} — the C++ table parse is incomplete")
    if js_order != cpp_order:
        fail(f"profile order differs: profiles.json {js_order} vs C++ {cpp_order}")

    # EVERY GROOVE OF EVERY PROFILE, not the profile's own. A bank whose second
    # entry was never compared is a bank whose second entry can be wrong, and
    # 09-03 fills these banks — so the total is derived from the JSON's actual
    # groove count rather than from `len(js_order)`, and asserted below.
    expected_patterns = sum(len(js[pid]["grooves"]) for pid in js_order) * len(lanes)
    problems: list[str] = []
    patterns_checked = field_checked = field_problems = 0

    def check_field(pid: str, section: str, key: str) -> None:
        """Counts at the point of comparison. The previous version re-derived
        'was this field-level' by string-sniffing the message format, which is
        the same 'reports OK while a class of check is broken' failure this
        script exists to prevent — reproduced in its own reporting."""
        nonlocal field_checked, field_problems
        field_checked += 1
        a, b = js[pid][section][key], cpp[pid][section][key]
        if a != b:
            field_problems += 1
            problems.append(f"{pid}.{key}: profiles.json {a!r} vs C++ {b!r}")

    for pid in js_order:
        js_bank, cpp_bank = js[pid]["grooves"], cpp[pid]["grooves"]

        if len(js_bank) != len(cpp_bank):
            problems.append(f"{pid}: profiles.json has {len(js_bank)} grooves, "
                            f"Profiles.cpp has {len(cpp_bank)}")

        for g, (a_g, b_g) in enumerate(zip(js_bank, cpp_bank)):
            # The bank is ORDERED and grooves[0] is the profile's own, so a
            # reordered bank silently changes what selecting a profile loads.
            # Compared by position AND by id, which is what catches that.
            if a_g["id"] != b_g["id"]:
                problems.append(f"{pid}: groove {g} is {a_g['id']!r} in profiles.json "
                                f"and {b_g['id']!r} in Profiles.cpp — the bank is ordered")

            if a_g["name"] != b_g["name"]:
                problems.append(f"{pid}/{a_g['id']}: name {a_g['name']!r} vs {b_g['name']!r}")

            # EVERY groove's feel, not just the default's. Compared exactly:
            # both sides come from source text, so a tolerance would only hide a
            # wrong digit.
            for field in ("bpm", "swing", "cachaca"):
                field_checked += 1
                x, y = a_g["feel"][field], b_g["feel"][field]
                if x != y:
                    field_problems += 1
                    # `!r`, not `:g` — which is six significant digits on BOTH
                    # sides, so 61.53125 against 61.5312 printed as two identical
                    # numbers. A script whose thesis is "name the digit that
                    # diverged" must be able to show it. `check_field` below has
                    # always used `!r`; this loop was the one place that did not.
                    problems.append(f"{pid}/{a_g['id']}.{field}: profiles.json {x!r} "
                                    f"vs C++ {y!r}")

            for lane in lanes:
                a = a_g["patterns"].get(lane)
                b = b_g["patterns"].get(lane)
                where = f"{pid}/{a_g['id']}/{lane}"
                if a is None:
                    problems.append(f"{where}: missing in profiles.json")
                    continue
                if b is None:
                    problems.append(f"{where}: missing in Profiles.cpp")
                    continue
                patterns_checked += 1
                # whitespace is cosmetic in the notation
                if a.replace(" ", "") != b.replace(" ", ""):
                    problems.append(f"{where}:\n    profiles.json: {a!r}\n    C++:           {b!r}")

        # Floats compared exactly: both sides come from source text, so an exact
        # match is achievable and a tolerance would hide a real wrong digit.
        for key in ("bpm", "swing", "cachaca", "timbre", "muted"):
            check_field(pid, "scalars", key)

        for key in ("displayName", "shortName", "code", "description"):
            check_field(pid, "identity", key)

    field_checked += check_timbres(DATA_JS.read_text(encoding="utf-8"), problems)

    claims_checked = check_descriptions(js_order, js, problems)
    # NO COUNT ASSERTION HERE, and the absence is deliberate. One was written —
    # `claims_checked != sum(len(descClaims))` — and it could never fire:
    # `check_descriptions` increments unconditionally inside `for phrase in
    # claims`, so its return value IS that sum by construction. The scenario its
    # message described, a declared claim going unchecked, is caught one function
    # up by the `phrase not in desc` test. `check_pattern_shape`'s count assertion
    # is NOT the same shape: that one walks a nested structure and can genuinely
    # skip a level, which a mutation proved. /code-review.

    # ASSERTED, not just printed. `check_descriptions` matches Portuguese
    # substrings — reword "bateria em silêncio" to "bateria muda" and the claim
    # stops being checked, the count drops, and the script still says OK. That
    # is "reports OK while a class of check is broken", which is the failure
    # `check_field`'s own docstring below was written against, reproduced one
    # function over. 09-02 rewrites these descriptions. /code-review.
    #
    # The floor is per-profile rather than a total: every profile's third line
    # names its timbre, so one claim each is the weakest true statement, and it
    # catches a whole profile going unchecked.
    # THE FLOOR, restated for the declaration. It used to be "the description
    # names a timbre", which was a proxy for "something in this prose is checked
    # at all". Now that a claim is checked because it is DECLARED, the honest
    # floor is that every profile declares at least one — a profile with an
    # empty descClaims has three lines of unchecked prose, which is where this
    # whole function started.
    for pid in js_order:
        if not js[pid]["descClaims"]:
            problems.append(f"{pid}: declares no description claims, so none of its three "
                            f"lines is checked against its data — declare the claim its "
                            f"prose makes, or add the phrase to CLAIM_RULES if it is new")

    # NO OUTER FLOOR HERE. One was written, comparing `claims_checked` against
    # `len(js_order)`, and it could never fire alone: a profile contributing zero
    # claims is exactly a profile naming no timbre, which the per-profile floor
    # in `check_descriptions` already reports — by name, and with what to do. The
    # outer one only ever restated it with less information. /simplify.
    shape_checked = check_pattern_shape(js_order, lanes, js, problems)

    # ASSERTED, not printed. `check_pattern_shape` walks the banks, so its count
    # rises as 09-03 adds grooves — and a count that silently FALLS is a groove
    # that stopped being shape-checked. One lane check per lane, plus one anchor
    # check per groove. 09-01's `/code-review` found exactly this class of number
    # printed and never compared, and the plan for 09-02 named it again.
    expected_shape = sum(len(js[pid]["grooves"]) for pid in js_order) * (len(lanes) + 1)

    if shape_checked != expected_shape:
        problems.append(f"ran {shape_checked} pattern-shape checks, expected {expected_shape} "
                        f"— a groove was skipped")

    if patterns_checked != expected_patterns:
        problems.append(f"compared {patterns_checked} patterns, expected {expected_patterns}")

    print(f"patterns compared: {patterns_checked}/{expected_patterns}")
    print(f"fields compared:   {field_checked}")
    print(f"description claims: {claims_checked}")
    print(f"pattern shape:     {shape_checked}")
    print(f"mismatches:        {len(problems)} ({field_problems} field-level)")

    if problems:
        print(f"\n{len(problems)} problem(s):", file=sys.stderr)
        for p in problems:
            print(f"  {p}", file=sys.stderr)
        return 1

    print("\nOK — the C++ groove tables match assets/profiles.json")
    return 0


# Everything this gate reads, in one place — CMake depends on exactly this (gate_inputs.py).
# PUBLISHED as INPUTS for the two gates that import this module: what it reads, they read.
INPUTS = gate_inputs.declare(__name__, files=[PROFILES_JSON, DATA_JS, PROFILES_CPP, PARAM_IDS_H,
                                              PROFILES_H, MIXBUS_H])


if __name__ == "__main__":
    sys.exit(gate_inputs.run(main))
