#!/usr/bin/env python3
"""Prove the C++ groove tables still match data.js, the design source of truth.

Why this exists: a transcription error in the musical content produces no crash,
no failed build and no failing test — only a groove that is subtly wrong, with no
way to tell which digit. A unit test that embedded the expected patterns by hand
would just duplicate the same typo risk. Comparing against data.js is the only
check here with real signal.

Exit 0 when every pattern and scalar matches; exit 1 naming the profile and lane
that diverged.
"""
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DATA_JS = ROOT / "data.js"
PROFILES_CPP = ROOT / "src" / "Profiles.cpp"
PARAM_IDS_H = ROOT / "src" / "ParameterIDs.h"

TIMBRE_INDEX = {"hifi": 0, "lofi": 1, "ciclo": 2}
MIXBUS_H = ROOT / "src" / "MixBus.h"


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


def need(pattern: str, text: str, what: str, flags: int = 0) -> str:
    """re.search that fails loudly instead of raising AttributeError."""
    m = re.search(pattern, text, flags)
    if not m:
        fail(f"could not read {what}")
    return m.group(1)


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


def read_data_js() -> tuple[list[str], dict]:
    src = DATA_JS.read_text(encoding="utf-8")

    literal = match_braces(src, src.index("{", src.index("const PROFILES = {")))

    profiles: dict = {}
    for m in re.finditer(r"^    (\w+):\s*\{", literal, re.M):
        key = m.group(1)
        body = match_braces(literal, literal.index("{", m.start()))

        ident = re.search(r'\bid:\s*"(\w+)"', body)
        if not ident:
            fail(f"data.js profile {key!r} has no id field")
        if ident.group(1) != key:
            fail(f"data.js key/id mismatch: key={key!r} id={ident.group(1)!r}")

        patterns = dict(
            re.findall(r'^\s+(\w+):\s+"([^"]+)",?\s*$', body[body.index("patterns"):], re.M)
        )

        # ([\d.]+) not (\d+): the latter matches "38" out of "38.5", so a wrong
        # fractional digit compared equal. Kept as float, never truncated.
        scalars = {
            k: float(need(rf"\b{k}:\s*([\d.]+)", body, f"{key}.{k}"))
            for k in ("bpm", "swing", "cachaca")
        }
        timbre_id = need(r'timbre:\s*"(\w+)"', body, f"{key}.timbre")
        if timbre_id not in TIMBRE_INDEX:
            fail(f"{key}: unknown timbre id {timbre_id!r}")
        scalars["timbre"] = float(TIMBRE_INDEX[timbre_id])
        scalars["muted"] = float(bool(re.search(r"muted:\s*\{\s*bateria:\s*true", body)))

        identity = {
            "code": need(r'\bcode:\s*"([^"]*)"', body, f"{key}.code"),
            "displayName": need(r'\bname:\s*"([^"]*)"', body, f"{key}.name"),
            "shortName": need(r'\bshort:\s*"([^"]*)"', body, f"{key}.short"),
        }

        # The three lines the side panel shows under the active profile. Joined
        # with "|" so one field compares the whole block — three separate fields
        # would let a line go missing and still compare two.
        desc_src = re.search(r"desc:\s*\[(.*?)\]", body, re.S)

        if desc_src is None:
            fail(f"{key}: no desc array in data.js")

        lines = re.findall(r'"([^"]*)"', desc_src.group(1))

        if len(lines) != 3:
            fail(f"{key}: data.js desc has {len(lines)} lines, expected 3")

        identity["description"] = "|".join(lines)
        profiles[key] = {"patterns": patterns, "scalars": scalars, "identity": identity}

    order_src = re.search(r"PROFILE_ORDER\s*=\s*\[([^\]]*)\]", src).group(1)
    order = [x.strip().strip('"') for x in order_src.split(",") if x.strip()]
    if list(profiles) != order:
        fail(f"data.js profile order {list(profiles)} != PROFILE_ORDER {order}")

    return order, profiles


def join_literals(text: str) -> str:
    """Adjacent C string literals, concatenated the way the compiler does.

    `"m\\xc3\\xa9" "dio"` is ONE string to the compiler. It has to be written that
    way because a `\\xNN` escape is greedy — `"m\\xc3\\xa9dio"` reads `\\xa9d` as a
    three-digit escape and is out of range — so any reader of this source that
    stops at the first closing quote compares half a word.
    """
    return "".join(re.findall(r'"([^"]*)"', text))


def decode_c_escapes(literal: str) -> str:
    """Turn \\xNN escapes back into text so an accented display name can be
    compared against data.js's UTF-8."""
    raw = re.sub(r"\\x([0-9a-fA-F]{2})", lambda m: chr(int(m.group(1), 16)), literal)
    return raw.encode("latin-1", "ignore").decode("utf-8", "replace")


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
        fields = [decode_c_escapes(join_literals(row.group(i))) for i in range(1, 8)]

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

    table = match_braces(src, src.index("{", src.index("kProfiles")))

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


def check_timbres(problems: list[str]) -> int:
    """The timbre names and sub-labels, against data.js's TIMBRES table.

    `timbreSpecs` carries the cutoff and drive the bus renders with; 06-02 added
    the sub-label the side panel shows. Both come from data.js and neither was
    compared against it before.

    THE NAME IS COMPARED WITH THE TRADEMARK STRIPPED, and that is a decision
    rather than a convenience: data.js says CICLOTRON(tm) and the C++ says
    CICLOTRON, because `PluginProcessor.cpp` scheduled the trademark on the
    parameter's choice string for Phase 8, alongside the visual treatment it
    belongs with. Stripping exactly that one character still catches every OTHER
    divergence, which a skipped check would not. The SUB-label keeps its own
    trademark and is compared verbatim.
    """
    js = DATA_JS.read_text(encoding="utf-8")
    cpp = MIXBUS_H.read_text(encoding="utf-8")

    table = js[js.index("const TIMBRES"):]
    expected = {}

    for row in re.finditer(r'id:\s*"(\w+)",\s*name:\s*"([^"]*)",\s*sub:\s*"([^"]*)"', table):
        expected[row.group(1)] = (row.group(2), row.group(3))

    if len(expected) != len(TIMBRE_INDEX):
        fail(f"data.js TIMBRES has {len(expected)} entries, expected {len(TIMBRE_INDEX)}")

    body = match_braces(cpp, cpp.index("{", cpp.index("timbreSpecs")))
    actual = [(decode_c_escapes(m.group(1)), decode_c_escapes(m.group(2)))
              for m in re.finditer(r'\{\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,', body)]

    if len(actual) != len(TIMBRE_INDEX):
        fail(f"timbreSpecs has {len(actual)} rows, expected {len(TIMBRE_INDEX)}")

    compared = 0

    for timbre_id, index in TIMBRE_INDEX.items():
        want_name, want_sub = expected[timbre_id]
        got_name, got_sub = actual[index]

        if got_name != want_name.replace("\u2122", ""):
            problems.append(f"timbre[{index}].displayName: data.js {want_name!r} "
                            f"(trademark stripped) vs C++ {got_name!r}")

        if got_sub != want_sub:
            problems.append(f"timbre[{index}].subLabel: data.js {want_sub!r} vs C++ {got_sub!r}")

        compared += 2

    return compared


def main() -> int:
    # Read once, used twice.
    param_ids_src = PARAM_IDS_H.read_text(encoding="utf-8")
    lanes = read_lane_order(param_ids_src)
    infos = read_profile_infos(param_ids_src)
    js_order, js = read_data_js()
    cpp_order, cpp = read_profiles_cpp(lanes, infos)

    print(f"lane order:   {lanes}   (read from ids::lanes)")
    print(f"data.js:      {len(js)} profiles {js_order}")
    print(f"Profiles.cpp: {len(cpp)} profiles {cpp_order}")

    if len(cpp_order) != len(infos):
        fail(f"parsed {len(cpp_order)} Profile entries from Profiles.cpp but "
             f"ids::profileInfos has {len(infos)} — the C++ table parse is incomplete")
    if js_order != cpp_order:
        fail(f"profile order differs: data.js {js_order} vs C++ {cpp_order}")

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
            problems.append(f"{pid}.{key}: data.js {a!r} vs C++ {b!r}")

    for pid in js_order:
        for lane in lanes:
            a = js[pid]["patterns"].get(lane)
            b = cpp[pid]["patterns"].get(lane)
            if a is None:
                problems.append(f"{pid}/{lane}: missing in data.js")
                continue
            if b is None:
                problems.append(f"{pid}/{lane}: missing in Profiles.cpp")
                continue
            patterns_checked += 1
            # whitespace is cosmetic in the notation
            if a.replace(" ", "") != b.replace(" ", ""):
                problems.append(f"{pid}/{lane}:\n    data.js: {a!r}\n    C++:     {b!r}")

        # Floats compared exactly: both sides come from source text, so an exact
        # match is achievable and a tolerance would hide a real wrong digit.
        for key in ("bpm", "swing", "cachaca", "timbre", "muted"):
            check_field(pid, "scalars", key)

        for key in ("displayName", "shortName", "code", "description"):
            check_field(pid, "identity", key)

    field_checked += check_timbres(problems)

    if patterns_checked != expected_patterns:
        problems.append(f"compared {patterns_checked} patterns, expected {expected_patterns}")

    print(f"patterns compared: {patterns_checked}/{expected_patterns}")
    print(f"fields compared:   {field_checked}")
    print(f"mismatches:        {len(problems)} ({field_problems} field-level)")

    if problems:
        print(f"\n{len(problems)} problem(s):", file=sys.stderr)
        for p in problems:
            print(f"  {p}", file=sys.stderr)
        return 1

    print("\nOK — the C++ groove tables match data.js")
    return 0


if __name__ == "__main__":
    sys.exit(main())
