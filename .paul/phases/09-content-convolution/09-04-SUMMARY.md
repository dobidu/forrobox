---
phase: 09-content-convolution
plan: 04
subsystem: content
tags: [grooves, content, audition, midi, checkpoint]

requires:
  - phase: 09-content-convolution
    plan: 03
    provides: "per-groove feel, without which a bank could not hold different rhythms"
provides:
  - "Sixteen grooves — four per regional profile"
  - "applyGroove's first caller, and the audition renderer per groove"
  - "32 audition artefacts: a .wav through the shipping chain and a .mid per groove"
  - "Asserted renders — finite, audible, unclipped, and the groove they are named for"

affects: [09-06, which makes the twelve new grooves selectable]

tech-stack:
  added: []
  patterns:
    - "A checkpoint artefact is asserted like a test, including that it IS what it claims"

key-files:
  created: []
  modified: [assets/profiles.json, src/Profiles.h, src/Profiles.cpp, src/Profiles.cpp,
             data.js, "Forró Box (standalone).html", tests/VoiceTest.cpp,
             tests/TestMain.cpp, tests/StateRoundTripTest.cpp, scripts/render-audition.sh]

key-decisions:
  - "Four per bank; eight of the sixteen names come from PLANNING.md:843's own list"
  - "The renderer was generalised, not written — Phase 3 already had one"
  - "The .mid is derived from the state the RIG rendered, not a second application"

patterns-established:
  - "A both-sides projection can hide inside a check built to catch substitution"

duration: ~2h
started: 2026-09-23T15:40:00-03:00
completed: 2026-09-23T18:30:00-03:00
description: "The content — twelve grooves drafted, rendered to audio and MIDI, and approved"
type: Summary
about: "Forró Box"
---

# Phase 9 Plan 04: the content — Summary

**Sixteen grooves, four per regional profile. Twelve are new, drafted from the four that exist and
from each profile's own description, rendered to 32 audition artefacts — a `.wav` through the
shipping chain and a `.mid` through 07-01's cross-checked writer — and approved at the checkpoint.
The four originals did not change a digit.**

This is the plan the user asked for at the outset: *"criar mais patterns por regional profile"*,
pointing at a cycler reading `PÉ-DE-SERRA 01` while CAMPINA GRANDE was selected. Three plans of
mechanism made a groove into one JSON object that six gates check; this one wrote the music.

## Performance

| Metric | Value |
|--------|-------|
| Tasks | 2 of 2 + 1 checkpoint, **approved** |
| Checks | 4496 → **4760** |
| Mutations | 6, each confirmed applied on disk before its result was read |
| `/code-review` | once after Task 2 — **6 findings, all 6 fixed** |
| Audition artefacts | **32** — 16 `.wav` + 16 `.mid`, all asserted |

## The banks

| Profile | Bank |
|---|---|
| CAMPINA GRANDE | PÉ-DE-SERRA 01 · BAIÃO SECO · XOTE LENTO · ARRASTA-PÉ |
| CARUARU | TRADICIONAL 01 · XAXADO 88 · BAIÃO PESADO · QUADRILHA |
| PETROLINA | FORRÓ ELÉTRICO · PISADINHA · XOTE ELÉTRICO · VAQUEJADA |
| UNIVERSITÁRIO | UNIVERSITÁRIO 01 · XOTE POP · FORRÓ POP · PÉ-DE-SERRA POP |

All eight labels from `PLANNING.md:843` are used, each assigned to the region whose own description
names that music. Six names were drafted: BAIÃO PESADO, XOTE ELÉTRICO, VAQUEJADA, XOTE POP,
FORRÓ POP, PÉ-DE-SERRA POP.

## Acceptance Criteria Results

| Criterion | Status | Evidence |
|-----------|--------|----------|
| AC-1: four banks of four, all checked | **Pass** | 128 patterns compared, 144 shape checks, 93 fields — all counts asserted. No two grooves in a bank share a name, an id or identical patterns |
| AC-2: the renderer plays the groove it names | **Pass** | 32 files, every render asserted. The hit count decoded from each `.mid` equals that groove's own velocity count — **and the check was rebuilt after a mutation proved the first version could not fail** |
| AC-3: `applyGroove` exists because something calls it | **Pass** | `applyProfile` delegates to it; behaviour identical, verified by the review |
| AC-4: the fingerprint moves once, deliberately | **Pass** | `0x313274a06c6ba3e1` → `0x53491866282c7b99`, re-pinned in the same commit. The four originals' digits are untouched — proved by `git diff`, because a digest cannot say which of its inputs changed |
| AC-5: the prototype still runs | **Pass** | `buildGroove` yields **231 hits**, unchanged — it reads `grooves[0]`, and no default groove moved |

## Verification

| | Result |
|---|---|
| GCC 13 / Clang 18 / MSVC 2022 | **4760 / 4760**, real exit 0 |
| Cross-checks | all six, run together |
| Checkpoint | **approved** — all sixteen grooves ship as drafted, names included |

## The check that could not fail, and how it was found

AC-2's whole purpose is that a render **is the groove it names** — sixteen files that all rendered
CAMPINA's default would be finite, audible and unclipped, and nobody would hear which across a
listening session.

The first implementation built the `.mid` by applying the groove to a **fresh** `State`, separately
from the rig. So `expectedHits` came from `groove.patterns` and the `.mid` came from `groove` as
well: **the same projection on both sides of the comparison, which can never disagree.** The
mutation proved it — pointing the rig at `defaultGroove()` for every groove passed clean, exit 0,
every check green.

The `.mid` is now derived from the state the rig actually rendered. The same mutation fails with
*expected 38, got 44*, and `/code-review` independently reproduced it at 12 of 16 renders.

**This is the same shape as 09-01's HIGH finding** — a lossy step applied to both sides — reproduced
inside the very check written to catch substitution. It is worth carrying: *a check that derives
both of its sides from one source proves only that the source equals itself.*

## The renderer was generalised, not written

`tests/VoiceTest.cpp:4893`'s `renderAuditionFiles` already rendered every profile to a `.wav`
through the shipping chain — `useShippedChain`, the real `CACHAÇA`, the limiter — behind
`--render-audition`. Phase 3 built it so a person could judge whether the grooves matched the
prototype. 09-04 pointed it at grooves and added the `.mid`.

**It also reproduces Phase 3's measured peaks exactly**: 0.5713 / 0.8059 / 0.8896 / 0.6687 against
the 0.571 / 0.806 / 0.890 / 0.669 recorded then — an instrument self-test that came for free.

## What `/code-review` found

Six findings, all fixed, and the two that mattered were both in the path Esmeraldo actually uses.

**A failed render reported `0 / 0 checks passed — OK` and exited 0.** The `stream == nullptr` and
`writer == nullptr` paths printed a message and `continue`d without recording a single check —
defeating the `reportSummary()` this same plan wired into `TestMain`, one function over.
Reproduced against a read-only directory.

**And my own comment was false.** It said *"the MIDI just written is read back"* while parsing the
in-memory `bytes`, never the file; both write calls returned `bool` and both were discarded. A full
disk would have written truncated WAVs and still reported everything green.

Also: `directory.createDirectory()` discarded its result; `applyProfile`'s contract comment was
orphaned onto `applyGroove`; `TestMain`'s comment still described four profiles; and
`scripts/render-audition.sh` — **the script Esmeraldo runs** — still described four profiles to
WAVs, never mentioned the `.mid`, and closed by telling the listener to expect something
*"MECHANICAL and loud"* because *"CACHAÇA and the character bus, limiter and master are not built
yet"*. All three shipped the same week that line was written, six phases earlier.

## Deviations from Plan

| Type | Count | Impact |
|------|-------|--------|
| Plan instruction wrong | 1 | The plan said bateria lanes stay silent for a profile that mutes bateria. CAMPINA's existing groove carries them, and 03-03 approved it — the existing convention won, and the drafts follow it |
| Bookkeeping | 1 | This SUMMARY was written after 09-05, because the checkpoint blocked UNIFY. 09-04's code therefore landed in commit `353eabd` under 09-05's message rather than its own |

## Notes for Future Plans

**09-06 makes these selectable.** Twelve of the sixteen grooves are reachable by no UI at all until
the top cycler lands; the audition files are how they were judged in the meantime, and they remain
the way to judge a revision without a build.

**`applyGroove` still records only `profile.id()` and clears `dirty`.** Applying
`campina/xote-lento` leaves a state that says "CAMPINA GRANDE, pristine", and `State` has no groove
field. 09-05 fixed the analogous hole for SLOTS by marking `dirty` on a switch; 09-06 must answer
the same question for grooves, and now has a precedent to follow or to argue against.

**Revising a groove costs one JSON edit and one re-pin.** The generator carries it to four
consumers, six gates check it, and the fingerprint moves — which is what it is printed for.
