"""C++ source text, as the gates read it: comments and literals told apart from code.

Shared by verify-charset.py (which reads the string literals) and verify-geometry.py
(which blanks everything that is not code before walking braces). One regex, because
it has been fixed three times and a copy would need every fix twice.
"""
from __future__ import annotations

import re

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


def blank_non_code(text: str) -> str:
    """`text` with every comment and string/char literal replaced by spaces.

    Newlines survive, so offsets and line numbers still point at the source; a `{`
    inside a comment or a string can no longer be mistaken for a brace.
    """
    return LITERALS.sub(lambda m: re.sub(r"[^\n]", " ", m.group(0)), text)
