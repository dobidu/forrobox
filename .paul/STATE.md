---
description: "Forró Box — current position and accumulated context"
type: ProjectState
about: "Forró Box"
---

# Project State

## Project Reference

See: .paul/PROJECT.md (updated 2026-09-06)

**Core value:** Producers get authentic, human-feeling Brazilian forró percussion grooves inside
their DAW without hiring a percussionist or programming every hit by hand.
**Current focus:** v0.1 Initial Release — Phase 2, Sequencer clock

## Current Position

Milestone: v0.1 Initial Release
Phase: 2 of 8 (Sequencer clock)
Plan: Not started
Status: Ready to plan
Last activity: 2026-09-07 — Phase 1 complete, transitioned to Phase 2

Progress:
- Milestone: [█▌░░░░░░░░] 13% (1 of 8 phases)
- Phase 1: [██████████] 100% — 3 of 3 plans, complete 2026-09-07

## Loop Position

Current loop state:
```
PLAN ──▶ APPLY ──▶ UNIFY
  ✓        ✓        ✓     [Phase 1 closed — ready to plan Phase 2]
```

## Accumulated Context

### Decisions

Full log in `.paul/PROJECT.md` → Key Decisions. Phase 1's most load-bearing, kept here because
Phase 2 builds directly on them:

| Decision | Phase | Impact |
|----------|-------|--------|
| Audio-thread contract: no allocation, locks or I/O in `processBlock` | 1 | Phase 2's clock inherits it; `/code-review` gates every processor change |
| 45 params in 6 groups; grid + profile as a `ValueTree` child, never automation | 1 | Phase 2 reads `bpm`/`swing`/`steps`/`sync` and the grid from these exact IDs |
| Grid lanes fixed at 32 slots; `steps` selects the active window | 1 | Phase 2's tiling operates on the window, not on storage |
| `LockedState` RAII handle is the only path to non-automatable state | 1 | Phase 2 must not hand the audio thread a reference through it — use a lock-free swap |
| Install target discovered from the host, never assumed | 1 | Ableton-specific today; `FORROBOX_VST3_DIR` for other hosts |
| ASCII display name; accented parameter/group names kept | 1 | Settled — do not reopen without an upstream JUCE fix |

### Deferred Issues

| Issue | Origin | Effort | Revisit |
|-------|--------|--------|---------|
| Prototype GR meter never updates (`refs.grFill` captured but never written in `app.js`) | Init | S | Phase 8 wires the real GR meter; fix the prototype only if used for A/B checks |
| Stubbed controls: `LOAD`, `LOAD IR…`, preset cycler, `PAT 01–08`, `MULTI-OUT` | Init | L | Post-v0.1 milestone; specs in PLANNING.md |
| ~~MSVC building from a UNC source path may be slow or fragile~~ | 1 | — | Measured during 01-03 planning: UNC reads are fast (30 KB in 22 ms, 303 files in 216 ms). Design settled — UNC source, local Windows build dir; mirror only as a fallback. An early probe failed only because the distro is `Ubuntu-24.04`, not `Ubuntu` |
| No `pluginval` or `wine` installed | 1 | S | Decided: Ableton Live 12 scan + instantiate is 01-03's load proof. Automated edge-case validation (state fuzzing, bus permutations) revisited in a later phase |
| `sync` made an automatable parameter although PLANNING.md's parameter-mapping list omits it (its global state table includes it) | 1 | S | Deliberate: user-facing toggle that must persist. Recorded as a spec deviation in 01-02 |
| ~~`getPatternState()` hands out a mutable reference~~ | 1 | — | Resolved during 01-02 UNIFY: replaced with the `LockedState` RAII handle. `/simplify`'s altitude agent judged the partial fix actively misleading rather than merely incomplete, which was the right call |
| Test harness duplicates `juce::UnitTest`/`UnitTestRunner`, including `expectWithinAbsoluteError` | 1 | M | Phase 2, when clock tests are added — a mechanical rewrite of 620 lines now risks silently dropping an assertion for no behavioural gain |

### Blockers/Concerns

Phase 1's resolved blockers are retired; their history is in the phase summaries.

| Blocker | Impact | Resolution Path |
|---------|--------|-----------------|
| No audio device guaranteed on WSL2 | Phase 3 voice auditioning | Standalone degrades gracefully; A/B listening happens on the Windows side, where the plugin now loads |
| 18 `MSB8064` warnings — MSBuild lowercases dependency paths against a case-sensitive filesystem | Windows incremental builds may misbehave | Not materialised (touch-and-rebuild did reconfigure). Mirror mode would avoid it; revisit if a stale Windows build is ever observed |
| Install discovery is Ableton-specific | Any other host | `FORROBOX_VST3_DIR` override, or generalise the strategy |
| MSVC output is not reproducible (PE build timestamp) | Hash comparison can validate a copy, never "is the install current" | Compare source state instead if staleness detection is ever needed |

### Sample library received 2026-09-07 — architectural decision needed

User supplied `D:\temp\forrobox\FORRO BOX SAMPLES.zip` (9.9 MB, 8 files, all 24-bit stereo).
Inspected read-only; extracted only to the session scratchpad, **not** into the project.

| File | Rate | Length | Kind |
|------|------|--------|------|
| `ZAB_LOW_01..04.wav` | 48 kHz | 0.265–0.498 s | 4 one-shot variants, single articulation |
| `TRIANGULO_BPM_90.wav` | 48 kHz | 2.000 bars @ 90 BPM | tempo-locked loop |
| `ZABUMBA_BPM_90_4_BARS_01.wav` | 48 kHz | 4.000 bars @ 90 BPM | tempo-locked loop |
| `GANZA 02 104.wav` | 44.1 kHz | 4.000 bars @ 104 BPM | tempo-locked loop |
| `PANDEIRO 01 104 DRY.wav` | 44.1 kHz | 4.000 bars @ 104 BPM | tempo-locked loop |

Contradicts the recorded decision "Synthesised voices first, samples optional later — no sample
library to ship yet". Four collisions with the designed architecture:

1. **Loops are not one-shots.** The product is a 16/32-step sequencer with per-step velocity, ghost
   notes, swing and `CACHAÇA` timing jitter. A fixed 4-bar performance cannot carry per-step
   velocity or ghost notes, and applying jitter to a loop is meaningless without slicing.
2. **Two source tempi (90, 104) and two sample rates (48k, 44.1k).** Combining them at a project
   tempo of 132 needs time-stretching; naive rate-shifting transposes, and a triângulo pushed +47%
   is a different instrument.
3. **Coverage is partial.** One-shots exist only for zabumba, and only a "LOW" articulation. Nothing
   for bateria (BB/CX/HH/TOM), no triângulo open/closed pair — and PLANNING.md calls the triângulo's
   velocity-split articulation "the instrument's defining behaviour".
4. Would activate two currently-stubbed controls: per-strip `LOAD` and the `BUNDLE: MINIMAL`
   indicator.

**Does not block 01-02.** PLANNING.md's sample path keeps the same 7 params per channel (`PITCH`
becomes playback rate or pitch shift, `DECAY` an envelope release), so the parameter surface is
identical either way and no rework follows from deciding this later. It does need settling before
Phase 3 is planned.

Samples currently live only in the session scratchpad, which is temporary. If they are to be kept
they need a durable home (`assets/samples/`) — deferred pending the decision.

## Boundaries (Active)

Phase 1 closed; its plan boundaries are retired. Project-wide constraints:

- `PLANNING.md`, `uploads/UIUX.md` — specification, read-only
- The HTML/CSS/JS prototype and `build_standalone.py` — design reference, must keep working
- `~/JUCE` — consumed read-only. The ASCII naming decision removed the only argument for patching it
- No new third-party dependencies beyond JUCE without an explicit decision
- `processBlock` stays allocation-free and lock-free — the contract Phase 1 established
- Parameter and group IDs are fixed; renaming one invalidates saved host state

## Session Continuity

Last session: 2026-09-07
Stopped at: Phase 1 complete and transitioned — plugin loads in Ableton Live 12
Next action: /paul:plan for Phase 2 (sequencer clock)
Resume file: .paul/ROADMAP.md
Open items: (1) samples vs synthesised voices — settle before Phase 3 is planned; it does not block
Phase 2. (2) Vendor folder in Live reads `Forro Box` inside `Forro Box`; `COMPANY_NAME` is
display-only and safe to change.

---
*STATE.md — Updated after every significant action*
