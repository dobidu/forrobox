---
phase: 08-polish
plan: 03
subsystem: ui
tags: [juce, vst3, fonts, fonttools, typography, css-font-matching]

requires:
  - phase: 04-ui-shell
    provides: "the offline variable-font instancer and its reproducibility machinery"
  - phase: 08-polish
    provides: "08-02's settings store, its declared displayFont entry and its free menu band"
provides:
  - "Three selectable display fonts, resolved through a family x weight table"
  - "A font-coverage gate in verify-charset.py: all 36 drawn characters, every embedded file, every build"
  - "One fewer embedded weight: Face::monoSemiBold had no reader for four phases"
affects: [any plan adding a font or a user-selectable asset]

tech-stack:
  added: [JetBrains Mono, Space Mono]
  patterns:
    - "A gate belongs where the thing it checks against is owned, and enrolled where it runs every build"
    - "A font-matching gap is resolved the way the prototype's browser resolves it"

key-files:
  created: []
  modified: [scripts/build-fonts.py, src/Typography.h, src/Typography.cpp, src/Settings.h, src/Chassis.cpp, CMakeLists.txt]

key-decisions:
  - "Space Mono's 500 draws at its 400, because CSS Fonts 4 §5.2 walks down — the prototype's own answer"
  - "The typeface cache is keyed by (family, face), not cleared on change"
  - "Face::monoSemiBold deleted: embedded and registered, asked for by nothing"
  - "The mono family is PUSHED into Typography, never pulled — it is a leaf the whole UI depends on"
  - "The font-coverage gate belongs in verify-charset.py, which owns the repertoire and is enrolled in CMake"

patterns-established:
  - "A resource table names its own key and asserts it on use"

duration: ~2h
started: 2026-09-22T01:10:00-03:00
completed: 2026-09-22T03:05:00-03:00
description: "The display font: three families selectable, the unused weight deleted, and the coverage gate given teeth"
type: Summary
about: "Forró Box"
---

# Phase 8 Plan 03: The display font — Summary

**The gear menu's fifth item works. IBM Plex Mono, JetBrains Mono and Space Mono are selectable and
the choice survives a restart. `Face::monoSemiBold` — embedded since 04-01, registered in the
resource table, asked for by nothing — is gone.**

## Performance

| Metric | Value |
|--------|-------|
| Tasks | 3 of 3 + 1 checkpoint, approved |
| Checks | 4082 → **4100** |
| Mutations | 4, each confirmed applied on disk before its result was read |
| `/code-review` | once, as the plan required — 9 findings, 7 fixed, 2 recorded |
| `/simplify` | 4 angles — 2 gates of mine that could not catch what they were for |
| Font payload | 766,024 → **955,708 bytes (+189,684)** |

## Acceptance Criteria Results

| Criterion | Status | Evidence |
|-----------|--------|----------|
| AC-1: built reproducibly, and they carry the glyphs this UI draws | **Pass** | `--verify` re-derives 13 files byte-identically; `verify-charset.py` proves all **9 embedded files** cover all **36** drawn characters, on every build |
| AC-2: the mono face resolves through the chosen family | **Pass** | Three families mutually distinguishable by ink mass; switching twice and back renders identically |
| AC-3: Space Mono's missing 500 resolves as a browser resolves it | **Pass** | Its monoMedium draws exactly as its monoRegular; the two families that publish a real 500 draw heavier |
| AC-4: the setting persists and applies like the other four | **Pass** | Choosing each redraws the chassis; returning restores it exactly; the band is bounded like its neighbours |
| AC-5: the unused weight is gone | **Pass** | Absent from the enum, both tables and `assets/fonts/` |

## Verification

| | Result |
|---|---|
| GCC 13 / Clang 18 / MSVC 2022 | **4100 / 4100**, real exit 0, 0 warnings from our sources |
| Cross-checks | all five, run explicitly |
| Font coverage | 9 embedded files x 36 characters, on every GCC/Clang build. **SKIPPED on MSVC** — `fontTools` is not on the Windows host — and that is sound: the check reads committed `.ttf` bytes that `MANIFEST.sha256` pins and that do not vary by platform. It prints the skip rather than passing silently |
| Font build | `--verify` and `--check` both clean, by their real exit codes |
| Install | hashes match, moduleinfo clean, `/mnt/d/VST3` |
| Checkpoint | approved |

## Decisions Made

| Decision | Rationale | Impact |
|----------|-----------|--------|
| Space Mono's 500 draws at its 400 | It publishes 400, 700 and their italics and is not variable. CSS Fonts 4 §5.2 resolves an unmatched 500 against {400, 700} by trying 500, finding nothing, then walking DOWN — so 400 is what the prototype's browser renders. The design source settles it, as it settled `.pad.beat` at 04-03 and the backdrop at 05-04 | Not a compromise; recorded at the table row |
| The cache is keyed by (family, face) | Keyed by face alone the first mono typeface built is returned for every family forever — switching changes nothing and switching back looks correct. Keying also means a family selected twice costs one construction, which clearing-on-change would not | Mutation F1 fires on it |
| The family is PUSHED into `Typography` | It is a leaf the whole UI depends on; pulling would put a file open behind every glyph, and 08-02 measured `Settings::get` at 12.4 µs | `applyStoredSettings` calls it beside the LookAndFeel's three setters |

## Deviations from Plan

| Type | Count | Impact |
|------|-------|--------|
| Success criterion wrong as written | 1 | The plan said the payload would be SMALLER. It is not |
| Files outside `files_modified` | 0 | |
| Deferred | 3 | Two from `/code-review`, one from `/simplify`; all in STATE.md with their named fix |

### The payload grows, and the plan said otherwise

`08-03-PLAN.md`'s success criteria said "the embedded font payload is SMALLER by one weight than
before". **766,024 → 955,708 bytes, +189,684.** Dropping `monoSemiBold` saved 140,216; the two new
families cost 329,900. "One fewer embedded weight" and "a smaller payload" are different claims and
they were conflated at planning. The count of WEIGHTS did fall, from seven to six.

## What the planning premise got wrong, and what caught it

The ROADMAP recorded at 08-02 that the type scale used `monoRegular`, `monoMedium` **and
`monoSemiBold` "across ~20 rows"**, making Space Mono — which can supply neither 500 nor 600 —
a hard trade-off needing a user decision between three bad options.

**Two of those three claims were false.** `Face::monoSemiBold` has ZERO readers: it is registered in
`Typography.cpp`'s resource table and no row of `typeSpecs` asks for it. So the setting needed two
weights, and the single genuinely missing one had an answer already determined by the design source.
Reading `typeSpecs` rather than trusting the note is what found it; the ROADMAP paragraph was
rewritten before this plan was drafted, so the plan would not inherit it.

## Instruments and checks that could not do their job

Four this plan, three of them mine.

| Thing | Why it could not work |
|---|---|
| The glyph-coverage "gate" | Three defects, found in three passes. `report_glyph_coverage` PRINTED and returned nothing, so no caller could consult it (`/code-review`). Made fatal, it then checked **10 of 36** characters — missing every typographic one including the mono truncation's ellipsis — over a font the user can now SELECT (`/simplify`). And it lived in `build-fonts.py`, which is enrolled in no CMake target and runs only on a networked font rebuild (`/simplify`). It is one check in `verify-charset.py` now, where the repertoire is owned and already pinned against `tests/UiTest.cpp` |
| My control of that gate | Run through `--check`, which is offline and never calls `build()`. It exited 0 and would have been recorded as a pass. Re-run through `--verify` it exits 1 and names every family |
| My swatch measurement | Sized from a default-constructed `Component`, so every ink mass came back `0.0` and the "distinguishable" check read three identical zeroes as a cache bug |
| My Space Mono check | Asserted the two weights were the same `Typeface::Ptr`. The cache holds one per (family, face), so they are distinct objects built from the same bytes. Renders are the observable claim, and comparing them is stronger |

And one gap the mutations found: **deleting `type::setMonoFamily` from `applyStoredSettings` went
undetected.** The families were proven distinguishable in one test and the store proven to round-trip
in another; a chassis that never pushed the choice would have passed both and drawn IBM Plex Mono
forever. Mutation F3 fires on the new check.

## Issues Encountered

| Issue | Resolution |
|-------|------------|
| `--verify` reported `IBMPlexMono-SemiBold.ttf: present but not produced by this script` | The file was still in `assets/fonts/` after being dropped from the build. The gate caught it; deleted |
| 08-02's tick-count check hardcoded four submenus | Now derived from `settings::infos.size()` — the right failure, but not one to fix by hand each time a setting lands |

## `/simplify` — two gates of mine that could not catch what they were for

Four angles. Three found overlapping structural problems; the efficiency pass measured everything
and recommended **no change to `src/`**.

| Fixed | Why it mattered |
|---|---|
| The coverage gate checked **10 of 36** characters, and ran on nothing | Promoting an advisory print to a build-breaking gate over a USER-SELECTABLE font made its set the working definition of "safe to ship" — and it missed every typographic character, including the `…` the mono truncation draws, plus 16 accented letters. `build-fonts.py` is also enrolled in no CMake target: it runs only when a human re-fetches fonts over the network. The gate moved into `verify-charset.py`, which already owns the repertoire AND pins it against `tests/UiTest.cpp`'s array, so it inherits that enrolment and runs offline on every build. Controlled: exit 1, naming every file |
| **Two embedded fonts had no licence check** | This plan embeds `JetBrainsMono-OFL.txt` and `SpaceMono-OFL.txt`; the test named the two that existed before. Forró Box would have distributed two families with nothing verifying their licence survived the build — in the plan whose headline is deleting an embedded resource with no reader. It loops all four now |
| `faceResources()`'s two mono rows were dead AND duplicated `monoResources()[0]` | Under a comment claiming the sans lookup "needs no special case" — the special case is five lines below, added by the same commit. The same "resource with no reader" shape as `monoSemiBold`, reintroduced twenty lines from its own postmortem. Now `sansResources()` sized to four, with `static_assert`s that the sans faces lead the enum |
| The coverage roster was hand-listed | A family added to the constants but forgotten in the tuple would have been a gate that could not fail for the family it was added for. Derived from what was built |
| Two doc comments documenting nothing | One `/** */` immediately followed by another, so Doxygen kept the second; and 08-02's AC-5 comment had been displaced onto THIS plan's new function by inserting between a comment and its subject |
| `postscript_prefix`, `nameOf`, a false `jassert` rationale | Derivable, callerless, and a claim that two things "agree today" when one is declared in terms of the other |

### Measured, and left alone

`/simplify`'s efficiency pass built HEAD separately and compared two real builds.

| Question | Measured | Verdict |
|---|---|---|
| `typefaceFor` gained a branch and a second index | **+0.06 ns** per call; **269 calls** per paint = 33 ns against a 4.43 ms paint | Free — the whole function is 0.05% of a paint |
| First paint in a newly-selected family | **+0.32 ms**, once; re-selecting a built family is a normal paint | One frame 7% longer after a menu click. This is what keying by family buys |
| Binary size | `.so` **+197,928 bytes (+1.35%)**, all `.rodata`; `dlopen` **1.169 → 1.161 ms** | No measurable load or scan cost. Code grew 2,556 bytes |
| Suite wall time | 3.351 → **3.380 s (+29 ms)** | The new TEST is 1.1 ms; the +48 ms is the four-line block in `testSettingsChangeTheChassis`, two thirds of it a pre-existing pixel helper |

Resident memory is the other side of keying by family: each newly visited family costs 300–650 kB and
returning to one costs nothing, so a user who tries all three holds ~0.9 MB on a ~28 MB baseline.
That is the price of never rebuilding a typeface, and it is the right trade.

## Next Phase Readiness

**Ready:** Phase 8 has two scope items left — the `CACHAÇA` easter egg and the Ciclotron™ treatment.

**Concerns:**
- **The easter egg needs U+266A and no embedded family carries it.** The coverage gate reports this
  for all four and deliberately does not fail on it. `♪ NO PONTO` needs a fallback face, a drawn
  glyph, or different copy — a decision, not an implementation detail.
- The display font is process-global, so two open instances can render mixed (deferred, in STATE.md).
- `Segmented` caches its segment widths at construction, so a font switch leaves its hit regions at
  the previous family's metrics — measured at ~2% worst case (deferred, in STATE.md).

**Blockers:** None.

---
*Built with PAUL Framework v1.4 · https://chrisai.cv/skool*
*Phase: 08-polish, Plan: 03*
*Completed: 2026-09-22*
