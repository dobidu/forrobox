---
phase: 02-sequencer-clock
plan: 01
type: Summary
about: "Forró Box"
description: "The four regional grooves ported from data.js as C++ tables, with a velocity decoder, 32-slot tiling, and a mechanical cross-check wired into the build so the tables cannot diverge from the design source unnoticed."
completed: 2026-09-07
---

# 02-01 Summary: Musical content

## What Was Built

The musical content of the product now exists in C++. `src/Profiles.h/.cpp` holds the four regional
profiles — campina, caruaru, petrolina, sp — each carrying its eight pattern lanes, its
bpm/swing/cachaça/timbre settings and its bateria mute state. `decodePattern` turns the design's
velocity notation into velocities, `expandPattern` tiles a 16-entry pattern across all 32 lane slots,
and `applyProfile` loads a profile into a `forrobox::State`.

The tables were **generated** from `data.js` rather than transcribed, and are **cross-checked** against
it by `scripts/verify-profiles.py` on every build that touches either side.

### Generated, not transcribed — and why that mattered

The first extractor produced a profile whose id said `triangulo` while carrying campina's data.
`CHANNEL_DEFAULTS` in `data.js` also has four-space-indented keys, so a non-greedy match started
inside the wrong object. Transcribing by hand would have produced exactly this class of error with
nothing to catch it. The extractor now brace-matches within the `PROFILES` literal and asserts each
key equals the `id` field it contains.

A second generation bug — missing commas between struct rows — was caught by
`-Wmissing-field-initializers`: C++ silently concatenated adjacent string literals and initialised
four members from one value.

### The cross-check is a build gate, not a script someone remembers to run

A wrong digit in a groove table produces no crash, no failed build and no failing test — just a
groove that is subtly wrong, with no way to know which digit. So the check is wired in as a build
dependency: `add_custom_command` with a stamp file and `DEPENDS` on its four real inputs
(`verify-profiles.py`, `data.js`, `Profiles.cpp`, `ParameterIDs.h`). It re-runs when any of those
change and costs nothing otherwise.

It compares 32 pattern strings and 32 fields (bpm, swing, cachaça, timbre, mute, and the three
identity strings per profile), reads its lane order from `ids::lanes` rather than repeating it, and
guards the parsed entry count against `ids::profileInfos` so a dropped row fails loudly rather than
shrinking the comparison set.

A missing Python 3 is a **`FATAL_ERROR`**, not a warning. A warning would have turned "cannot build
with diverged groove tables" into "verified only if Python happens to be installed" — the same
silent divergence the gate exists to prevent, relocated one layer up. `-DFORROBOX_REQUIRE_PROFILE_CHECK=OFF`
is the explicit opt-out.

Alongside it, a compile-time proof: each `Profile` *references* its `ids::profileInfos` entry rather
than copying it, so a row pointing at the wrong entry would compile cleanly and mislabel a whole
groove. `static_assert (profilesAlignWithInfos())` makes that unbuildable. The size assert alone
could not see it.

## Acceptance Criteria Results

| AC | Result | Evidence |
|----|--------|----------|
| AC-1 Velocity notation decodes as specified | ✅ | `.` → 0, `9` → 126; spaces cosmetic. Pattern-decoder test group |
| AC-2 All four profiles carry their scalars | ✅ | 32 fields compared against `data.js`; profile-scalars test group |
| AC-3 Pattern tables verbatim, proven mechanically | ✅ | 32/32 patterns match; 0 mismatches |
| AC-4 Tiling fills the whole lane (revised) | ✅ | `expandPattern` takes no step count; all 32 slots = `pattern[i mod 16]` |
| AC-5 Loading a profile fills every lane | ✅ | All 8 lanes filled; tiling-and-profile-load test group |
| AC-6 The audio thread is untouched | ✅ | `processBlock` still `ScopedNoDenormals` + `buffer.clear()` + `midi.clear()` |
| AC-7 Everything still builds and passes | ✅ | GCC, Clang and MSVC clean of our warnings; 287/287 tests under all three |

## Verification

- **Three compilers**: GCC and Clang (Ninja, Release) and MSVC 2022 through WSL interop. The only
  MSVC diagnostics are the known `MSB8064` lowercased-dependency-path warnings, now 22 rather than
  18 — exactly the four new `DEPENDS` paths, which incidentally confirms the dependency reached the
  MSBuild generator.
- **287/287 checks** pass under all three, across ten case groups.
- **11 mutation negative controls**, each asserting its mutation actually applied before the result
  was trusted: pattern digit, bpm, swing, cachaça, timbre index, bateria mute, identity code, lane
  reorder, `data.js` pattern, dropped entry (count guard), misaligned info (compile-time). All
  detected.
- **Stamp behaviour** confirmed both directions: zero re-runs on a no-op rebuild, one re-run after
  touching `data.js`.
- **Both gates negative-controlled**: a Python-less configure produces the `FATAL_ERROR`; the opt-out
  produces a loud warning and configures.
- **`data.js` unmodified** — `git diff --quiet` after every mutation test.
- Windows VST3 installed to the host-discovered `D:\VST3`, built and installed hashes matching,
  `moduleinfo.json` free of the accent corruption.

## Deviations From Plan

- **AC-4 was revised during execution.** As written it tied tiling to the step count. The state is
  always 32 slots wide and the step count is a *view* onto it, so `expandPattern` takes no step count
  and fills all 32. Tying storage to the window would have made a 16→32 switch reveal stale slots.
- **Simplify pass grew the plan's file list.** `CMakeLists.txt` and `scripts/build-windows.sh` were
  touched by the quality pass (the stamp, the Python gate, the install banner) beyond what the plan
  listed.

## Decisions Made

| Decision | Rationale |
|----------|-----------|
| Generate the C++ tables from `data.js`, never transcribe | The first extractor mislabelled a profile; a human transcription would make that error silently and permanently |
| The cross-check is a build dependency with a stamp | A check nobody runs is a comment claiming verification. The stamp keeps the guarantee without the per-build cost |
| Missing Python 3 fails configuration | A warning silently downgrades the guarantee to "if Python is installed" |
| `Profile` references its `ProfileInfo`, with a `static_assert` proving alignment | Identity lives once; the assert closes the hole that referencing (rather than copying) opens |
| `expandPattern` ignores step count | Storage is 32 slots; step count is a view |
| Assert every mutation applied before trusting a negative control | Three controls in this plan reported "not detected" for the wrong reason — the mutation never landed |

## Deferred

- **Extract `PROFILES` into a `profiles.json` consumed by both the prototype and the script.** The
  deeper fix for the parsing fragility, but it would modify `data.js` and the prototype — both
  read-only boundaries — so it needs its own decision rather than a quiet edit.
- **`MSB8064` (22×)** — MSBuild lowercases dependency paths on a case-sensitive filesystem. Cosmetic.
- **MSVC output is not reproducible** (PE timestamp), so a hash cannot answer "is the install
  current" across builds; it only verifies a copy within one build.
- **`juce::UnitTest` migration** re-deferred; the hand-rolled harness is still readable at 287 checks.

## Files Created / Modified

| File | Change |
|------|--------|
| `src/Profiles.h` | New — `Profile`, `decodePattern`, `expandPattern`, `allProfiles`, `findProfile`, `applyProfile` |
| `src/Profiles.cpp` | New — the four profiles generated from `data.js`, plus the alignment proof |
| `src/ParameterIDs.h` | `profileInfos` and `defaultProfile` added to the single source of truth |
| `scripts/verify-profiles.py` | New — mechanical `data.js` ↔ C++ cross-check |
| `tests/StateRoundTripTest.cpp` | Decoder, profile-scalar, tiling and profile-load groups (287 checks total) |
| `CMakeLists.txt` | Stamped cross-check dependency; `FORROBOX_REQUIRE_PROFILE_CHECK` gate |
| `scripts/build-windows.sh` | Install banner distinguishes "installed" from "would install" |

## Next

**02-02: Clock core** — sample-accurate step advance from the audio block's sample position, swing on
odd sixteenths, internal tempo. The tables now exist for it to advance over.
