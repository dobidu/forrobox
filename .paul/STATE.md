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
Phase: 2 of 8 (Sequencer clock) — In progress
Plan: 02-02 complete
Status: Loop closed on 02-02. Ready to plan 02-03
Last activity: 2026-09-07 — Closed 02-02: sample-accurate clock with swing and internal transport, 438 checks under three compilers

Progress:
- Milestone: [█▌░░░░░░░░] 13% (1 of 8 phases)
- Phase 2: [██████▋░░░] 67% (2 of 3 plans)

## Loop Position

Current loop state:
```
PLAN ──▶ APPLY ──▶ UNIFY
  ✓        ✓        ✓     [02-02 closed — see 02-02-SUMMARY.md]
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
| Phase 2 split into 3 plans: musical content, clock core, host sync + handover | 2 | Three unrelated concerns that fail in different ways; each independently testable |
| Pattern handover: double-buffer + atomic index | 2 | No allocation or deallocation ever on the audio thread. Outlives Phase 2 — Phase 6's profile reload uses it |
| `playing` is neither an APVTS parameter nor persisted state | 2 | Decided at 02-02 planning. `PLANNING.md`'s parameter-mapping list omits it, a play toggle on an automation lane fights the host transport, and a plugin that resumes playing when a project opens is hostile. Distinct from `dirty`/`activeProfile`, which are persisted |
| The clock is a plain class taking its tempo/swing/window as arguments, not reading the APVTS | 2 | Lets the timing be swept exhaustively offline with no processor, host or audio device — and lets 02-03 substitute the host playhead as the tempo source without touching the step maths |
| The clock's grid position and its next-step-to-emit are separate state | 2 | Conflating them let a deferred swung step drag the grid back by an amount computed at the old tempo, which then needed a per-call clamp — making placement depend on the host's buffer size |
| One test executable, both suites | 2 | A second executable re-compiled the whole JUCE module set (10.25 s and 19 MB per clean build), and a suite that is built but never run reports nothing while looking like coverage |
| Host sync is unit-testable offline via `AudioProcessor::setPlayHead()` | 2 | A fake playhead emitting scripted `PositionInfo` proves bar-locking with no DAW. Every `PositionInfo` field is `Optional<>` and must be handled as absent |

### Deferred Issues

| Issue | Origin | Effort | Revisit |
|-------|--------|--------|---------|
| Prototype GR meter never updates (`refs.grFill` captured but never written in `app.js`) | Init | S | Phase 8 wires the real GR meter; fix the prototype only if used for A/B checks |
| Stubbed controls: `LOAD`, `LOAD IR…`, preset cycler, `PAT 01–08`, `MULTI-OUT` | Init | L | Post-v0.1 milestone; specs in PLANNING.md |
| ~~MSVC building from a UNC source path may be slow or fragile~~ | 1 | — | Measured during 01-03 planning: UNC reads are fast (30 KB in 22 ms, 303 files in 216 ms). Design settled — UNC source, local Windows build dir; mirror only as a fallback. An early probe failed only because the distro is `Ubuntu-24.04`, not `Ubuntu` |
| No `pluginval` or `wine` installed | 1 | S | Decided: Ableton Live 12 scan + instantiate is 01-03's load proof. Automated edge-case validation (state fuzzing, bus permutations) revisited in a later phase |
| `sync` made an automatable parameter although PLANNING.md's parameter-mapping list omits it (its global state table includes it) | 1 | S | Deliberate: user-facing toggle that must persist. Recorded as a spec deviation in 01-02 |
| ~~`getPatternState()` hands out a mutable reference~~ | 1 | — | Resolved during 01-02 UNIFY: replaced with the `LockedState` RAII handle. `/simplify`'s altitude agent judged the partial fix actively misleading rather than merely incomplete, which was the right call |
| Extract `PROFILES` from `data.js` into a `profiles.json` consumed by both the prototype and the cross-check | 2 | M | The root fix for parsing `data.js` with regexes, raised by `/simplify`. Blocked on a boundary decision: it modifies `data.js` and the prototype, both read-only. Revisit if the extractor breaks again |
| Test harness duplicates `juce::UnitTest`/`UnitTestRunner`, including `expectWithinAbsoluteError` | 1 | M | **Re-deferred at Phase 2 planning**, overriding the earlier "revisit in Phase 2" note: clock tests fit the existing harness as-is, and a 620-line mechanical rewrite mid-phase risks silently dropping coverage for no behavioural gain. Revisit as a dedicated cleanup when nothing else is in flight |

### 02-03 design input — decided at 02-02 UNIFY, must be settled when planning 02-03

`/simplify`'s altitude review argues that `Clock::advance (numSamples, ...)` is the wrong driving
direction for host sync, and the argument is convincing enough to record rather than rediscover:

Under `AudioPlayHead` the authoritative statement is not "advance by 1024 samples" but "this block
spans ppq *P* to *P′* — emit every sixteenth in that range". The host can jump, loop and scrub, which
the clock's current internal position (`gridPhase`, `nextStep`) has no way to accept. Bolting sync on
means either a re-anchor entry point or a `syncMode` flag in `Params` — a second mode sharing only
the swing and rounding code with the first.

The proposed shape:

```
advance (double startPositionInSteps, double endPositionInSteps, int numSamples,
         const Params&, StepListener&)
```

- internal tempo: the processor computes `end = start + numSamples / stepSamples`
- host sync: the processor computes `start = ppqPosition * 4`, `end` from the next block

One mode, no re-anchor special case, host jumps and loops fall out for free, and it moves the tempo
*source* fully to the processor side — which the `Params`-by-value decision already established, and
which `Clock` currently half-owns by computing `stepSamples` from `bpm` itself.

Not applied in 02-02: it would rewrite the API that plan had just specified and verified. **02-03
should decide this before writing code**, because the cost rises once Phase 3's voices depend on the
emission semantics. A related deferral: 02-03's pattern handover should probably subsume
`resetPending` into one published transport snapshot rather than adding a second bespoke atomic.

### Skill audit gap (Phase 2)

| Expected | Invoked | Notes |
|----------|---------|-------|
| `/graphify` | ○ | Required by SPECIAL-FLOWS before planning a phase and for `PLANNING.md`/prototype lookups. Done by hand with grep/sed in both 02-01 and 02-02. The 02-02 plan's skills table wrongly claimed ✓; corrected. **Invoke it when planning 02-03** — host sync needs the `SYNC` behaviour spec and `AudioPlayHead` expectations pulled out of the handoff |

### Phase-completion heuristic does NOT apply here

`unify-phase.md` decides "last plan in phase" by comparing PLAN.md and SUMMARY.md counts in the phase
directory. Phase 2 currently has 2 of each, so the heuristic reads as complete and would trigger a
phase transition. It is wrong: ROADMAP.md is authoritative and Phase 2 has **3** plans, with 02-03
(host sync + lock-free handover) not yet written. No transition. This is the second time the heuristic
has mis-fired in this phase — it will read correctly only once 02-03 has both files.

### 02-02 reconciliation

Recorded in `.paul/phases/02-sequencer-clock/02-02-SUMMARY.md`. What generalises beyond the plan:

| Lesson | Why it earned a rule |
|--------|----------------------|
| **Commit the verified state before running mutation controls; controls restore from the commit and must refuse to run on a dirty file.** | Three incidents this plan began by controlling uncommitted work, and the `git checkout` restore destroyed it — once requiring `advance()` to be reconstructed, once wiping six review fixes, once wiping a declaration mid-verification. The earlier rule ("restore from a backup copy") was simply wrong |
| **Run controls in a throwaway build directory.** | The control loop's silenced incremental rebuilds left `build-linux` with mixed objects, which then reported 418/437 on a clean, correct tree. A fresh build of the same commit gave 438/438 |
| **A fix for a review finding needs its own negative control.** | The clamp added to fix the swing-debt burst was itself block-size dependent — the exact property the class exists to guarantee — and no existing test could see it |
| **Verify a reviewer's premise before fixing on it.** | One HIGH finding was not reproducible: `AudioParameterChoice` snaps its range, so the raw value is always integral. And a measured "~26 s saved" was really 10.25 s |

### 02-01 reconciliation

Recorded in `.paul/phases/02-sequencer-clock/02-01-SUMMARY.md`: the deviations table (tables
generated rather than transcribed; AC-4 revised mid-flight), all 10 `/code-review` findings, and the
`/simplify` pass. Two lessons carried forward as project practice:

| Lesson | Why it earned a rule |
|--------|----------------------|
| **A green check that never asked the right question is not evidence.** | The cross-check reported 32/32 OK while comparing both sides against the same hand-copied lane list, and while never comparing display names at all |
| **Assert a mutation actually applied before trusting a negative control.** | Three controls in 02-01 and four more during the `/simplify` verification reported "not detected" purely because the `sed` never matched. One failed for the *same* reason a real bug had: `CHANNEL_DEFAULTS` also has a `zabumba:` key, so a `0,/zabumba:/` range ended on the wrong line |

A third, narrower one: **do destructive negative controls from a backup copy, not `git checkout`.**
Restoring a mutated file with `git checkout` during the `/simplify` verification silently discarded
two uncommitted fixes from that same pass, which then had to be re-applied.

### Blockers/Concerns

Phase 1's resolved blockers are retired; their history is in the phase summaries.

| Blocker | Impact | Resolution Path |
|---------|--------|-----------------|
| No audio device guaranteed on WSL2 | Phase 3 voice auditioning | Standalone degrades gracefully; A/B listening happens on the Windows side, where the plugin now loads |
| 22 `MSB8064` warnings — MSBuild lowercases dependency paths against a case-sensitive filesystem | Windows incremental builds may misbehave | Not materialised (touch-and-rebuild did reconfigure). Count went 18 → 22 in 02-01, exactly the four new `DEPENDS` paths. Mirror mode would avoid it; revisit if a stale Windows build is ever observed |
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
Stopped at: 02-02 loop closed — `/code-review` (9 findings) and `/simplify` (4 agents) both applied, three compilers green at 438 checks, committed
Next action: Run /paul:plan for 02-03
Resume file: .paul/phases/02-sequencer-clock/02-02-SUMMARY.md
Open items: (1) samples vs synthesised voices — settle before Phase 3 is planned; it does not block
Phase 2. (2) Vendor folder in Live reads `Forro Box` inside `Forro Box`; `COMPANY_NAME` is
display-only and safe to change.

---
*STATE.md — Updated after every significant action*
