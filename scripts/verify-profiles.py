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

  1. `ids::profileInfos` — the identity strings and the three description lines
     are still hand-written, and NOTHING else compares them to anything.
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
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
PROFILES_JSON = ROOT / "assets" / "profiles.json"

# Still read, and only for the TIMBRES table: `check_timbres` compares
# `timbreSpecs` against it. The grooves left this file at 09-01; the timbres,
# the instruments, the channel defaults and the preset labels did not.
DATA_JS = ROOT / "data.js"
PROFILES_CPP = ROOT / "src" / "Profiles.cpp"
PARAM_IDS_H = ROOT / "src" / "ParameterIDs.h"
PROFILES_H = ROOT / "src" / "Profiles.h"

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
MIXBUS_H = ROOT / "src" / "MixBus.h"


def read_pattern_length() -> int:
    """`kPatternLength`, out of `Profiles.h` — never the digit 16 typed here.

    `decodePattern` REFUSES a pattern of any other length (Profiles.h:58, and
    deliberately, rather than padding), so this is the length a groove must be
    to reach the plugin at all. Written as a literal it was a fourth copy of a
    number the C++ already owns, in the file whose whole job is to not
    transcribe. /simplify.
    """
    m = re.search(r"inline constexpr int kPatternLength\s*=\s*(\d+);",
                  PROFILES_H.read_text(encoding="utf-8"))
    if m is None:
        fail("could not find kPatternLength in src/Profiles.h")

    return int(m.group(1))


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


def match_braces(text: str, open_at: int) -> str:
    """Body between the brace at open_at and its match. Brace matching, not a
    non-greedy regex: data.js has other 4-space-indented objects (CHANNEL_DEFAULTS
    among them) and a lazy match silently captures the wrong block."""
    depth = 0
    for i in range(open_at, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[open_at + 1 : i]
    fail("unbalanced braces")
    return ""


# Derived once, at import. `verify-midi.py` imports this module and
# `build-profiles.py` now does too, so all three read one answer.
TIMBRE_INDEX = read_timbre_index()
PATTERN_LENGTH = read_pattern_length()

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

        for lane in lanes:
            if lane not in p["patterns"]:
                fail(f"{key}: no pattern for lane {lane!r}")


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

        profiles[key] = {
            "patterns": dict(p["patterns"]),
            # Floats because the C++ side reads `38.0f` out of source text and
            # the two are compared exactly — a tolerance would hide a wrong digit.
            "scalars": {
                "bpm":     float(p["bpm"]),
                "swing":   float(p["swing"]),
                "cachaca": float(p["cachaca"]),
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
    entry_re = re.compile(
        r"\{" + sep + r"&ids::profileInfos\[(?P<idx>\d+)\]" + sep + r",[^,]*?"
        r"(?P<bpm>\d+)\s*,\s*(?P<swing>[\d.]+)f\s*,\s*(?P<cachaca>[\d.]+)f\s*,\s*"
        r"(?P<timbre>\d+)\s*,\s*(?P<muted>true|false)" + sep + r",(?:\s|/\*.*?\*/|//[^\n]*\n)*\{\{(?P<pats>.*?)\}\}",
        re.S,
    )

    for m in entry_re.finditer(table):
        idx = int(m.group("idx"))
        if idx >= len(infos):
            fail(f"Profile entry references ids::profileInfos[{idx}], which does not exist")
        info = infos[idx]
        pid = info["id"]

        strings = re.findall(r'"([^"]*)"', m.group("pats"))
        if len(strings) != len(lanes):
            fail(f"{pid}: C++ has {len(strings)} pattern strings, expected {len(lanes)}")

        order.append(pid)
        profiles[pid] = {
            "patterns": dict(zip(lanes, strings)),
            "scalars": {
                "bpm": float(m.group("bpm")),
                "swing": float(m.group("swing")),
                "cachaca": float(m.group("cachaca")),
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
        for lane in lanes:
            # Not `.get`: `validate` refuses a missing lane before this runs,
            # in both the checker and the generator, so a soft branch here was a
            # third statement of one rule that could only ever be dead. A KeyError
            # naming the lane is the honest failure if that ever stops holding.
            pattern = profiles[pid]["patterns"][lane]

            checked += 1
            significant = [c for c in pattern if not c.isspace()]

            if len(significant) != PATTERN_LENGTH:
                problems.append(f"{pid}/{lane}: {len(significant)} significant characters, "
                                f"expected {PATTERN_LENGTH} — a short pattern tiles wrongly and yields a "
                                f"groove that is merely subtly wrong")

            bad = sorted({c for c in significant if c != "." and c not in "123456789"})

            if bad:
                problems.append(f"{pid}/{lane}: {bad} is not a velocity — only '.' and 1-9")

        checked += 1
        anchor_pattern = profiles[pid]["patterns"].get(anchor, "")

        if not any(c in "123456789" for c in anchor_pattern):
            problems.append(f"{pid}: the anchor lane ({anchor}) is silent — every groove here "
                            f"carries its pulse on it")

    return checked


def check_descriptions(order: list[str], profiles: dict, problems: list[str]) -> int:
    """Each profile's own description, against its own data.

    THE DESCRIPTIONS MAKE CLAIMS AND NOTHING HAS EVER CHECKED THEM. These three
    lines are what the side panel shows under the active profile, and they say
    things that are true or false about the numbers beside them:

        campina    "Timbre HI-FI, bateria em silêncio."   hifi, bateria muted
        caruaru    "Peso extra na zabumba, swing alto."    swing 54, the highest
        petrolina  "Timbre LO-FI, cachaça baixa."          lofi, cachaca 16
        sp         "Quantizado, cachaça quase zero."       swing 16 and cachaca 6, both lowest

    Two kinds of claim, and the difference is what makes this checkable at all.

    NAMED claims are exact: a timbre by name, `bateria em silêncio`. They compare
    against a field.

    ORDINAL claims — `alto`, `baixa`, `quase zero` — are not. There is no
    threshold at which swing becomes "high", and inventing one would produce a
    checker that fails on correct data. What they DO assert is a rank among the
    four profiles, and that is exact: `swing alto` means no profile swings more.

    WHAT IS NOT WRITTEN IS NOT ASSERTED. A description that says nothing about
    cachaça claims nothing about it, so nothing is checked. A checker that
    guessed at silence would be the thing this project keeps finding: a check
    that fires on correct input.
    """
    checked = 0

    def mean(field: str) -> float:
        return sum(profiles[k]["scalars"][field] for k in order) / len(order)

    def is_lowest(field: str, pid: str) -> bool:
        mine = profiles[pid]["scalars"][field]
        return all(profiles[k]["scalars"][field] >= mine for k in order)

    for pid in order:
        desc = profiles[pid]["identity"]["description"].lower()
        scalars = profiles[pid]["scalars"]

        # ── named: the timbre ───────────────────────────────────────────────
        for name, index in (("hi-fi", 0), ("lo-fi", 1), ("ciclotron", 2)):
            if f"timbre {name}" in desc:
                checked += 1
                if scalars["timbre"] != float(index):
                    actual = next(k for k, v in TIMBRE_INDEX.items() if v == scalars["timbre"])
                    problems.append(f"{pid}: the description says \"Timbre {name.upper()}\" "
                                    f"but timbre is {actual!r}")

        # ── named: bateria ──────────────────────────────────────────────────
        if "bateria em silêncio" in desc:
            checked += 1
            if scalars["muted"] != 1.0:
                problems.append(f"{pid}: the description says \"bateria em silêncio\" "
                                f"but bateria is not muted")

        if "bateria presente" in desc:
            checked += 1
            if scalars["muted"] != 0.0:
                problems.append(f"{pid}: the description says \"bateria presente\" "
                                f"but bateria is muted")

        # ── comparative: above or below the four profiles' average ──────────
        #
        # `alto` and `baixa` are COMPARATIVE, not superlative, and the first
        # version of this got that wrong: it read `cachaça baixa` as "the
        # lowest" and fired on petrolina, whose 16 is genuinely low and is not
        # the minimum — sp's 6 is. A checker that fails on correct data is the
        # exact failure this docstring warns about, reproduced inside it.
        for field, word, above_average in (("swing",   "swing alto",    True),
                                           ("cachaca", "cachaça baixa", False)):
            if word not in desc:
                continue

            checked += 1
            average = mean(field)
            mine = scalars[field]

            if above_average and mine <= average:
                problems.append(f"{pid}: the description says \"{word}\" but {field} is "
                                f"{mine:g}, at or below the four-profile average of {average:g}")
            elif not above_average and mine >= average:
                problems.append(f"{pid}: the description says \"{word}\" but {field} is "
                                f"{mine:g}, at or above the four-profile average of {average:g}")

        # ── superlative: the lowest of the four ─────────────────────────────
        #
        # `quase zero` and `quantizado` ARE superlatives — one about cachaça and
        # one about swing, the second naming the absence of swing rather than
        # swing itself.
        for field, word in (("cachaca", "cachaça quase zero"), ("swing", "quantizado")):
            if word not in desc:
                continue

            checked += 1

            if not is_lowest(field, pid):
                lower = [k for k in order
                         if profiles[k]["scalars"][field] < scalars[field]]
                problems.append(f"{pid}: the description says \"{word}\" but {lower} "
                                f"have less {field}")

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

    expected_patterns = len(js_order) * len(lanes)
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
        for lane in lanes:
            a = js[pid]["patterns"].get(lane)
            b = cpp[pid]["patterns"].get(lane)
            if a is None:
                problems.append(f"{pid}/{lane}: missing in profiles.json")
                continue
            if b is None:
                problems.append(f"{pid}/{lane}: missing in Profiles.cpp")
                continue
            patterns_checked += 1
            # whitespace is cosmetic in the notation
            if a.replace(" ", "") != b.replace(" ", ""):
                problems.append(f"{pid}/{lane}:\n    profiles.json: {a!r}\n    C++:           {b!r}")

        # Floats compared exactly: both sides come from source text, so an exact
        # match is achievable and a tolerance would hide a real wrong digit.
        for key in ("bpm", "swing", "cachaca", "timbre", "muted"):
            check_field(pid, "scalars", key)

        for key in ("displayName", "shortName", "code", "description"):
            check_field(pid, "identity", key)

    field_checked += check_timbres(DATA_JS.read_text(encoding="utf-8"), problems)

    claims_checked = check_descriptions(js_order, js, problems)

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
    for pid in js_order:
        desc = js[pid]["identity"]["description"].lower()

        if not any(f"timbre {name}" in desc for name in ("hi-fi", "lo-fi", "ciclotron")):
            problems.append(f"{pid}: the description names no timbre, so nothing in it is "
                            f"checked against the data — reword it to name one, or teach "
                            f"check_descriptions the claim it makes instead")

    # NO OUTER FLOOR HERE. One was written, comparing `claims_checked` against
    # `len(js_order)`, and it could never fire alone: a profile contributing zero
    # claims is exactly a profile naming no timbre, which the per-profile floor
    # in `check_descriptions` already reports — by name, and with what to do. The
    # outer one only ever restated it with less information. /simplify.
    shape_checked = check_pattern_shape(js_order, lanes, js, problems)

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


if __name__ == "__main__":
    sys.exit(main())
