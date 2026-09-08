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
Phase: 3 of 8 (Voices & mix bus) — Planning
Plan: 03-03 created, awaiting approval
Status: PLAN created, ready for APPLY. Last plan in Phase 3 — closing it triggers the transition
Last activity: 2026-09-08 — Created .paul/phases/03-voices-mix-bus/03-01-PLAN.md

Progress:
- Milestone: [██▌░░░░░░░] 25% (2 of 8 phases)
- Phase 3: [██████▋░░░] 67% (2 of 3 plans)

## Loop Position

Current loop state:
```
PLAN ──▶ APPLY ──▶ UNIFY
  ✓        ○        ○     [03-03 created, awaiting approval]
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
| Phase 3 engine: **hybrid** — sampled zabumba, seven synth lanes | 3 | Decided at Phase 3 planning, superseding "synthesised voices first, samples optional later" (recorded when no library existed). The library covers exactly one lane; everything else it contains is a tempo-locked loop |
| Phase 3 split into 3 plans: voices → humanisation → mix bus | 3 | Six ROADMAP concerns, and they fail in different ways: synthesis is spectral, humanisation is statistical, the bus is gain-staging. Each gets its own verification method |
| The `VoiceEngine` is a persistent processor member; `BlockEmitter` stays a thin per-block adapter | 3 | From 02-04's altitude review. `CACHAÇA` jitter can place a trigger after the block that scheduled it, and RNG, smoothers, bus and limiter are all `prepareToPlay` lifetime. The path of least resistance — calling DSP from inside `stepTriggered` — is the shape 02-03's review already removed once |
| `CACHAÇA`'s bipolar jitter is bought with a FIXED 32 ms reported latency | 3 | Decided at 03-02 planning. A hit jittered early must sound before its step, and that block is already rendered — the only alternatives were one-sided jitter (which makes the groove drift late and halves the humanisation range) or a knob-scaled latency (which forces a host re-negotiation mid-session). 32 ms, not 22: a ghost's ±10 ms is applied on top of the step's ±22 ms |
| Stochastic behaviour is tested as a distribution AND as a seeded exact render | 3 | Asserting specific seeded values pins the draw order and breaks on any refactor while letting a wrong distribution through — the shape that produced 02-04's flaky detector. Distribution tolerances are measured or computed from the binomial standard error, never guessed |
| The output stage is a `MixBus` SIBLING of the engine, not part of it | 3 | Decided at 03-03 planning, **overriding** STATE's original Phase 3 design input, which had the engine owning the character bus and limiter. The engine is about voices — a pool, per-voice state, per-lane keys — and the bus is one global stage with no per-voice anything. 03-02 having reduced `processBlock` to a single `engine.render` call site is what makes a second stage safe to add |
| `tanh` applied directly, not through Web Audio's 1024-point clamped table | 3 | The table clamps beyond +/-1, so at drive 1.2 an input of 2.0 yields tanh(1.2) = 0.834 against a direct 0.984 — and the grooves peak at 1.454, so inputs do exceed 1. A hard ceiling at an arbitrary input level is a table artefact, not intent, and PROJECT.md's rule is that correct plugin practice wins. Listen for it at the A/B step |
| The character bus lowpass keeps Web Audio's default Q of 1.0 | 3 | `createBiquadFilter()` never has its Q set in the sketch, so it is 1.0, not Butterworth 0.707. The resonant lift near cutoff is the spec, and "fixing" it would be a silent deviation |
| Audio claims are proved by offline render + measurement, not by listening | 3 | No audio device is guaranteed on WSL2, and "is it silent" passes for a wrong-but-audible voice. Onset positions, band energy and duration are measured; listening is a separate human-verify checkpoint |

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

### 03-01 reconciliation

Recorded in `.paul/phases/03-voices-mix-bus/03-01-SUMMARY.md`. **33 negative controls, 30 detect.**
What generalises:

| Lesson | Why it earned a rule |
|--------|----------------------|
| **The meta-check needs its own scrutiny most of all.** | The seventh assertion in this project that could not fail was the one validating the allocation counter. It dropped the `volatile` escape `ClockTest` documents as necessary, and it called `check()` between the two counter reads — `juce::String` has no small-string optimisation, so the literal allocated and the assertion passed on its own description string |
| **A test can be structurally blind to a 4.7 dB error while looking straight at it.** | Peak-versus-RMS normalisation left the whole suite green. Velocity spans 18 dB across the sampled points and the crossfade smears the inter-layer error across neighbours, so the monotonicity ramp stayed monotonic while every layer sat at the wrong level. Assert the property, not a downstream symptom |
| **Test a time-varying property over time.** | Forcing the frequency sweep off left every check green: bb's loud band is 45–150 Hz and its *start* frequency of 130 Hz is inside it. And the obvious repair is wrong — bb has 8× more energy near the sweep start than its end, because the envelope decays while the frequency falls. Comparing which frequency dominates early against late gives 16×/33× margins |
| **A no-op mutation needs a tripwire, not a test.** | Three fixes (per-slot rate, per-voice channel, mono pan law) are unobservable with the shipped assets — all four files are 48 kHz and lane 0 is the only sampled lane. Each now has an invariant assertion that fires when its precondition changes, and each tripwire was itself controlled |
| **Cross-check the spec table against the sketch.** | `PLANNING.md`'s Voice Specifications omit the triângulo's `0.5v` outer envelope, give the wrong envelope floor, and do not say that `pitchFactor` is applied PER COMPONENT — which would have been wrong for five of the eight lanes |
| **Parallel review agents need read-only scope.** | The efficiency agent wrote timing instrumentation into the repo rather than the scratchpad; the reuse agent then reported it as a finding about my code. Acting on it would have been a self-inflicted change justified by another agent's scratch work |
| **Three of eight efficiency findings were "looks expensive, measures free".** | Iterating all 176 voices per block is 0.112 µs; per-voice pan gains 3.98 ns/call and already skipped for inactive voices; the sampler's per-read bounds check differs by 0.03 ns. Measuring first is what kept three needless optimisations out |

**Measured wins:** the engine's render cost fell 74% by carrying the triângulo's sin/cos forward by
rotation instead of recomputing them (107.7 → 10.9 ns/sample; 65.9% → 7.1% of budget when saturated),
verified equivalent to −119.9 dB. Suite wall time 5.22 s → 0.95 s, most of it one hoisted rig: JUCE's
APVTS-driven timer thread was being torn down and recreated 1024 times.

### 03-02 reconciliation

Recorded in `.paul/phases/03-voices-mix-bus/03-02-SUMMARY.md`. **39 negative controls, 39 detect.**
What generalises:

| Lesson | Why it earned a rule |
|--------|----------------------|
| **A property held by discipline will be broken one level down.** | Muting a channel re-timed another because the humanisation draws were conditional. Drawing unconditionally fixed it — and missed that `SynthVoice::trigger` drew five more values for one lane, after the gate, only for a claimed voice. Keying every value on (seed, step, lane, purpose, index) made it structural: a value is a function of its key, so gating and draw order cannot reach it |
| **A comment asserting an invariant is where to look for the invariant's exception.** | The header claiming "a FIXED number of draws per lane per step" listed the triângulo's detune as part of that stream in the sentence above — two adjacent sentences contradicting each other |
| **A design note written to a future self must be deleted by that self.** | `scheduleStep`'s doc still ended with "NOT yet solved, and 03-02 must… offsets are clamped at zero" while describing this plan's own shipped fix |
| **A build failure is not a detection.** | A control for the per-lane jitter bug reported `detected (build failed)` and was recorded as detected. The mutation never compiled, so it never ran — 02-01's rule about invalid patches, applied to anchors while a compile error went through |
| **One unconditional tail beats N conditional ones.** | Three `engine.render` call sites would have let 03-03's limiter be added to the normal path only, leaving the ring-out after Stop +3.5 dB louder and unlimited. Scheduling now returns early freely; rendering happens once |
| **Measure a design input over a distribution, not a realisation.** | The limiter's input was 1.336 from one draw; over 36 realisations it is 1.454. The seeds were private with no injection point, so no test could sample it — the seam had to be built before the number could be trusted |
| **A measurement instrument needs its own proof.** | Nine measurement errors across two plans. `TestHarness.h` already had the pattern in `checkAllocationCounterRegisters` and it was applied to one instrument in ten. Self-testing the other nine immediately found that `bandEnergy`'s semitone grid never samples 1000 Hz |

### Phase 5 design input — the 32 ms UI lead

**Phase 3 chose a fixed 32 ms reported latency, and that creates a Phase 5 obligation.**

`lastStepVelocities` and `currentStep` are published at GRID time inside `stepTriggered`, while that
step's audio leaves the plugin `getLatencySamples()` later. Host latency compensation realigns the
*recording*, not live monitoring — so a playhead driven straight from `currentStep` will run 32 ms
ahead of what the user hears, plus up to 22 ms of jitter.

The fix belongs with the trigger FIFO already named for Phase 5 (see below): the audio-domain offset
is a third field of that same published struct, not a new atomic. Alternatively the playhead simply
shifts by `getLatencySamples()`, which is already public. The jitter component is under two frames at
60 Hz and arguably should not be tracked at all — a playhead should show the grid.

Deliberately not half-built in Phase 3: an accessor with no consumer is not a guarantee.

### 03-02 design input — the jitter seam

**03-01's plan claimed the engine shape carries 03-02 unchanged. That is true for late offsets and
false for early ones.**

- `app.js` draws `CACHAÇA`'s timing jitter **once per step** (`const t = nextNoteTime + swingDelay +
  jitter`) and moves every lane of that step together. `scheduleStep` now takes all eight velocities
  and one offset so the draw has one correct home; a per-lane call would invite a per-lane draw, and
  the lanes would flam apart with no assertion able to see it.
- **Jitter is bipolar and offsets are clamped at zero.** There is no representation for a hit earlier
  than its step. ±22 ms at 48 kHz is ±1056 samples — wider than two 512-sample blocks — so clamping
  would collapse the early half onto the block boundary and make the render block-size dependent,
  which is the one property AC-7 exists to protect. **Plan a scheduling origin delayed by the
  jitter's own maximum**, so `offset = lookahead + jitter` is non-negative by construction. This
  needs no change to `Clock` and no host-latency reporting.
- Ghost notes fire exactly where velocity is 0, and `scheduleStep` now hands the engine that fact.
  The per-channel `ghost` parameter and global `cachaca` are **not** yet in `VoiceEngine::Settings`.

### 03-03 design input

**Limiter threshold sizes from 1.454 (+3.25 dBFS)** — the worst profile peak across 36 humanisation
realisations, hottest CARUARU. Not 1.336, which is what a single realisation reports, and not the
"humanisation cannot raise the peak" claim 03-02 first made and had to retract: ghosts add voices and
voices sum.

Traced by 03-02's altitude review and confirmed: `engine.render(buffer)` plus a sibling `MixBus`
works, now that there is exactly ONE render call site. Two consequences to settle deliberately rather
than by where code is easiest to add:

- `getLatencySamples()` and `getTailLengthSeconds()` are currently the engine's figures. With a
  second stage they become chain sums
- `timbre`, `charMix`, `limiterOn` and `master` are output-stage values, already declared in
  `ids::globalParams`. Piling them onto `VoiceEngine::Settings` would make the engine carry values it
  does not use; `resolveChannelSettings` may want splitting into two resolvers
- STATE's original Phase 3 design input said the engine should own "the character bus and limiter".
  03-02's review argues either shape works but the choice must be explicit — the sibling shape is
  where the latency reporting has already drifted

Also still unowned: **gain/pan smoothing**, named in the Phase 3 design input and omitted by both
03-02 and 03-03's scope. VOL and PAN are constant per block per voice, so automating VOL steps at
block boundaries.

### Deferred from 03-02

| Item | Effort | Why deferred |
|------|--------|--------------|
| Test duplication: `renderSteps`, `stepPeaks`, `stepDisplacements` helpers | S | The block-aligned render idiom appears 12 times in two spellings, the per-step window idiom 5 times, and `JitterRig` is used in 3 of the 5 places it fits. ~110 lines |
| `bandEnergy`'s grid cost in `testGhostsOnlyWhereAllowed` | S | 30.6 ms of that test's 54.5 ms is the instrument, not the render |
| `testNoAllocationsWhileRendering`'s 2000-block window | S | 147 ms for 4 checks; 500 blocks still covers hundreds of steal/retire cycles, but the number is in the assertion text |
| `getVoicesDropped()` still has no reader | S | 03-01's review response added two write sites to make it reachable. Either assert it or call it write-only diagnostics |
| Move the measurement layer out of `TestHarness.h` | S | ~250 of its 424 lines are DSP used by one suite, so `ClockTest` and `StateRoundTripTest` recompile on every edit to it |

### Deferred from 03-01

| Item | Effort | Why deferred |
|------|--------|--------------|
| Merge the two voice pools into one `PooledVoice` | M | The right shape and unlocks per-strip `LOAD` plus the *pá* articulation, but it rewrites `render`. `usesSample` is `constexpr`, so `LOAD` cannot be added without changing the discriminator's type. Two pools also carry two different stealing policies today |
| Whether to embed `ZAB_LOW_03` at all | S | The brightness classifier's only production effect is excluding a file nothing can play. Dropping it from the embed list would delete the classifier, its threshold and the alternate counter. Product-shaped, so recorded not decided |
| Designated initialisers for `voiceSpecs` | S | 11 anonymous positional values per row in a C++20 project; named fields are shorter *and* more diffable against `PLANNING.md` |
| Gain/pan smoothers have no owner | S | Named in the Phase 3 design input but omitted by both 03-02 and 03-03. VOL/PAN are constant per block, so automating VOL steps at block boundaries |
| Test duplication | S | `renderSingleHit` needs a setup hook (14 sites bypass it), a `loadProfile` helper (3 copies), `renderBlocks` in the harness (~13 copies across all three suites) |

### Phase 3 design input — from 02-04's altitude review (consumed by 03-01)

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
| No audio device guaranteed on WSL2 | Phase 3 voice auditioning | **Resolved in approach at 03-01 planning:** every audio claim is proved by offline render plus measurement (onset position, band energy, duration), which needs no device and catches a wrong-but-audible voice that an is-it-silent check would pass. `scripts/render-audition.sh` writes WAVs; listening is a separate human-verify checkpoint, on the Windows side where the plugin loads |
| 22 `MSB8064` warnings — MSBuild lowercases dependency paths against a case-sensitive filesystem | Windows incremental builds may misbehave | Not materialised (touch-and-rebuild did reconfigure). Count went 18 → 22 in 02-01, exactly the four new `DEPENDS` paths. Mirror mode would avoid it; revisit if a stale Windows build is ever observed |
| Install discovery is Ableton-specific | Any other host | `FORROBOX_VST3_DIR` override, or generalise the strategy |
| MSVC output is not reproducible (PE build timestamp) | Hash comparison can validate a copy, never "is the install current" | Compare source state instead if staleness detection is ever needed |

### Sample library — decision settled 2026-09-08: hybrid

User supplied `D:\temp\forrobox\FORRO BOX SAMPLES.zip` (9.9 MB, 8 files, all 24-bit stereo).
**Decision: zabumba plays the four `ZAB_LOW` one-shots; the other seven lanes are synthesised.**

**What ships** — the four one-shots, ~450 KB, embedded via `juce_add_binary_data`. Re-measured at
03-01 planning (48 kHz, 24-bit, **true stereo, not dual-mono**; one attack each, so genuinely
single hits):

| File | Length | Peak | RMS | Attack |
|------|--------|------|-----|--------|
| `ZAB_LOW_01` | 0.498 s | −3.1 dBFS | 0.196 | 3.6 ms |
| `ZAB_LOW_02` | 0.457 s | −2.5 dBFS | 0.122 | 1.7 ms |
| `ZAB_LOW_03` | 0.344 s | −2.0 dBFS | 0.037 | 0.6 ms |
| `ZAB_LOW_04` | 0.265 s | **−23.1 dBFS** | 0.018 | 1.1 ms |

**The earlier entry called these "4 one-shot variants" and that was wrong in a way that matters.**
Peaks cluster within 1.1 dB across 01–03 and then fall 20 dB at `04`, while RMS spreads 10×. Played
round-robin — the obvious reading of "variants" — consecutive zabumba hits would jump 20 dB. They
are **velocity layers**, and `04` is the soft layer 03-02's ghost notes (velocity 0.20–0.32
normalised) will reach for. 03-01 derives the ordering from measured RMS at load rather than from
file order, so replacing a sample cannot silently reorder the mapping.

**What does not ship** — the four tempo-locked loops (`ZABUMBA_BPM_90_4_BARS_01`,
`TRIANGULO_BPM_90`, `GANZA 02 104`, `PANDEIRO 01 104 DRY`), 9.5 MB. Three reasons, unchanged from
the original analysis: a fixed 4-bar performance cannot carry per-step velocity, ghost notes or
`CACHAÇA` jitter without slicing; two source tempi (90, 104) and two rates (48k, 44.1k) would need
time-stretching, and a triângulo rate-shifted +47% is a different instrument; and coverage is
partial anyway — nothing for bateria BB/CX/HH/TOM, and no triângulo open/closed pair, which
`PLANNING.md` calls that instrument's defining behaviour.

**Consequences accepted with the decision:**

- `PITCH` and `DECAY` now mean two different things depending on the channel — playback rate and
  envelope truncation on zabumba, oscillator pitch and decay scale elsewhere. `PLANNING.md` line
  ~730 already specifies exactly this, so it is a documented split, not a new one.
- `PAN` on zabumba is a stereo **balance**, because the source is already stereo; the seven synth
  voices are mono-into-pan. The output stage owns the distinction so no voice invents its own.
- Sample rate: 48 kHz fixed, so a 44.1 k host needs resampling — done once in `prepare`, never in
  `render`, and skipped entirely at 48 k.
- Two currently-stubbed controls become reachable in principle: per-strip `LOAD` and the
  `BUNDLE: MINIMAL` indicator. Both stay stubs for v0.1.

## Boundaries (Active)

Phase 1 closed; its plan boundaries are retired. Project-wide constraints:

- `PLANNING.md`, `uploads/UIUX.md` — specification, read-only
- The HTML/CSS/JS prototype and `build_standalone.py` — design reference, must keep working
- `~/JUCE` — consumed read-only. The ASCII naming decision removed the only argument for patching it
- No new third-party dependencies beyond JUCE without an explicit decision
- `processBlock` stays allocation-free and lock-free — the contract Phase 1 established
- Parameter and group IDs are fixed; renaming one invalidates saved host state
- **Amended 2026-09-08:** the standing "do not commit the sample library or anything from `/mnt`"
  boundary now has one carved-out exception — the four `ZAB_LOW` one-shots (~450 KB) enter
  `assets/samples/` because the hybrid decision makes them part of the product. The 9.5 MB of loops,
  and everything else under `/mnt` or the scratchpad, stay out. Nothing is pushed to any remote

## Session Continuity

Last session: 2026-09-08
Stopped at: **03-01 complete.** The plugin makes sound; 842 checks green on three compilers
Next action: Review and approve the plan, then run `/paul:apply .paul/phases/03-voices-mix-bus/03-03-PLAN.md`
Resume file: .paul/phases/03-voices-mix-bus/03-03-PLAN.md
Open items: (1) Vendor folder in Live reads `Forro Box` inside `Forro Box`; `COMPANY_NAME` is
display-only and safe to change. (2) The four tempo-locked loops remain unused and unshipped — the
per-strip `LOAD` control that would give them a home is a post-v0.1 stub.

---
*STATE.md — Updated after every significant action*
