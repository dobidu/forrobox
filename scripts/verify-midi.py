#!/usr/bin/env python3
"""Prove the C++ Standard MIDI File writer produces the SAME BYTES as the
prototype's own `exportMIDI`, across a matrix of states.

Why this exists: `PLANNING.md:804-825` specifies the format and names
`exportMIDI()` in `audio.js` as the reference implementation, so the same
argument `verify-profiles.py` makes about groove tables applies here — a wrong
digit is not a crash, it is a file a DAW opens with the groove subtly wrong.

A MIDI file is worse than a groove table. Its bytes are a delta-encoded stream:
one wrong VLQ shifts every event after it and the result still parses. A test
asserting "34 events" or "the first note is 36" would pass against a file whose
timing is silently wrong from bar two. So this compares BYTES, and on a mismatch
it reports the FIRST DIVERGING OFFSET — with a delta-encoded stream, "the files
differ" sends the reader off to diff two hex dumps by hand, while the offset is
the whole diagnosis.

The reference is RUN, not transcribed. `scripts/verify-midi.js` loads `audio.js`
and calls the function the design source itself defines. A port of it here would
be a second transcription, which is the thing this check exists to make
unnecessary.

HOW THE C++ SIDE IS ASKED: through a `--emit-midi` mode on the test executable,
which reads the same NDJSON on stdin and writes the same hex on stdout. Chosen
over a dedicated tool because a second CMake target re-compiles the entire JUCE
module set — the reason `--render-audition` already lives there.

ONE PROCESS PER SIDE, not one per state. Measured, a Node spawn costs 20.3 ms and
a ForroBoxTests spawn 4.2 ms, so a state-per-process matrix took this gate to
1351 ms of every relink — three times the four existing cross-checks combined,
and three times the link it waits on. Batched it is ~35 ms. A gate that expensive
is one somebody eventually switches off, which is the failure every comment in
this file is about.

Exit 0 when every state in the matrix agrees, printing how many were compared;
exit 1 naming the case and the first diverging byte.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import pathlib
import re
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
VERIFY_JS = ROOT / "scripts" / "verify-midi.js"
VERIFY_PROFILES_PY = ROOT / "scripts" / "verify-profiles.py"
PARAM_IDS_H = ROOT / "src" / "ParameterIDs.h"

# Two BPMs, and the second is not decoration: the tempo meta is
# `round(60000000 / bpm)`, so a writer that truncates instead is only visible at
# a BPM whose REMAINDER IS AT LEAST HALF. 137 looked like it qualified — it does
# not divide evenly — but 60000000/137 = 437956.204 truncates and rounds to the
# same integer, so the case would have had no teeth. The assertion below is what
# stops the next edit from putting one back.
BPMS = (120, 139)


def fail(message: str) -> None:
    print(f"verify-midi: {message}", file=sys.stderr)
    sys.exit(1)


def assert_bpm_can_see_rounding() -> None:
    """A BPM that cannot tell rounding from truncation is a case with no teeth.

    Asserted rather than trusted, because the first version of this matrix
    contained exactly that: 137 does not divide 60000000 evenly, which is what
    it was chosen for, and rounds to the same integer truncation gives.
    """
    if not any((60_000_000 % b) / b >= 0.5 for b in BPMS):
        fail("no BPM in the matrix has a remainder of half a microsecond or more, "
             f"so the tempo meta\'s rounding is untested: {BPMS}")


def verify_profiles_module():
    """verify-profiles.py, loaded as a module.

    This script needs three things that file already reads correctly — the lane
    order, the PROFILES literal and its brace matcher — and a second copy of any
    of them is a second thing to keep in step with the sources. The first
    attempt at a private data.js parser here proved the hazard: a bare `id:`
    search matched the CHANNEL list and labelled campina's patterns `zabumba`.

    Loaded through importlib because the filename is hyphenated. The module is
    constants and function definitions with a guarded main, so importing it runs
    nothing.
    """
    spec = importlib.util.spec_from_file_location("verify_profiles", VERIFY_PROFILES_PY)
    if spec is None or spec.loader is None:
        fail(f"could not load {VERIFY_PROFILES_PY}")

    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def read_lane_order(module, src: str) -> list[str]:
    """Lane order comes from ids::lanes, never a copy of it.

    The JSON this script builds keys lanes by NAME, but the C++ side indexes
    `state.lanes` POSITIONALLY, so a hand-copied list would key both sides off
    the same stale ordering.
    """
    lanes = module.read_lane_order(src)

    if len(lanes) != 8:
        fail(f"ids::lanes parsed {len(lanes)} lanes, expected 8")

    return lanes


def read_channel_order(module, src: str) -> list[str]:
    """Channel ids from ids::channelInfos, for the same reason as the lanes.

    BRACE-MATCHED through `verify-profiles.py`'s own helper, not a `index("}}")`
    scan. A row with a nested initialiser ends the naive scan early — and
    `ids::profileInfos`, in this same header, already has one. A short channel
    list makes the mute matrix skip cases while still printing OK, which is the
    silent-coverage failure this file exists to prevent.
    """
    m = re.search(r"channelInfos\s*\{\{", src)
    if not m:
        fail("could not find the ids::channelInfos declaration in ParameterIDs.h")

    body = module.match_braces(src, src.index("{", m.start()))
    channels = re.findall(r'\{\s*"(\w+)"', body)

    if not channels:
        fail("ids::channelInfos parsed empty")

    return channels


def read_step_windows(src: str) -> tuple[int, ...]:
    """The step windows come from ids::stepWindows, never a hand-copy.

    A third window added to the `steps` parameter would otherwise go uncompared
    in the one check whose own comment calls the window the thing every other
    case cannot tell apart.
    """
    m = re.search(r"stepWindows\s*\{([^}]*)\}", src)
    if not m:
        fail("could not find the ids::stepWindows declaration in ParameterIDs.h")

    windows = tuple(int(x) for x in re.findall(r"\d+", m.group(1)))

    if not windows:
        fail("ids::stepWindows parsed empty")

    return windows


def read_profiles(module) -> dict[str, dict[str, str]]:
    """The four profiles' pattern strings — through verify-profiles.py's OWN
    reader, not a second one.

    That reader asserts key == id and asserts the key order against the declared
    profile order, both of which a private parser here got wrong. Reusing it also
    means this script follows when the source moves, which it did at 09-01: the
    grooves left `data.js` for `assets/profiles.json` and `read_data_js` became
    `read_profiles_json`. The rename broke this call, and only running all six
    gates together found it — each on its own was green. See
    `verify_profiles_module`.
    """
    order, profiles = module.read_profiles_json()

    if len(order) != 4:
        fail(f"assets/profiles.json declares {len(order)} profiles, expected 4")

    return {pid: profiles[pid]["patterns"] for pid in order}


def decode(pattern: str, steps: int) -> list[int]:
    """`9..5 ..6.` -> velocities, then TILED to the step window.

    The digit -> velocity rule (`min(127, digit * 14)`) is duplicated from
    src/Profiles.cpp DELIBERATELY and is NOT under test here: both sides of this
    comparison receive the identical array, so a stale multiplier can change
    which velocities are exercised but cannot hide a writer bug. What is under
    test is what the writer does with the numbers. `verify-profiles.py` is the
    check that pins the mapping itself.

    Tiling is `PLANNING.md:606`'s rule, which State::tileToFullWidth implements:
    16 -> 32 duplicates the bar rather than clearing it.
    """
    values = []
    for ch in pattern:
        if ch.isspace():
            continue
        values.append(0 if ch == "." else min(127, int(ch) * 14))

    if not values:
        fail(f"pattern decoded empty: {pattern!r}")

    return [values[i % len(values)] for i in range(steps)]


def lane_grid(lanes, channels, composite, row_for):
    """A grid in the prototype's shape, one row per lane from `row_for(index)`."""
    grid: dict[str, object] = {composite: {}}

    for i, lane in enumerate(lanes):
        row = row_for(i)

        if lane in channels:
            grid[lane] = row
        else:
            grid[composite][lane] = row

    return grid


def clamp_grid(lanes, channels, composite):
    """A grid carrying velocities above 127, and one at the very top of the byte.

    Values a real pattern cannot hold, on purpose: this is the only case that
    reaches the [1, 127] clamp on either side.
    """
    def row(i):
        out = [0] * 16
        out[0] = 128 + i          # just over the top
        out[4] = 255              # the top of the byte
        out[8] = 126              # in range, so the case still proves the normal path
        return out

    return lane_grid(lanes, channels, composite, row)


def sparse_grid(lanes, channels, composite):
    """One lane, two hits far apart — a delta of 173 ticks.

    173 needs two VLQ groups (0x81 0x2D). Every other case in this file keeps
    every delta under 128, where a VLQ bug is invisible.
    """
    return lane_grid(lanes, channels, composite,
                     lambda i: [100 if (i == 0 and step in (0, 8)) else 0 for step in range(16)])


def beyond_window_grid(lanes, channels, composite):
    """Lanes 32 slots long, to be exported at a 16-step window.

    The upper half is loud on purpose: a writer that ignored `steps` would put a
    second bar in the file, and every other case here stores nothing above the
    window, so none of them could tell.
    """
    # The lower half is not silent: with nothing inside the window the expected
    # file would carry no notes at all, and "both sides produced an empty track"
    # is not a comparison worth much.
    return lane_grid(lanes, channels, composite,
                     lambda i: [70 if step in (0, 6) else 0 for step in range(16)]
                               + [90 if step % 4 == 0 else 0 for step in range(16)])


def unmuted_state(channels, grid, bpm=120, steps=16):
    """The state document, with nothing muted.

    One place that knows the PROTOTYPE's state shape, because that shape is
    load-bearing: `verify-midi.js` hands it straight to `exportMIDI` unchanged,
    so anything the two sides had to translate differently would be a difference
    this comparison could not see. Stated four times, it would have to be found
    four times when the shape moves.
    """
    return {
        "bpm": bpm,
        "steps": steps,
        "channels": {c: {"mute": False} for c in channels},
        "grid": grid,
    }


def make_state(profiles, lanes, channels, composite, profile_id, steps, bpm, muted_channel):
    """One matrix case, built from a profile's patterns."""
    rows = profiles[profile_id]

    for lane in lanes:
        if lane not in rows:
            fail(f"data.js profile {profile_id!r} has no pattern for lane {lane!r}")

    state = unmuted_state(
        channels,
        lane_grid(lanes, channels, composite, lambda i: decode(rows[lanes[i]], steps)),
        bpm=bpm,
        steps=steps,
    )
    state["channels"] = {c: {"mute": c == muted_channel} for c in channels}
    return state


def run(command: list[str], documents: list[str], what: str) -> list[str]:
    """One process, every state — see the module docstring on why not one each.

    Returns one hex line per document, in order.
    """
    result = subprocess.run(
        command, input="\n".join(documents) + "\n",
        capture_output=True, text=True, cwd=str(ROOT)
    )

    if result.returncode != 0:
        fail(f"{what} exited {result.returncode}\n{result.stderr.strip()}")

    lines = [line.strip() for line in result.stdout.splitlines() if line.strip()]

    # Count checked as well as content. A side that emitted one line too few
    # would otherwise shift every comparison after it by one and report the
    # mismatches as byte differences in the wrong states.
    if len(lines) != len(documents):
        fail(f"{what} printed {len(lines)} lines for {len(documents)} states")

    # Length checked as well as the alphabet. `bytes.fromhex` raises on an odd
    # number of digits, and an uncaught traceback at a build step whose whole
    # value is a readable failure message is not a diagnosis.
    for i, line in enumerate(lines):
        if not re.fullmatch(r"(?:[0-9a-f]{2})+", line):
            fail(f"{what} line {i} is not an even-length run of hex bytes:\n{line[:400]}")

    return lines


def first_divergence(expected: bytes, actual: bytes) -> str:
    """The offset, and the bytes around it on both sides.

    The point of this function: one wrong VLQ shifts every byte after it, so a
    bare "files differ" is a diff by hand and an offset is a diagnosis.
    """
    limit = min(len(expected), len(actual))
    offset = next((i for i in range(limit) if expected[i] != actual[i]), limit)

    lo = max(0, offset - 6)
    hi = offset + 7

    def window(data: bytes) -> str:
        return " ".join(f"{b:02x}" for b in data[lo:hi])

    # The caret line is padded to the same column the two hex windows start in
    # (six of indent plus "prototype: "), so it points at the diverging byte
    # rather than one to its left.
    lines = [
        f"    first difference at byte {offset} (0x{offset:x})",
        f"      prototype: {window(expected)}",
        f"      C++:       {window(actual)}",
        f"{' ' * 17}{'   ' * (offset - lo)}^^",
    ]
    if len(expected) != len(actual):
        lines.append(f"    lengths differ: prototype {len(expected)}, C++ {len(actual)}")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", help="path to ForroBoxTests (which carries --emit-midi)")
    parser.add_argument("--node", default=None, help="path to the Node interpreter")
    args = parser.parse_args()

    # A check that quietly stops running is the failure this project keeps
    # finding, so an absent Node is loud rather than a silent pass.
    node = args.node or shutil.which("node")
    if not node:
        fail("Node was not found, so the prototype's exportMIDI cannot be run. "
             "Install Node 18+ (Blob must be a global), or pass --node.")

    exe = args.exe
    if not exe:
        for candidate in ("build-linux", "build-clang"):
            path = ROOT / candidate / "ForroBoxTests"
            if path.exists():
                exe = str(path)
                break
    if not exe or not pathlib.Path(exe).exists():
        fail("the test executable was not found; pass --exe <path to ForroBoxTests>")

    assert_bpm_can_see_rounding()

    profiles_module = verify_profiles_module()

    # Read once, used three times — verify-profiles.py:305's own note.
    param_ids = PARAM_IDS_H.read_text(encoding="utf-8")

    lanes = read_lane_order(profiles_module, param_ids)
    channels = read_channel_order(profiles_module, param_ids)
    step_windows = read_step_windows(param_ids)
    profiles = read_profiles(profiles_module)

    # The composite channel is the one no lane carries the id of — derived the
    # same way VoiceEngine::compositeChannel() derives it, so this script cannot
    # disagree with the C++ about which lanes nest under a channel.
    composites = [c for c in channels if c not in lanes]
    if len(composites) != 1:
        fail(f"expected exactly one composite channel, found {composites}")
    composite = composites[0]

    cases = []
    for profile_id in profiles:
        for steps in step_windows:
            for bpm in BPMS:
                cases.append((f"{profile_id}/{steps}steps/{bpm}bpm", profile_id, steps, bpm, None))

    # One run per channel muted. `bateria` is among them, and it is the case
    # AC-2 names: it must remove BB, CX, HH and TOM together, because the four
    # are one channel with four lanes rather than four channels.
    for channel in channels:
        cases.append((f"caruaru/16steps/120bpm/mute:{channel}", "caruaru", 16, 120, channel))

    # Each of the three below reaches something the profile matrix cannot. They
    # are appended as whole documents rather than as (profile, steps, bpm) rows
    # because their grids are hand-built, not decoded from data.js.
    #
    # THE VELOCITY CLAMP. Every profile pattern decodes to at most 126, and
    # State::readFrom already clamps to State::kMaxVelocity on the persistence
    # path (src/ForroBoxState.cpp:39), so the matrix never walks either bound. It
    # is still reachable: the writer takes a plain `const State&` whose lanes are
    # a raw uint8_t array, and nothing in the type says 127.
    #
    # THE MULTI-BYTE VLQ. Every profile has a lane that fires on every step, so
    # no delta in the matrix exceeds 19 — and a delta-encoded stream is the
    # entire reason this check compares bytes. A writer that emitted the VLQ
    # groups least-significant-first is a no-op below 128 and passed every case
    # here until this one existed.
    #
    # THE STEP WINDOW. `State` keeps all 32 slots when the user narrows to 16
    # (src/ForroBoxState.h, "narrowing merely stops reading the upper half"), so
    # a writer that ignored `steps` would export a second bar the user never
    # hears. Every matrix case supplies a lane exactly `steps` long, which cannot
    # tell the two apart; this one stores velocities BEYOND the window.
    extras = [
        ("velocity-clamp", clamp_grid),
        ("sparse-multibyte-vlq", sparse_grid),
        ("beyond-window", beyond_window_grid),
    ]

    problems = []

    documents = [
        (name, make_state(profiles, lanes, channels, composite, profile_id, steps, bpm, muted))
        for name, profile_id, steps, bpm, muted in cases
    ]
    documents += [
        (f"{name}/16steps/120bpm",
         unmuted_state(channels, build(lanes, channels, composite)))
        for name, build in extras
    ]

    payload = [json.dumps(state) for _, state in documents]

    expected = run([node, str(VERIFY_JS)], payload, "verify-midi.js")
    actual = run([exe, "--emit-midi"], payload, "ForroBoxTests --emit-midi")

    for (name, _), want, got in zip(documents, expected, actual):
        if want != got:
            problems.append(
                f"  {name}\n{first_divergence(bytes.fromhex(want), bytes.fromhex(got))}")

    print(f"states compared:   {len(documents)}")
    print(f"mismatches:        {len(problems)}")

    if problems:
        # Flushed first, or the summary lands after the detail when the two
        # streams are buffered differently — which is how the reader sees the
        # offsets before knowing how many states they came from.
        sys.stdout.flush()
        print(f"\n{len(problems)} state(s) diverged:", file=sys.stderr)
        for p in problems:
            print(p, file=sys.stderr)
        return 1

    print("\nOK — the C++ writer and the prototype's exportMIDI agree byte for byte")
    return 0


if __name__ == "__main__":
    sys.exit(main())
