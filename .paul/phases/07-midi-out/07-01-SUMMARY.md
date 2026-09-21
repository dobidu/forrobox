---
phase: 07-midi-out
plan: 01
subsystem: midi
tags: [juce, smf, midi, vlq, node, cross-check, gm-percussion]

requires:
  - phase: 02-sequencer-clock
    provides: the stored grid and the bpm/steps parameters the export reads
  - phase: 01-plugin-foundation
    provides: State, ids::lanes, ids::channelInfos, VoiceEngine::channelForLane
provides:
  - renderStandardMidiFile — State + bpm + steps + mute gate -> SMF bytes, type 0, PPQ 96
  - ChannelGate — a mute-only, per-CHANNEL gate, deliberately not ChannelSettings::audible
  - the fifth cross-check: the prototype's own exportMIDI, RUN under Node, compared byte for byte
  - ForroBoxTests --emit-midi — states as NDJSON in, hex out, batched
affects: [07-02-drag-out, 07-03-live-midi, 08-polish]

tech-stack:
  added: [node]
  patterns:
    - "A runnable design source is RUN, never transcribed — the same standing as data.js"
    - "A byte format is checked by BYTES and by the first diverging offset, never by an event count"
    - "A cross-check case that cannot distinguish the bug it names is a case with no teeth — assert it can"
    - "One process per side, not one per case: a gate nobody can afford is a gate that gets switched off"

key-files:
  created: [src/MidiExport.h, src/MidiExport.cpp, scripts/verify-midi.js, scripts/verify-midi.py, tests/MidiExportTest.cpp, tests/EmitMidi.cpp]
  modified: [CMakeLists.txt, tests/TestMain.cpp, tests/TestSuites.h]

key-decisions:
  - "The reference is RUN, not ported — verify-midi.js loads audio.js and calls its own exportMIDI"
  - "The export is the STORED GRID: swing and CACHAÇA are not arguments, so they cannot reach it"
  - "One opt-out flag for all five gates — a second one produced the interlock it was warned about"
  - "The metronome byte is a literal 24, not kPPQ/4: MIDI clocks are 24 per quarter whatever the PPQ"
  - "verify-midi is a CALL to forrobox_add_verify_target, not a hand-copy of it"

patterns-established:
  - "Prove a matrix case can distinguish its bug, and assert that property so the next edit cannot remove it"
  - "A test's oracle transcribes the spec with its line numbers; it never reads the subject's own constants"

duration: ~300min
started: 2026-09-20T23:05:00-03:00
completed: 2026-09-21T02:10:00-03:00
description: "The Standard MIDI File writer, cross-checked byte for byte against the prototype's own exportMIDI run under Node — and the review found two of my checks had no teeth and the gate cost 1351 ms a build"
type: Summary
about: "Forró Box"
---

# Phase 7 Plan 1: The Standard MIDI File writer — Summary

**A pure function from the stored grid to the bytes of a type 0, PPQ 96 Standard MIDI File, whose
output is compared byte for byte against the prototype's own `exportMIDI` — run as JavaScript, not
transcribed. The reviews found two cases of mine that could not fail, a multi-byte VLQ path verified
by nothing, and a gate costing three times every other check combined.**

## Performance

| Metric | Value |
|--------|-------|
| Duration | ~300 min |
| Tasks | 3 of 3 |
| Qualify | 3 PASS (Task 1 re-qualified after `/code-review`) |
| Checks | 3822 → **3861** (+39) |
| Compilers | GCC 13, Clang 18, MSVC 2022 — 0 warnings |
| Cross-checks | **5** green; the matrix covers 24 states |
| MIDI gate cost | 1351 ms → **92 ms** per build (93%) |

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: bytes match the prototype, every profile and both windows | **Pass** | 24 states, byte for byte, on every build |
| AC-2: muted channels excluded, BATERIA takes four lanes | **Pass** | Derived from `channelForLane`, not re-stated |
| AC-3: the format is what PLANNING specifies | **Pass** | Read back through `juce::MidiFile` against PLANNING's own line numbers |
| AC-4: the export is the STORED GRID, deliberately | **Pass** | Swing and CACHAÇA are not arguments; proved through a real processor |

## Accomplishments

- **The reference is run, not read.** `verify-midi.js` loads `audio.js` and calls
  `window.FB_AUDIO.exportMIDI`. A port would have been a second transcription — the thing the
  check exists to make unnecessary.
- **Eleven mutations, each proved to fail a named check**, and each re-proved after the `/simplify`
  refactors rather than assumed to survive them.
- **`juce::MidiFile::writeTo` was ruled out with evidence, not by assertion.** `juce_MidiFile.cpp:536`
  elides the status byte under running status; every note-on here shares `0x99`, so JUCE would drop
  it from all but the first. The files would parse identically and compare unequal.

## Files Created/Modified

| File | Change | Purpose |
|------|--------|---------|
| `src/MidiExport.h` | Created | `renderStandardMidiFile`, `ChannelGate` |
| `src/MidiExport.cpp` | Created | The writer; the GM map as one array of structs with a `static_assert` |
| `scripts/verify-midi.js` | Created | The prototype's own `exportMIDI`, batched over NDJSON |
| `scripts/verify-midi.py` | Created | The 24-state matrix, the first diverging offset |
| `tests/MidiExportTest.cpp` | Created | 21 assertion sites (39 checks, several in loops) against `PLANNING.md`'s line numbers |
| `tests/EmitMidi.cpp` | Created | `--emit-midi`, split out of `main()` |
| `CMakeLists.txt` | Modified | `ARGS`/`AFTER` on the shared gate function; the fifth gate |
| `tests/TestMain.cpp` | Modified | One dispatch line |
| `tests/TestSuites.h` | Modified | Two declarations |

## Decisions Made

| Decision | Rationale | Impact |
|----------|-----------|--------|
| `ChannelGate` is mute-only | `exportMIDI` reads `mute` and never solo. Folding in `ChannelSettings::audible` would make the C++ diverge from the thing it is compared against | Written into the header, because 07-02 will have `resolveChannelSettings()` in hand and would reasonably reach for `! audible` |
| The metronome byte is a literal 24 | MIDI clocks are 24 per quarter by definition, whatever the PPQ. `kPPQ / 4` equals it only because kPPQ is 96 | Raising kPPQ no longer silently rewrites a byte while looking like it keeps it in step |
| One opt-out flag, not two | `verify-theme`'s own comment: "a second opt-out flag would let one rot while the other looked healthy" | See Deviations — the second flag produced exactly that, within one revision |
| `verify-midi` is a call, not a copy | The function's comment already predicted what a hand-copied block costs; the copy had already dropped the UNC/`WORKING_DIRECTORY` warning | `ARGS`/`AFTER` keywords; the four existing calls unchanged |
| The `midiFile::` constants moved into the `.cpp` | No caller outside it, and the only other mention is a comment explaining why the tests must not use them — 02-04's rule | The header carries only what callers need |

## Deviations from Plan

### Summary

| Type | Count | Impact |
|------|-------|--------|
| Review fixes applied | 5 (`/code-review`) + 14 (`/simplify`) | Two were defects in checks; one was a build regression I introduced |
| Findings skipped, with reasons | 4 | One was factually wrong; three were correct as written |
| Files added beyond the plan | 2 | `tests/MidiExportTest.cpp`, `tests/EmitMidi.cpp` |
| Deferred | 3 | Logged below |

### Two of my own checks had no teeth

**BPM 137 could not tell rounding from truncation.** I chose it because it "does not divide 60000000
evenly" — true, and irrelevant: 437956.204 truncates and rounds to the same integer, so the case
would have passed against the bug it was written to catch. Replaced with **139** (remainder 0.676),
and `assert_bpm_can_see_rounding()` now fails the whole check if no BPM in the matrix has a remainder
of at least half. The next edit cannot quietly put a toothless one back.

**A bare `id:` search mislabelled campina's patterns `zabumba`.** It matched the CHANNEL list first,
then ran forward to the next `patterns:` block. Every byte still compared; only the case name in a
failure report was a lie. Fixed by reusing `verify-profiles.py`'s own `read_data_js`, which
brace-matches the literal and asserts key == id and the order against `PROFILE_ORDER`.

### `/code-review` — five findings, all real

1. **No BPM guard.** `llround(60000000.0 / 0)` is undefined, and `--emit-midi` reached it. Clamped;
   `--emit-midi` now rejects an out-of-range BPM; a check covers it.
2. **The multi-byte VLQ was verified by nothing.** Every profile has a lane firing on every step, so
   no delta exceeded 19 — and the delta encoding is the entire reason this check compares bytes.
   Reversing the VLQ group order passed all 22 states. Added a sparse case (delta 173) and a check.
3. **`steps` was never exercised.** Replacing the window with `kMaxSteps` passed everything. `State`
   keeps all 32 slots when narrowed, so that regression hands the user a second bar they never hear.
4. **The CMake gate downgraded to a warning** when Python was absent, while its own require flag read ON.
5. `bytes.fromhex` could raise instead of using the script's `fail()` path.

### The fix for (4) introduced the interlock it was warned about

`FORROBOX_REQUIRE_MIDI_CHECK` was a second opt-out flag over the same concern. `/simplify` found the
consequence: on a Python-less machine, `-DFORROBOX_REQUIRE_PROFILE_CHECK=OFF` — which used to be
enough — now **hard-failed configuration**, because the second flag still defaulted ON. Removed. One
flag gates all five checks, which is what `verify-theme`'s comment said in the first place.
`FORROBOX_TESTS=OFF` silently dropping the gate is now an explicit error for the same reason.

### The gate cost more than everything it sat beside

Measured from `.ninja_log`: **1351 ms**, against 446 ms for all four existing cross-checks combined
and 467 ms for the link it waits on. Cause: 48 process spawns for 24 states (Node 20.3 ms each,
`ForroBoxTests` 4.2 ms each) to do ~30 ms of work. Both sides now speak NDJSON and run once:
**92 ms**. A gate nobody can afford is a gate that eventually gets switched off, which is the failure
every comment in that file is about.

### Three claims of mine were false

`PLANNING.md:818` (BB's note 36 is at **:819**), `:811-820` for the GM table (**:815-822**), and
"twenty lines below" for a comment **66** lines away. Two state counts written into comments were
**removed rather than corrected** — a number that drifts goes false quietly.

### Findings skipped, with reasons

| Finding | Why not |
|---------|---------|
| Drop the `build-clang` fallback — "appears in no other file" | **Factually wrong.** It is a standing build directory across the project's plans and all three-compiler verification |
| Use `State::kMaxVelocity` for the writer's 127 | Wrong in principle. 127 is the MIDI wire ceiling; if `State`'s ceiling ever moved, this must not |
| Replace `append*` with `juce::MemoryOutputStream` | The writer patches the track length after the fact and returns a vector; the stream buys a copy, not a simplification |
| Give the two processor-backed tests a plain `State` | The setup is inert *because* the parameters are not arguments — which is the claim. Removing it weakens the demonstration for a measured 1 ms |

## Issues Encountered

| Issue | Resolution |
|-------|------------|
| The `/code-review` agent mutation-tested `src/MidiExport.cpp` **concurrently** and restored a pre-edit snapshot, silently reverting two fixes | Caught by re-reading the file, not by the build. Later passes were run with no overlapping edits |
| A build error, and the suite I ran straight after reported 3861 OK from the stale binary | Result discarded, error fixed, re-run. The exact trap the handoff warns about — twice this session |

## Deferred (logged to PROJECT.md)

- **`setValue`/`setChoice` belong in `fbtest`** (`tests/TestHarness.h`). The same three-line shape
  exists in `VoiceTest.cpp`'s `AudioRig`, `StateRoundTripTest.cpp`, `UiTest.cpp` and now here. Four
  files, outside this plan.
- **The shared `data.js` readers should be an importable module**, not reached through
  `importlib.spec_from_file_location` because the filename is hyphenated. That mechanism creates an
  unenforced invariant: `verify-profiles.py` must stay side-effect-free at import, with nothing to
  catch a regression.
- **MSVC emits 183 `MSB8064` dependency warnings** under a UNC source, across all five gate projects
  (9 from `verify-midi`). Pre-existing — 174 of them predate this plan. It means MSVC *incremental*
  rebuilds may not re-trigger the gates; clean builds run them.

## Next Phase Readiness

**Ready:** 07-02 has `renderStandardMidiFile` as a pure function and needs only a temp file, a
filename and `performExternalDragDropOfFiles`. The `ChannelGate` header comment tells it not to fold
SOLO in.

**Concerns:** The time signature's clocks-per-click byte is pinned only by the cross-check — there is
no `PLANNING.md` line specifying it, so no suite check cites one. If both sides were ever wrong the
same way there, nothing would catch it.

**Blockers:** None.

---
*Built with PAUL Framework v1.4*
*Phase: 07-midi-out, Plan: 01*
*Completed: 2026-09-21*
