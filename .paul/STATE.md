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
**Current focus:** v0.1 Initial Release — Phase 3, Voices & mix bus

## Current Position

Milestone: v0.1 Initial Release
Phase: 3 of 8 (Voices & mix bus)
Plan: Not started
Status: **Phase 2 complete.** Ready to plan Phase 3 — but see the blocker below
Last activity: 2026-09-08 — Phase 2 complete (4/4 plans), transitioned to Phase 3

Progress:
- Milestone: [██▌░░░░░░░] 25% (2 of 8 phases)
- Phase 3: [░░░░░░░░░░] 0% (not started)

## Loop Position

Current loop state:
```
PLAN ──▶ APPLY ──▶ UNIFY
  ○        ○        ○     [Phase 2 closed and transitioned; Phase 3 not started]
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

### Phase 3 design input — from 02-04's altitude review

**`BlockEmitter` is the right adapter but the wrong owner for voices.** Three things break its
per-block lifetime, and they are Phase 3's core features:

- **`StepEvent::sampleOffset` is currently unused** — the emitter reads only `event.step`. Rendering
  at the offset is Phase 3's whole job, so the one field it needs is the one field never exercised.
- **`CACHAÇA` timing jitter outlives the block.** A jittered or ghost note can land after the end of
  the block that scheduled it. A stack object destroyed at block end has nowhere to carry "this
  trigger fires 3 ms from now" — that needs a persistent scheduled-trigger queue.
- **RNG, gain/pan smoothers, the character bus and the limiter are all `prepareToPlay` lifetime.**
  None can hang off a block-scoped object, and an RNG rebuilt per block would either reseed or need a
  member — reintroducing the mutable-member channel `/simplify` removed twice.

**The shape to build:** a persistent `VoiceEngine` member, prepared with sample rate and block size,
owning voices, RNG, the jitter queue, smoothers, character bus and limiter. `BlockEmitter` stays a
thin per-block adapter translating `StepEvent` + lane velocities into
`engine.schedule (lane, velocity, sampleOffset)`; the engine drains and renders once per block after
`clock.advance`. That keeps the no-mutable-member property while giving the DSP a lifetime that
matches it.

**The path of least resistance is the wrong one:** the emitter already takes references, so adding
voice references and calling DSP from inside `stepTriggered` will look natural. That puts synthesis
inside the clock's callback interleaved with step placement, leaves cross-block jitter nowhere to
live, and is the same "accumulated two concerns that were not its own" shape 02-03's review already
removed once.

### Phase 5 design input — the audio→UI channel

`currentStep`, the packed lane velocities and `emittedSteps` are separate atomics. The step and its
velocities are *ordered* (release/acquire) and the eight velocities are group-atomic among themselves
(one 64-bit word), but the step and the velocities are **not** group-atomic: the audio thread can fire
the next step between a reader's two loads. Fixing that needs one published struct, which is Phase 5's
trigger FIFO. Deliberately not half-built in Phase 2 — an accessor promising consistency it cannot
deliver is worse than one that does not promise it.

### Superseded: 02-04 design input from 02-03 UNIFY

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

### Skill audit (Phase 2) — closed

| Expected | Invoked | Notes |
|----------|---------|-------|
| `/graphify` | ✅ **closed** | Was skipped in 02-01 and 02-02 (done by hand with grep/sed) and the 02-02 plan wrongly claimed ✓. Invoked at 02-03 planning over `PLANNING.md`, `app.js`, `audio.js` and `data.js`: 178 nodes, 311 edges, graph in `graphify-out/` (gitignored). It earned its place — it surfaced the loop/jump/tempo-ramp spec gap and the quotation that settled the position-range decision |

### Phase-completion heuristic — resolved

`unify-phase.md` decides "last plan in phase" by comparing PLAN.md and SUMMARY.md counts. It mis-fired
twice mid-phase while the counts happened to match at 2 and 2, and ROADMAP.md was authoritative both
times. It now reads correctly and agrees: 4 plans, 4 summaries, Phase 2 complete and transitioned.

### 02-04 reconciliation

Recorded in `.paul/phases/02-sequencer-clock/02-04-SUMMARY.md`. What generalises:

| Lesson | Why it earned a rule |
|--------|----------------------|
| **A guarantee with no production callers is not a guarantee.** | `lockPatternState()` published automatically on release — and both production state methods bypassed it, so the property held at neither of its two real sites. Only tests exercised it |
| **Prefer making a bug unrepresentable over testing for it.** | The "no block reads two tables" property needed a probabilistic race detector with a measured threshold. Giving the emitter the LANES instead of a refreshable reader deleted the counter, the accessor, the per-step audio-thread load and the flaky test |
| **An assertion that conflates two causes cannot see either.** | "Some refreshes give up rather than retrying" counted failures, but a refresh also returns false when nothing is new — so a reader changed to *block* still passed it. Contention had to be counted separately |
| **Check whether the framework already recommends the primitive, not just whether it ships one.** | My "JUCE has no value-swap utility, so hand-roll a seqlock" was right on the check and wrong on the conclusion: `juce_Convolution.h` names `SpinLock`/`GenericScopedTryLock` for exactly this job. The hand-roll cost 30x the copy time and carried a session-long stall mode |
| **Measure the claim in the comment.** | I wrote that relaxed atomic bytes "cost nothing". Measured: 57.8 ns against 1.92 ns for the memcpy a lock allows |

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
Stopped at: **Phase 2 complete and transitioned.** 4 of 4 plans closed; PROJECT.md evolved, ROADMAP
updated, phase committed
Next action: `/paul:plan for Phase 3` — **but settle the samples-versus-synthesised decision first**
(below). Phase 3's goal is A/B listening against the prototype, and which engine it builds is the
first thing its plan must state
Resume file: .paul/ROADMAP.md
Open items: (1) samples vs synthesised voices — settle before Phase 3 is planned; it does not block
Phase 2. (2) Vendor folder in Live reads `Forro Box` inside `Forro Box`; `COMPANY_NAME` is
display-only and safe to change.

---
*STATE.md — Updated after every significant action*
