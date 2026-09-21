#!/usr/bin/env python3
"""Prove every non-ASCII character in a src/ string literal is one this UI draws.

Why this exists: 06-04 stopped spelling accented text as `\\xNN` escapes and
pinned the source/execution charset in CMakeLists.txt instead. That deleted a
whole class of bug — the escape is greedy, `"m\\xc3\\xa9dio"` reads `\\xa9d` as a
three-digit escape, and four separate local fixes had papered over it — but it
traded one property away. An escape produces exact bytes no matter what charset
the compiler assumed; a real UTF-8 literal does not.

So the literals need a guard, and the runtime check in tests/UiTest.cpp is not
it. That check walks `ids::profileInfos` and `forrobox::timbreSpecs` and nothing
else, while 06-04 converted ~19 further literals — the CACHAÇA knob label, the
FORRÓ·BOX wordmark, TRIÂNGULO and GANZÁ, the sample names, the ÷2/×2 minis, the
cycler arrows, LOAD IR…, the DRAG MIDI arrow. `/code-review` found that gap, and
a mutation confirmed it: replacing CACHAÇA with its mojibake CACHAÃ‡A built
clean and ran 3778/3778 GREEN. A check that cannot fail is this project's
recurring lesson, and that one could not.

This closes it at the source level, which is where the realistic failure lands:
a bad merge, an editor that rewrote a file in Latin-1, or a literal pasted from
mangled output. It reads the bytes as UTF-8 and asserts that every non-ASCII
character in a literal is one this interface actually draws.

What it does NOT cover, stated so the gap is known rather than assumed closed:
  * what the COMPILER did with those bytes. On MSVC without /utf-8 the source is
    decoded AND narrow literals re-encoded with the same ANSI code page, so on a
    single-byte page like this project's pt-BR CP1252 the bytes round-trip
    unchanged and there is nothing to detect — the build is correct anyway. On a
    DBCS page (CP932/936/950) a lead byte swallows the next and the damage IS
    visible, to the runtime check in UiTest.cpp. Neither check can see a missing
    /utf-8 on CP1252, and neither needs to.
  * comments. Only string literals are inspected; prose may say anything.
  * tests/. Its literals legitimately use characters this UI does not draw — a
    `±` in a tolerance message, and deliberate mojibake fixtures — so the drawn
    repertoire is the wrong rule for that tree. Recorded rather than assumed.

Exit 0 when every literal is clean; exit 1 naming each offending file, line and
code point.
"""
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
UI_TEST = ROOT / "tests" / "UiTest.cpp"

# THE RULE IS THE REPERTOIRE, NOT AN INVENTORY of what the text happens to use
# today. The allowlist this replaces held twelve characters — the ones present
# on the day it was written — so `É Ó Ç · × ÷ … ‹ › ↓ ↗` were all outside it and
# the first display name carrying an É would have been a FALSE FAILURE on a
# correct build. Stated as a rule, a new correct accent cannot trip it.
PORTUGUESE = set("áàâãéêíóôõúüç" "ÁÀÂÃÉÊÍÓÔÕÚÜÇ")

# And the typographic characters this UI draws, each named with its source so a
# future addition has to say what draws it rather than just widening the set.
#
# A set with trailing comments, not a dict: the descriptions were never read —
# only the keys reached `ALLOWED` — so the mapping was ceremony. This is also
# the shape tests/UiTest.cpp states the same repertoire in, which is what makes
# the two comparable line for line. /simplify.
TYPOGRAPHIC = set(
    "·"    # css:341 — the FORRÓ·BOX lockup and the sub-dot separators
    "×÷"   # the header's double and halve minis; the kit overlay's close button
    "—…"   # campina's em dash; LOAD IR… and Typography's truncation ellipsis
    "‹›"   # the preset cycler arrows, U+2039 / U+203A
    "↓↗"   # DRAG MIDI, drawn as text so css:539 is comparable; the kit arrow
    "™"    # CICLOTRON™
)

ALLOWED = PORTUGUESE | TYPOGRAPHIC

# Comments, char literals and string literals in ONE alternation, and the order
# is the discipline: whichever starts first wins the text it covers, so a `/*`
# inside a string can never open a comment and a quote inside a comment can
# never open a string.
#
# This replaced two regex passes that stripped comments and THEN matched
# literals — which read a comment marker inside a string as a comment. A literal
# containing `/*` blanked everything up to the next `*/`, hiding accented
# literals in between; a three-line probe reported ZERO literals. /code-review.
#
# The char-literal arm carries a LOOKBEHIND, because `1'000` is a digit
# separator and not a quote. Without it, a separator and an apostrophe inside a
# literal on the SAME line — `int x = 1'000; const char* n = "a'b É";` — let the
# char arm swallow the string's opening quote, and the literal was never
# inspected: a silent under-report, in a checker whose whole purpose is not to
# have one. No `src/` file contains a separator today, so it had never fired.
# /code-review.
#
# The first fix was a hand-rolled character loop. It was correct on that probe
# and wrong elsewhere: its char-literal branch had no newline stop, so a digit
# separator (`1'000`) or an apostrophe in code desynchronised the rest of the
# file — measured, it found ZERO literals in a two-line probe the alternation
# reads correctly. It was also 40.8 ms against 3.6 ms over `src/`. Same output
# on all 556 literals, one fewer failure mode, 11x faster. /simplify.
LITERALS = re.compile(
    r'//[^\n]*'                  # a line comment
    r'|/\*.*?\*/'                # a block comment
    r"|(?<![0-9A-Za-z_])'(?:\\.|[^'\\\n])*'"   # a char literal, not a digit separator
    r'|"((?:\\.|[^"\\\n])*)"',     # a STRING literal — the only capturing arm
    re.S)


def string_literals(text: str):
    """Every string literal in a C++ translation unit, with its line number."""
    line, pos = 1, 0

    for match in LITERALS.finditer(text):
        line += text.count("\n", pos, match.start())
        pos = match.start()

        # group(1) is set only by the string-literal alternative; a comment or a
        # char literal matches with it None and is skipped.
        if match.group(1) is not None:
            yield line, match.group(1)

        line += text.count("\n", pos, match.end())
        pos = match.end()


def self_test() -> list[str]:
    """The tokenizer, against subjects with known answers.

    THREE fixes to one regex arrived as three reviewers hand-writing an ad-hoc
    probe, and each left behind a COMMENT describing the probe rather than the
    probe. 04-01's law is that every measurement instrument is self-tested
    including a case it must reject; this file is one and had prose instead.
    """
    cases = [
        ('const char* a = "/*";\nconst char* b = "\u00d3ops";\nconst char* c = "*/";\n',
         ['/*', '\u00d3ops', '*/'],
         'a block-comment marker INSIDE a literal does not open a comment'),
        ('const char* u = "https://x/\u00c9";\nconst char* n = "Caf\u00e9";\n',
         ['https://x/\u00c9', 'Caf\u00e9'],
         'a line-comment marker inside a literal does not open a comment'),
        ("int x = 1'000; const char* n = \"a'b \u00c9\";\n",
         ["a'b \u00c9"],
         'a digit separator is not a char literal'),
        ('// a comment with "a quoted \u00d3"\nconst char* n = "real \u00c1";\n',
         ['real \u00c1'],
         'a quoted string inside a comment is NOT a literal'),
        ("char c = '\\\\''; const char* n = \"S\u00e3o\";\n",
         ['S\u00e3o'],
         'an escaped quote inside a char literal does not unbalance the scan'),
    ]

    problems = []

    for source, expected, what in cases:
        got = [lit for _, lit in string_literals(source)]
        if got != expected:
            problems.append(f"tokenizer self-test — {what}: expected {expected!r}, got {got!r}")

    return problems


# Where a non-ASCII message literal is SAFE. `check`, `checkEqual` and `section`
# have `const char*` overloads that convert with `fromUTF8`; anywhere else the
# literal reaches `juce::String (const char*)`, which reads LATIN-1
# (juce_String.cpp:306) and prints mojibake.
#
# THE OVERLOADS ARE NOT ENOUGH, which is why this rule exists. `section ("… — "
# + mode)` binds `operator+ (const char*, const String&)` and constructs the
# String BEFORE `section` is called, so the overload never sees it — 20 lines of
# a PASSING run printed mojibake with the overloads already in place, found by
# reading the output rather than the exit code. /simplify.
UNSAFE_BEFORE = re.compile(r'juce::String\s*\(\s*$')
UNSAFE_AFTER = re.compile(r'^\s*\+')


def check_test_message_literals() -> list[str]:
    """A non-ASCII literal under tests/ must be a DIRECT argument, not built
    into a juce::String on the way."""
    problems = []

    for path in sorted((ROOT / "tests").rglob("*.cpp")):
        text = path.read_text(encoding="utf-8")

        for line, literal in string_literals(text):
            if not any(ord(c) > 127 for c in literal):
                continue

            index = text.find(f'"{literal}"')
            if index < 0:
                continue

            before = text[max(0, index - 40):index]
            after = text[index + len(literal) + 2:index + len(literal) + 14]

            if UNSAFE_BEFORE.search(before) or UNSAFE_AFTER.match(after):
                problems.append(
                    f"{path.relative_to(ROOT)}:{line}: a non-ASCII message literal reaches "
                    f"`juce::String (const char*)`, which reads Latin-1 and prints mojibake. "
                    f"Wrap it: `fbtest::utf8 (\"…\")`")

    return problems


def main() -> int:
    problems: list[str] = self_test() + check_test_message_literals()
    files = 0
    literals = 0
    accented = 0

    for path in sorted(list(SRC.rglob("*.h")) + list(SRC.rglob("*.cpp"))):
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError as exc:
            problems.append(f"{path.relative_to(ROOT)}: not valid UTF-8 ({exc})")
            continue

        files += 1

        for line, literal in string_literals(text):
            literals += 1

            for char in literal:
                if ord(char) < 128:
                    continue

                accented += 1

                if char not in ALLOWED:
                    problems.append(
                        f"{path.relative_to(ROOT)}:{line}: literal carries "
                        f"U+{ord(char):04X} {char!r}, which Brazilian Portuguese "
                        f"is not written with and this UI does not draw"
                    )

    print(f"files scanned:      {files}")
    print(f"literals inspected: {literals}")
    print(f"non-ASCII chars:    {accented}")

    # The floor. A version of src/ that had lost every accent — the exact damage
    # a wrong charset does on a lossy code page — would satisfy every check
    # above, because "no offending characters" is also true of no characters.
    if accented < 40:
        problems.append(
            f"only {accented} non-ASCII characters found in src/ literals. The "
            f"accented text is gone, which is what a lossy source charset looks "
            f"like — this check passes trivially on a file with no accents left"
        )

    # ── the repertoire has ONE owner, and this proves it ───────────────────
    #
    # tests/UiTest.cpp states the same set, because the runtime check needs it
    # at runtime. Two hand-kept copies is the failure src/ParameterIDs.h names
    # in its own comment — "one array of structs makes divergence impossible
    # instead of detectable" — and it would be reintroduced here, one level up,
    # among the checkers. Add a drawn symbol, update one list, forget the other:
    # this script false-passes or the C++ check false-fails, and nothing says so.
    #
    # So the C++ array is the owner and this asserts agreement, which is the
    # technique verify-profiles.py and verify-geometry.py already use to read
    # C++ tables. It reads HEX, so a charset that mangled the file cannot move
    # either side of the comparison. /simplify.
    try:
        ui = UI_TEST.read_text(encoding="utf-8")
    except OSError as exc:
        problems.append(f"cannot read {UI_TEST.relative_to(ROOT)}: {exc}")
    else:
        block = re.search(
            r"static constexpr std::array<juce::juce_wchar, \d+> allowed \{(.*?)\n    \};",
            ui, re.S)

        if block is None:
            problems.append(
                "could not find the `allowed` code-point array in "
                f"{UI_TEST.relative_to(ROOT)} — if it was renamed or reshaped, this "
                "cross-check silently stops comparing, so it fails instead")
        else:
            cpp = {chr(int(h, 16)) for h in re.findall(r"0x([0-9A-Fa-f]{4}),", block.group(1))}

            for char in sorted(ALLOWED - cpp):
                problems.append(
                    f"U+{ord(char):04X} {char!r} is allowed here but NOT in "
                    f"{UI_TEST.relative_to(ROOT)}'s `allowed` — the runtime check "
                    f"would false-fail on it")

            for char in sorted(cpp - ALLOWED):
                problems.append(
                    f"U+{ord(char):04X} {char!r} is in {UI_TEST.relative_to(ROOT)}'s "
                    f"`allowed` but not here — this check would false-pass on it")

            # Only CLAIMED when true. The first version printed "agreeing with"
            # unconditionally, above the failure list, so a run that had just
            # detected divergence still said the two agreed.
            if cpp == ALLOWED:
                print(f"repertoire:         {len(ALLOWED)} characters, agreeing with "
                      f"{UI_TEST.relative_to(ROOT)}")

    if problems:
        # stderr, matching verify-theme.py's `exit_with` and its two siblings. A
        # build log that filters stderr to surface failures would otherwise show
        # nothing for this one check. /simplify.
        print(f"\nmismatches: {len(problems)}\n", file=sys.stderr)
        for problem in problems:
            print(f"  {problem}", file=sys.stderr)
        return 1

    print("\nOK — every non-ASCII character in a src/ literal is one this UI draws")
    return 0


if __name__ == "__main__":
    sys.exit(main())
