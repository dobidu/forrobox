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
Phase: 2 of 8 (Sequencer clock) — Planning
Plan: 02-04 created, awaiting approval
Status: PLAN created, ready for APPLY
Last activity: 2026-09-07 — Created .paul/phases/02-sequencer-clock/02-04-PLAN.md (last plan in Phase 2)

Progress:
- Milestone: [█▌░░░░░░░░] 13% (1 of 8 phases)
- Phase 2: [███████▌░░] 75% (3 of 4 plans)

## Loop Position

Current loop state:
```
PLAN ──▶ APPLY ──▶ UNIFY
  ✓        ○        ○     [02-04 created, awaiting approval — LAST plan in Phase 2]
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
| ~~Pattern handover: double-buffer + atomic index~~ → **generation counter + reader snapshot** | 2 | **Superseded at 02-04 planning.** Traced: with two slots the writer's only remaining target after two publications IS the slot the audio thread is holding, and two publications inside one ~5 ms block are reachable (a profile reload that publishes then fixes up, a pad drag, `setStateInformation`). Nothing in that design enforced the spacing it depended on. The replacement: writer bumps a generation odd, writes staging, bumps even; the audio thread copies into a private 256-byte snapshot only when the generation changed, verifying before and after, and on a collision keeps the previous snapshot. Wait-free both sides, no retry loop, no torn read possible, and it copies nothing when nothing was published |
| `playing` is neither an APVTS parameter nor persisted state | 2 | Decided at 02-02 planning. `PLANNING.md`'s parameter-mapping list omits it, a play toggle on an automation lane fights the host transport, and a plugin that resumes playing when a project opens is hostile. Distinct from `dirty`/`activeProfile`, which are persisted |
| The clock is a plain class taking its tempo/swing/window as arguments, not reading the APVTS | 2 | Lets the timing be swept exhaustively offline with no processor, host or audio device — and lets 02-03 substitute the host playhead as the tempo source without touching the step maths |
| The clock's grid position and its next-step-to-emit are separate state | 2 | Conflating them let a deferred swung step drag the grid back by an amount computed at the old tempo, which then needed a per-call clamp — making placement depend on the host's buffer size |
| The clock is driven by a musical position and a rate; it holds no position | 2 | `PLANNING.md` requires alignment derived from absolute host PPQ each block, not a local counter. One code path for synced and free; host loops and jumps have nothing to contradict |
| Partition equality is: indices exact, positions within one sample | 2 | Bit-identity is unreachable once position-driven, and host sync requires that. Stated in the tests rather than quietly relaxed |
| ONE position watermark, owned by both paths | 2 | Two representations of one concept is what made a duplicate filter seem necessary; tiling spans make duplicates AND gaps impossible instead |
| Negative host positions accepted | 2 | A host count-in reports negative ppq and the groove should play through it on the same grid. Deliberate deviation from 02-03's plan |
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

### 02-04 design input — decided at 02-03 UNIFY

**`resetPending` stays a separate atomic.** The 02-02 question is answered, and the altitude review
changed my mind: it is a **consume-once command edge**, not latest-wins data publication. Folding it
into a snapshot forces either a generation counter the audio thread must write back — turning a
read-only publication into an RMW on every block — or dropped resets when two arrive between blocks.
The two-variable consistency it needs already exists: `setPlaying` releases `playing` after writing
`resetPending`, and `processBlock` acquires `playing` first.

**Where the snapshot pressure actually is:** `currentStep` and `emittedSteps` are independent relaxed
atomics that Phase 5's playhead and activity meter will read at frame rate, and read mutually
inconsistently. That is the real candidate for one published struct — and ROADMAP already names Phase
5's trigger FIFO as the mechanism. Logged against Phase 5, not 02-04.

**Recommended landing site for 02-04's pattern read:** once per block in `processBlock`, never per
step, handed down through a stack-allocated per-block emitter implementing `StepListener`. The
interface already supports that at zero cost. Specifically **do not** reintroduce a mutable-member
channel set before `advance` and read in the callback — 02-03 had one (`currentSegmentOffset`) and
`/simplify` removed it, because it made the clock's own documented offset contract false.

### Skill audit gap (Phase 2)

| Expected | Invoked | Notes |
|----------|---------|-------|
| `/graphify` | ✅ **closed** | Was skipped in 02-01 and 02-02 (done by hand with grep/sed) and the 02-02 plan wrongly claimed ✓. Invoked at 02-03 planning over `PLANNING.md`, `app.js`, `audio.js` and `data.js`: 178 nodes, 311 edges, graph in `graphify-out/` (gitignored). It earned its place — it surfaced the loop/jump/tempo-ramp spec gap and the quotation that settled the position-range decision |

### Phase-completion heuristic

`unify-phase.md` decides "last plan in phase" by comparing PLAN.md and SUMMARY.md counts. It mis-fired
twice in this phase while the counts matched at 2 and 2. Now that 02-03-PLAN.md exists the counts
differ again, so it reads correctly — but ROADMAP.md remains authoritative: Phase 2 has **4** plans
after the 02-03/02-04 split, and the transition is due only after 02-04 closes.

### 02-02 reconciliation

Recorded in `.paul/phases/02-sequencer-clock/02-02-SUMMARY.md`. What generalises beyond the plan:

| Lesson | Why it earned a rule |
|--------|----------------------|
| **Commit the verified state before running mutation controls; controls restore from the commit and must refuse to run on a dirty file.** | Three incidents this plan began by controlling uncommitted work, and the `git checkout` restore destroyed it — once requiring `advance()` to be reconstructed, once wiping six review fixes, once wiping a declaration mid-verification. The earlier rule ("restore from a backup copy") was simply wrong |
| **Run controls in a throwaway build directory.** | The control loop's silenced incremental rebuilds left `build-linux` with mixed objects, which then reported 418/437 on a clean, correct tree. A fresh build of the same commit gave 438/438 |
| **A fix for a review finding needs its own negative control.** | The clamp added to fix the swing-debt burst was itself block-size dependent — the exact property the class exists to guarantee — and no existing test could see it |
| **Verify a reviewer's premise before fixing on it.** | One HIGH finding was not reproducible: `AudioParameterChoice` snaps its range, so the raw value is always integral. And a measured "~26 s saved" was really 10.25 s |

### 02-03 reconciliation

Recorded in `.paul/phases/02-sequencer-clock/02-03-SUMMARY.md`. What generalises:

| Lesson | Why it earned a rule |
|--------|----------------------|
| **A fix can be asymmetric — check the mirror case before believing it.** | My duplicate filter caught the overlap case (a step emitted twice) and was blind to the gap case (a step never emitted), and no counter noticed the gaps. `/simplify` found it; `/code-review` had not |
| **A rule buried inside a function that needs a whole subsystem to reach is a rule that will be wrong.** | "Step 0 only locks to the bar in 4/4 starting at ppq 0" survived a full review pass because the anchoring could only be exercised by driving a processor through a fake host, and every 4/4 case agrees with the wrong formula. As a free function it is swept directly |
| **An assertion that cannot fail is worse than no assertion.** | Three shipped in this plan — two `check(true)` and one comparing `blocksRendered` to its own argument — each counting toward a green tally while proving nothing |
| **Measure before optimising, and before worrying.** | I assumed the test suite's cost was block rendering. It is processor construction: 72.5 µs per rig against 53–75 ns per block. And 35% of the suite was one pre-existing 800 KB string exercising a guard that rejects on length before decoding |

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
Stopped at: Plan 02-04 created — the last plan in Phase 2
Next action: Review and approve plan, then run /paul:apply .paul/phases/02-sequencer-clock/02-04-PLAN.md
Resume file: .paul/phases/02-sequencer-clock/02-04-PLAN.md

**Phase transition is due after 02-04 closes** — it is the last of Phase 2's four plans, so UNIFY must
run `transition-phase.md`: evolve PROJECT.md, mark Phase 2 complete in ROADMAP.md, commit the phase,
and route to Phase 3. Phase 3 planning is still blocked on the samples-vs-synthesised decision below.
Open items: (1) samples vs synthesised voices — settle before Phase 3 is planned; it does not block
Phase 2. (2) Vendor folder in Live reads `Forro Box` inside `Forro Box`; `COMPANY_NAME` is
display-only and safe to change.

---
*STATE.md — Updated after every significant action*
