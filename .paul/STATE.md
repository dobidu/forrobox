---
description: "Forró Box — current position and accumulated context"
type: ProjectState
about: "Forró Box"
---

# Project State

## Project Reference

See: .paul/PROJECT.md (updated 2026-09-08)

**Core value:** Producers get authentic, human-feeling Brazilian forró percussion grooves inside
their DAW without hiring a percussionist or programming every hit by hand.
**Current focus:** v0.1 Initial Release — Phase 5, the sequencer grid

## Current Position

Milestone: v0.1 Initial Release
Phase: 5 of 8 (Sequencer grid) — planning
Plan: 05-03 created 2026-09-15 — awaiting approval
Status: PLAN created, ready for APPLY
Last activity: 2026-09-15 — 05-03 planned: the grid answers writers other than itself

Progress:
- Milestone: [█████░░░░░] 50% (4 of 8 phases)
- Phase 5: [█████░░░░░] 50% (2 of 4 plans)

## Loop Position

Current loop state:
```
PLAN ──▶ APPLY ──▶ UNIFY
  ✓        ○        ○     [05-03 created — awaiting approval]
```

Phase 3: 03-01 ✓ · 03-02 ✓ · 03-03 ✓ — all three loops closed, phase transitioned.
Phase 4: 04-01 ✓ · 04-02 ✓ · 04-03 ✓ · 04-04 ✓ · 04-05 ✓ · 04-06 ✓ — COMPLETE
Phase 5: 05-01 ✓ · 05-02 ✓ · 05-03 ◀ PLANNED · 05-04 ○  (split to FOUR at 05-02 planning)

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
| Phase 4 split into 4 plans, and its scope amended to include the header and footer controls | 4 | Decided at Phase 4 planning. ROADMAP gave the grid to Phase 5 and the side panel to Phase 6 and left the header/footer owned by no phase, while the phase goal is that the chassis reads as the prototype. An unpopulated header does not — and the header holds the two signature 54 px knobs, the Knob component's most important instance |
| Space Grotesk is instanced OFFLINE from the variable font into four committed statics, not loaded as a variable font | 4 | Verified at planning: google/fonts publishes Space Grotesk as a variable font only, the upstream repo has no SemiBold static at all, and JUCE 8.0.12's `Typeface` API has no variation-axis setter — so `createSystemTypefaceFor` on the VF loads its fvar default of **wght 300, Light**, and every weight in the UI would render at the thinnest one silently. The instancer also leaves name ID 1 as "Space Grotesk Light" for every weight, so the name tables are patched. IBM Plex Mono ships real statics at 400/500/600 and is used as fetched |
| The design tokens are cross-checked against `forrobox.css` on every build, not transcribed and trusted | 4 | The same argument as the groove tables, and the same failure mode: a wrong hex digit is not a crash or a failed test, it is a colour that is subtly wrong with no way to tell which digit. A unit test holding the expected hexes by hand would duplicate the typo risk it is meant to catch |
| UI claims are proved by headless offline render + pixel measurement; looking at it is a separate human checkpoint | 4 | The same split Phase 3 used for audio. Verified at planning: a `Component` never added to a desktop needs no window peer, so `paintEntireComponent` into a `juce::Image` works with no display — probing (5,5) returned exactly `ff141414`. Weight is discriminated by INK MASS, not advance width: the four Space Grotesk widths for "FORRO BOX" at 24 px span 0.45% (100.326–100.777) and no honest tolerance separates that from rounding, while ink mass spans 417.3–617.9 with a 9% smallest step — a width assertion would pass with four copies of one weight |
| The Knob and step pad are custom `Component`s, not `LookAndFeel` overrides | 4 | Both carry per-instance state a stateless L&F callback cannot: a pad's velocity and flash decay, a knob's bipolar flag. The `LookAndFeel` stays thin — tokens, the two families, the radius, and only the JUCE colour IDs actually consumed |
| Knob reset is Alt+click; right-click falls through to the host | 4 | Decided at 04-02 planning, by `/graphify`. `PLANNING.md:370`'s interaction table says right-click resets, and `PLANNING.md:876-878` — five hundred lines away, in Accessibility & Input Notes — qualifies it: *"in a plugin, ensure this doesn't collide with the host's parameter context menu (or move reset to `Alt`+click / double-click and put automation options in the right-click menu, which is the DAW convention)"*. Double-click is already type-to-set, so reset is Alt+click. Spec-directed, and it retires PROJECT.md's standing "must not collide" constraint |
| A knob owns NO value state: range, interval, default and display text all come from the parameter | 4 | Decided at 04-02 planning. `controls.js` freezes `def` at construction and `loadProfile` pushes values with `fire=false`, so the prototype's reset returns to the page-load value, not the loaded profile's — a prototype artefact. And `set()` quantises with a `step` the knob owns while JUCE's `NormalisableRange` already carries the interval. Two copies of one law is the shape that produced 03-03's `dry = 1 - 0.5*wet` and 04-01's tracking bug |
| Knob geometry is RELATIVE — a 100x100 viewBox scaled once, not pixels | 4 | `PLANNING.md:347` renders the same viewBox at 28/32/54 px, so 38/30/5/16 are viewBox units: at 32 px the track radius is 12.16 px. Treating them as pixels draws one correct 100 px knob and three wrong ones, and 32 px is the strip case — the one the header would never reveal |
| 04-03/04-04 divided by WHERE a component is used, not by ROADMAP's wording | 4 | Decided at 04-03 planning. ROADMAP listed the transport buttons and the STYLE segmented control in 04-03's button family, but both appear only in the header and footer that 04-04 places. Building them early would leave two components unplaced and unproven in situ. 04-03 takes the pad plus everything landing in the STRIP and finishes it; still four plans |
| The pad's backlit gradient is a circular `ColourGradient` with an ELLIPSE transform on the `FillType` | 4 | Verified at 04-03 planning. The spec is `radial-gradient(120% 100% at 50% 22%)`; `juce::ColourGradient`'s radial mode is circular (`juce_ColourGradient.h:67`). `juce::FillType` carries its own `transform` (`juce_FillType.h:154`) applied to the GRADIENT, so a radius-`ry` circle scaled `rx/ry` in x gives the ellipse with the pad's corners untouched. `Graphics::addTransform` would have stretched the rounded rectangle too |
| Every element has its OWN accent-intensity law; there is no shared one | 4 | Three now, each from its own CSS rule: the accent bar is `i x 35%` (css:608), the knob and fader are `saturate(0.4 + i x 0.6)` (css:361, 378), the pad is `i x 45%` (css:630). 04-01's "a shared mechanism does not imply a shared value", third instance |
| A ghost pad is a LIT pad plus a dot, not an unlit one | 4 | Decided at 04-03 APPLY, with the user. `PLANNING.md:451` and this plan's own AC-1 say a ghost "renders as off"; `app.js:374-379` adds `ghost` on top of `on` and `.pad.ghost` (css:476-480) only appends the `::after` circle, so the prototype's ghost is backlit at its velocity opacity. Third spec/reference conflict in this phase and the third resolved toward the running prototype — the AC was amended rather than the render bent to it |
| Velocity is an ELEMENT opacity, so it is a transparency layer and not an alpha threaded through each `setColour` | 4 | The prototype sets `pad.style.opacity`, which composites the whole pad — ground, sheen, glow, dot, border — as one group and blends once. Per-layer alpha blends the sheen against an already-faded ground and gives a different result. `beginTransparencyLayer`, and full velocity lands on exactly 1.0 so the common case takes no layer |
| `color-mix` with TWO percentages is normalised to sum 100, and the raw percentages stay in the header | 4 | `color-mix(in srgb, var(--c) 100%, white 22%)` sums to 122, so a browser mixes 22/122 = 18.0% white, not 22%. The pad is the first place the design uses that form. `theme::mixWeight (basePct, otherPct)` writes the law once and keeps 22 and 35 readable straight out of the stylesheet by `verify-geometry.py` |
| A component whose paint bleeds past its own box reserves that room and is ASKED for its bounds | 4 | The pad's 9 px outer glow and the fader's 6 px thumb overhang are both outside the css box, and a `Component`'s paint is clipped to its own bounds — drawn at the exact box each would contribute nothing at all. `StepPad::boundsForPadRect` and `Fader::boundsForBox` take the css rect and return the bounds; the grid keeps laying out on the css pitch. Same shape as `kKnobCellHeight` asking the knob |
| The fader is ABSOLUTE, and `ProportionAttachment` is the half it shares with the knob | 4 | `controls.js:232-247` computes the value from the pointer's x inside the track rect and clamps it — click-to-jump, and a drag is that same computation repeated. No `/160`, no shift-fine, no wheel and no tooltip, all of which the knob has. The fader's whole attachment surface is a strict subset of the knob's, so parameter->control, drag and the gesture bracket were extracted into `ProportionAttachment` and `KnobAttachment` keeps only what a fader cannot fire |
| `ChassisLayout::kPatternRowHeight` is the TALLEST child, 26 and not 20 | 4 | Decided at 04-03 APPLY, with the user, and it breaks that plan's own "StripLayout's geometry does not move" boundary. The constant was derived from `.pat-screen` alone; `.pattern-row` is `display:flex; align-items:center` and its other two children are `.arrow-btn` at a fixed 26 px (css:231), so every box below the cycler sat 6 px high. No check could see it — 04-02's stack assertions compare the derivation against the constants it is built from. The fix is `kKnobCellHeight`'s: ask the component |
| Mute, solo and LOAD have NO press scale | 4 | Only `.btn` (css:142) and `.arrow-btn` (css:236) declare `:active`. Task 1 gave the mute/solo variant the base button's 0.96 because it shared everything else with it — an invented behaviour, the same class as giving the fader a wheel because the knob has one. `Button::kNoPress` writes the absence down rather than leaving it implied |
| `verify-geometry.py` reads `app.js` and the TYPE SCALE, not only lengths | 4 | The velocity law and the ghost threshold are behaviour, in no stylesheet, and the C++ asserts `opacityForVelocity` against the same two constants it is built from. The type scale had nothing policing it at all: `PLANNING.md`'s table covers 21 rows and five of the strip's come from `forrobox.css` alone, where a transcribed 9 for a declared 10 is invisible. `cpp_constant` also resolves `ns::name` now — `pad::kHeight` is 26 and `ChassisLayout::kHeight` is 780, and searching the concatenated headers reported the pad as 780 px tall |
| An attachment must be reset EXPLICITLY before the control it binds; declaration order does not cover assignment | 4 | Found by `/code-review` on 04-03 and confirmed under AddressSanitizer. An implicitly-defined move-assignment assigns members in DECLARATION order, unlike destruction, which runs in reverse — so `controls = {}` freed each `Button`/`Fader` while its attachment still held a reference, and `~ToggleAttachment` then wrote `button.onClick = nullptr` into that memory. ASan named it exactly: `heap-use-after-free` at `ToggleAttachment.cpp:51` through `StripControls::operator=`. The comment that used to sit on those members claimed the ordering made them "obviously safe"; it covers `~Chassis` and nothing else |
| While SYNC is on, the HOST's transport is the only one that plays | 4 | Found at 04-04's checkpoint by the user: turning SYNC on stopped the beat. The plugin's own `playing` gated ahead of the host's check in `planBlock`, so a synced plugin sat silent against a rolling project until its own Play was pressed too — two gates where `PLANNING.md:838` ("follow host tempo and transport") and 02-03's own summary ("the host's transport decides whether anything plays") describe one. It survived four plans of host-sync work because EVERY host-sync rig calls `setPlaying(true)` in its constructor, so "host rolling, plugin stopped" had never been rendered once. Breaches 04-04's own boundary on `processBlock`; confirmed with the user first |
| A control the user cannot reach shows a state and REFUSES input, visibly | 4 | The BPM field under SYNC, and now the transport pair. Dimmed to 0.55 with the cursor withdrawn, because a control that ignores a click without saying why reads as broken. The Play button also shows the HOST's transport rather than the plugin's own clock while synced — showing `isPlaying()` there would leave it dark while the groove ran |
| The three interaction laws are three, and the third is the BPM field's | 4 | knob `(dy/160) x range` anchored (controls.js:130); fader the pointer's x within its track, absolute (controls.js:236); BPM `0.5 BPM/px` anchored (app.js:138), with a wheel of exactly +/-1 where the knob's is `max(1, range/50)`. The BPM law is the KNOB's shape in BPM units — a header full of screens invites the fader's absolute law, which would make the field jump to wherever the pointer happened to be |
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

### 03-03 reconciliation

Recorded in `.paul/phases/03-voices-mix-bus/03-03-SUMMARY.md`. **20 negative controls, 19 detect,
one a documented numeric no-op** (`MixBus::kTailSeconds` is 0.0, so removing it from the chain sum
changes no value — the sum is there for the next stage). Phase total: **92 controls.**
What generalises:

| Lesson | Why it earned a rule |
|--------|----------------------|
| **A comment claiming a fix is unnecessary is the fix's absence, written down.** | `drive` was unsmoothed under a comment asserting the step was masked by the other smoothers. False in two of the three transitions the plugin can make: LO-FI ↔ HI-FI is a −2.8 dB single-sample step, and LO-FI → CICLOTRON shares its cutoff and mix so nothing else smooths at all. The test only exercised HI-FI → LO-FI, the one case where the claim held |
| **A law written out twice is covered once.** | `dry = 1 − 0.5 × wet` lived in `dryGainFor`, which four checks cover, and again per sample in `process`, which nothing covered — under a comment claiming that expressing it there "enforces it". An earlier control had mutated the *accessor's* copy and been detected, which is exactly why the duplicate looked safe |
| **A table-driven value needs its ORDER asserted, not just its default.** | `timbreChoices()` builds the TIMBRE strings from `timbreSpecs`, but nothing tied the two orders together. Hand-writing the `StringArray` in a different order left the plugin displaying LO-FI while rendering HI-FI, and passed 1090 checks. Those strings are what host automation lanes and saved projects carry |
| **A smoother must SNAP on its first block, not ramp from a constructor default.** | Two identically configured instances rendered differently for the first 20 ms, each "correct" in isolation — which is the determinism criterion failing quietly rather than failing |
| **A rig default chosen to isolate one stage will be mistaken for the product.** | `renderAuditionFiles` inherited `AudioRig`'s deliberately transparent bus and reported clipping at 1.19–1.28 while the headline test measured 0.753 on the same grooves. `useShippedChain` is now the named complement |
| **Read a consuming accessor once.** | `takeGainReductionDb()` appeared twice in one expression; argument evaluation order is unspecified, so the message printed 8.13 dB while the condition tested the 0 left behind |
| **The slope reference for a smoothing bound is the STEEPER endpoint.** | `tanh` has slope `drive` at zero, so an output step is the product of the input step and the gain — bounding against the destination understates a transition that starts steeper than it ends |

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

### Consumed: 03-02 design input — the jitter seam (shipped in 03-02)

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

### Consumed: 03-03 design input (shipped in 03-03; the sibling `MixBus`, the chain sums and the 1.454 threshold are all in place)

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

### Deferred from 03-03

| Item | Effort | Why deferred |
|------|--------|--------------|
| Per-voice gain and pan smoothing | M | **Still unowned after three plans.** The bus smooths its four values per sample; VOL and PAN are constant per block per voice, so automating either steps at block boundaries. Named in the Phase 3 design input and in every Phase 3 plan's deferred list |
| `ids::outputMode` ships inert | S | **Phase 4 must decide.** Declared, host-visible, automatable, persisted, and read by nothing. Removing it later is a saved-state compatibility question, so the choice belongs to the phase that draws the footer — draw it working, draw it disabled, or drop it before any user has state to invalidate |
| Whether to embed `ZAB_LOW_03` at all | S | Unchanged from 03-01: it measures as a *pá* hit and the articulation classifier's only production effect is excluding a file nothing can play |

### Deferred from 03-02

| Item | Effort | Why deferred |
|------|--------|--------------|
| Test duplication: `renderSteps`, `stepPeaks`, `stepDisplacements` helpers | S | **Grown at 03-03:** the block-aligned render idiom is now **16 sites in three spellings**, not 12 in two — `BusRig::run` added the third. The per-step window idiom is 5 sites, and `JitterRig` is used in 3 of the 5 places it fits. ~110 lines |
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

### Phase-completion heuristic — NOT resolved; mis-fired a third time

`unify-phase.md` decides "last plan in phase" by comparing PLAN.md and SUMMARY.md counts. It mis-fired
twice mid-phase in Phase 2 while the counts happened to match at 2 and 2, and ROADMAP.md was
authoritative both times. It was then recorded as resolved because the counts agreed at Phase 2's
real end (4 and 4) — but agreeing when the phase IS complete proves nothing about the heuristic.

**Fourth mis-fire at 04-02 UNIFY (2026-09-11): 2 PLAN, 2 SUMMARY, counts match, phase 50% done.**
**Third at 04-01 UNIFY: 1 PLAN, 1 SUMMARY, counts match, phase 25% done.**
The counts match whenever plans are written one at a time, which is this project's normal practice —
so the heuristic is wrong by construction here, not occasionally unlucky. **ROADMAP.md's plan list is
the authority**: it names 4 plans for Phase 4 and `paul.toml` carries `plans_total`. Check one of
those before transitioning, never the file counts.

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

### Phase 4 planning — what the spike settled, and what it left open

Four assumptions were tested before they went into a plan as verification methods, because the whole
phase's evidence rests on them:

| Assumption | Verified | Result |
|---|---|---|
| A JUCE `Component` can be rendered and measured with no display | yes | `paintEntireComponent` into an ARGB `juce::Image` returned exactly `ff141414` at (5,5) and exactly `ffe8650a` at (20,20); 351 text ink pixels; PNG written. No peer, no X11 |
| The fonts can supply all seven specified weights | **no, as planned** | Space Grotesk is variable-only upstream and JUCE cannot select its axis — the VF's default instance is 300 Light. Resolved by offline instancing with patched name tables |
| `fontTools --update-name-table` fixes the instance names | **no** | `ValueError: Cannot find Axis Values {'wght': 600.0}` — the STAT table declares no named value at 600. Name IDs 1/2/4/6/16/17 are set explicitly instead |
| Advance width can discriminate font weight in a test | **no** | 0.45% total spread across four weights. Ink mass is the instrument: 417.3 / 523.2 / 572.3 / 617.9 |

**Left open, deliberately:** glyph coverage for the Portuguese copy (Ó Â Á Ç) and for Phase 8's
`♪ NO PONTO` (U+266A) is checked and recorded in 04-01 but not acted on, and no subsetting is done —
so the embedded fonts are ~620 KB. `/graphify` is skipped for 04-01 with the reason recorded (its
value is avoiding manual re-reading of relationships; this plan's inputs are literal hex and px
values, which a graph does not carry) and is intended for 04-02, whose input is `controls.js`'s
interaction semantics.

### 04-01 reconciliation — closed 2026-09-11

Recorded in `.paul/phases/04-ui-shell/04-01-SUMMARY.md`. **30 negative controls, 30 detect** (20 in
apply after two rounds, 10 in `/simplify`). 1348/1348 under GCC, Clang and MSVC with `DISPLAY`
unset. Checkpoint approved from the renders plus a live Ableton Live 12 screenshot.

| Built | Where |
|---|---|
| Seven embedded font weights, both OFL licences, reproducible asset build | `assets/fonts/`, `scripts/build-fonts.py`, `src/Typography.*` |
| Both palettes + the two highlight recipes, cross-checked against `forrobox.css` on every build | `src/Theme.*`, `scripts/verify-theme.py` |
| Four regions, five strips, per-strip reserved geometry, one scale transform | `src/Chassis.*`, `src/LookAndFeel.*`, `src/PluginEditor.*` |
| The headless UI measurement harness, every instrument self-tested | `tests/UiTest.cpp` (a fourth suite, same executable) |

**`/simplify` found two rendering defects that 1288 passing checks could not see:**

| Defect | Why nothing caught it |
|---|---|
| The light theme's header highlight shipped at white **0.50** where the stylesheet says 0.05 | ONE `raisedHighlight` field served two different CSS values — `.header` is 0.05 in both themes (css:87), `.side, .footer` are 0.04/0.50 (css:600-601). Max-channel error 7/255 over `--raised` `#faf7f0` |
| The footer's highlight **never rendered at all** | `paintFooter` drew it on row 0, then painted `--line` over the same row. `.footer` carries `border-top` AND an inset shadow — two rows in the CSS box model. The side panel escaped only because its border is vertical |

What generalises:

| Lesson | Why it earned a rule |
|--------|----------------------|
| **A shared mechanism does not imply a shared value.** | `paintRaisedHighlight` was correctly shared across three surfaces and reached for one field. The stylesheet shares the mechanism and splits the value, so the helper takes the colour now. Sharing the drawing and sharing the number are separate decisions |
| **A value cross-check does not prove the value reaches a pixel.** | `verify-theme.py` pinned the token; nothing proved it was painted. Changing the light header 0.50 → 0.05 passed all 1288 checks. The pixel probe added to close that is what then found the footer overpaint — a second guarantee, not a duplicate of the first |
| **A brightness instrument is only valid over the background it was proved on.** | `inkMass` measures absolute brightness, so over `--panel` (brightness 0.118) an empty 163x16 region already scores ~300 — every threshold that detects text is one an empty region also clears. `contrastMass` measures departure from a known background instead |
| **A law written out twice is covered once — twice over.** | Zeroing tracking inside `drawTracked` passed 1275 checks (`letterSpacingEm * heightPx` lived in two places). `trackingFor` fixed the STEP and left the two paths still computing the total EXTENT differently — whole-string advance vs summed per-glyph, which disagree wherever the font kerns. One `GlyphArrangement` now serves both |
| **An equivalence harness is an instrument and needs its own control.** | My first pixel-diff reported 0 differing pixels for a deliberately broken variant: `Chassis::paint` opens with `fillAll`, which wiped the injected "old" fill, so it compared the new code to itself. Rebuilt as two binaries differing only in `paintMatrix`; controls then detected 4,770 and 19,080 px |
| **A checkpoint artefact needs the same scrutiny as a test.** | `writeReferenceRenders` upscaled the 1x bitmap under a comment claiming that is what the editor does — it is not, `setTransform` is applied by the PARENT. Each render's far corner is now asserted |
| **`--verify` is worth writing even when nothing seems to need it.** | The font build's own verify caught that all four instanced files were not byte-reproducible. Three bytes: `head.modified` and the checksum after it — fontTools recomputes it from the clock unless `recalcTimestamp = False` |
| **Reproducing a percentage means quantising once.** | `juce::Colour::interpolatedWith` quantises the proportion itself to 8 bits and round-trips through premultiplied integers: the anchor tint's largest channel move came out 27/255 where the spec's 12% gives 24. `theme::mix` interpolates in float |

**Measured wins:** `paintMatrix` filled 419,520 px to reveal 1,824 of divider — now fills only the
columns no strip covers, **548.7 µs → 3.46 µs**, verified equivalent at **0 of 9,753,796 pixels
differing** across six sizes in both themes. Tracked text via one `GlyphArrangement`: **243.6 µs →
65.0 µs** for the chassis's ten labels.

**Five measurement errors during apply, all mine, all before any code was wrong:** `isTransparent()`
is alpha == 0 so the alpha check asserted its own opposite; `checkEqual` on a `juce::uint8` printed a
character; saturation is the wrong instrument for "subtle" at brightness 0.21; the strip-divider
composite is over `--bg`, not `--panel`; and `inkMass` over `--panel` as above.

**Anchor tint settled:** measured `#362720`, exactly `color-mix(in srgb, #1e1e1e 88%, #e8650a)`.
Approved as-is at the checkpoint. If ever revisited, that is a **spec** deviation, not a code fix.

### Deferred from 04-01

| Item | Effort | Why deferred |
|------|--------|--------------|
| Render the **editor** in tests, not only the chassis | M | AC-3's scale claim is proved against test code that reproduces the transform; nothing renders `ForroBoxAudioProcessorEditor`. A wrong denominator in `resized()` passes every current check and produces correct-looking PNGs |
| The remaining shadow/gradient recipes are uncovered | M | The recessed/well/pad layers and the header gradient are still hand-transcribed. The two that actually diverged are now cross-checked; the rest is stated in the script's docstring as a known gap |
| The OFL blobs are not tied to their own family | S | The check proves each blob contains the OFL text, not that `SpaceGroteskOFL_txt` is Space Grotesk's — a swapped `forrobox_add_binary_data` order would pass |
| Neither font carries U+266A (♪) | S | Phase 8's `♪ NO PONTO` needs a fallback face, a drawn glyph or different copy. Coverage reported on every font build |
| No font subsetting — ~788 KB embedded | S | Nothing forces it yet |

### 04-02 reconciliation — closed 2026-09-11

Recorded in `.paul/phases/04-ui-shell/04-02-SUMMARY.md`. **27 negative controls, 27 detect** — after
three rounds, because the first two found assertions that could not fail. 1900/1900 under GCC, Clang
and MSVC. Suite 2.80 s → 2.16 s.

**The finding that mattered: `verify-geometry.py` was built this plan to close "a C++ test cannot
police a constant it also consumes" — and then policed 29 CSS lengths and ZERO of the five numbers
`PLANNING.md:349-357` calls the knob's identity.** `kArcRadius` 38, `kHubRadius` 30,
`kIndicatorTipY` 16 and the ±135° sweep live in `controls.js`, which the script never opened. Every
C++ assertion multiplies the same constant it checks, and the indicator tests profile the angular
SECTOR — direction, never length. `kIndicatorTipY` could have been 40, a third-length stub, with all
1891 checks green. Now 35 lengths across both source files; six new controls, six detecting.

What generalises:

| Lesson | Why it earned a rule |
|--------|----------------------|
| **Cross-check a design number against ITS source file, not against the one you happened to open.** | The stylesheet carries strokes and margins; the SVG path geometry carries the radii, the sweep and the tip. Half a cross-check reads as a whole one — the script's own CMake comment claimed it "owns the lengths" while the knob's identity was owned by nobody |
| **A tautology survives the removal of its duplicate.** | `kKnobCellHeight` re-added `preferredHeight`'s body; making it ASK the knob removed the duplicate expression and the control STILL passed, because `cell.getHeight() == kKnobCellHeight` compares a constant to itself. Removing a duplicated law and asserting the relation it stood for are two separate jobs |
| **A clamping API turns an overflow assertion into a tautology.** | `juce::Rectangle::removeFromTop` clamps, so `slack >= 0` could never catch a stack grown past the bottom pad — grow a margin by 100 px and the last box is squashed to zero with slack still exactly 0. The comment claimed the opposite. Assert the DECLARED total, which clamping cannot hide |
| **A gesture tested through its callback is a gesture tested through nothing.** | Double-click called `onTextEntered` directly; deleting `Knob::mouseDoubleClick` entirely left the suite green. AC-6 stated the rule verbatim and the test broke it anyway |
| **Every rig default is a claim about what gets rendered.** | `KnobRig` sized its holder `preferredHeight (size, false)`, so a labelled knob had no room for its label — the micro-label's ink was asserted nowhere and the rig could not have drawn one if asked. Its HEIGHT was cross-checked three ways |
| **A hand-copied order in a test agrees with itself.** | The VOL/PITCH/DECAY/PAN order lived in two local arrays copied from production, so reordering moved the knobs AND both expectations. 02-01's "keyed both sides off the same stale list", four plans later |
| **Measure the sleep, not the work.** | `runDispatchLoopUntil` is FIXED-duration and never returns early: 92 calls × 8.015 ms = 737 ms of a 2.80 s suite spent asleep. The processor construction I assumed was the cost measured 49 µs × 18 rigs = 0.9 ms |
| **A dead branch's comment names a case that already exists and contradicts it.** | `intervalSize`'s continuous fallback was justified by "a 40..300 BPM knob" — which is already an `AudioParameterInt`. The branch was dead on arrival and its regression test ran on VOL, where both definitions agree |

**MSVC caught a use-after-free that eleven `/code-review` findings did not:**
`writeReferenceRenders` declared the processor AFTER the chassis, so it died first while the chassis
still held `KnobAttachment`s deregistering from its parameters. Linux tolerated it; MSVC crashed the
suite. I first misread that crash as my own timeout.

**Five measurement errors, all mine, all before any code was wrong:** both arc instruments measured
BRIGHTNESS, which the light theme breaks (the orange arc is 0.036 from `--panel` in brightness and
0.545 in colour distance) and which reported a bipolar knob sweeping the wrong way; my control runner
read `tail`'s exit code; `KnobRig` could not render a labelled knob; a guessed 5.0 threshold where
the real value is 4.7; and the checkpoint renders were built from a bare chassis, so the images a
human would have looked at showed empty strips.

**Deliberate spec deviations:** reset targets the PARAMETER's default, not `controls.js`'s
construction-frozen `def`; Shift+wheel moves one interval, not `step * 0.2`, which quantises back to
zero on every integer control in the prototype.

### Deferred from 04-02

| Item | Effort | Why deferred |
|------|--------|--------------|
| Cache the knob's invariant paint layers | M | **Measured:** track arc + hub + label = 17.8 of 24.75 µs per knob paint; a chassis repaint would go 1,192 → 892 µs. No repaint timer exists today — repaints are value-driven — so this earns itself when **Phase 5's 60 fps playhead** lands |
| A shared module for the three verify scripts | M | `verify-theme.py` still parses CSS with the lazy `\{(.*?)\}` that `verify-profiles.py` and `verify-geometry.py` each independently rejected. Three parsers of one stylesheet, one using the anti-pattern the others document |
| `arcProfile` / `inkRadiusCentroid` share one pixel walk | S | The `0.02` ink floor and the pixel-centre convention are written twice, and every AC-1/AC-2 claim rests on the two agreeing |
| The micro-label is confined to the knob's width, not its cell | S | `justify-items: center` on a full-width cell is the other reading of css:326 |
| `tests/UiTest.cpp` is 2,909 lines with three render-rig idioms | S | `KnobRig`, `AttachedKnobRig` and the inline rigs have genuinely different subjects; only the flat-ground holder is duplicated three ways |

### Blockers/Concerns

Phase 1's resolved blockers are retired; their history is in the phase summaries.

| Blocker | Impact | Resolution Path |
|---------|--------|-----------------|
| ~~No audio device guaranteed on WSL2~~ | Phase 3 voice auditioning | **Resolved and closed at the 03-03 listening checkpoint.** The approach settled at 03-01 planning: every audio claim is proved by offline render plus measurement (onset position, band energy, duration), which needs no device and catches a wrong-but-audible voice that an is-it-silent check would pass. `scripts/render-audition.sh` writes WAVs; listening was a separate human-verify checkpoint, on the Windows side where the plugin loads. All four grooves approved 2026-09-08 |
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

Last session: 2026-09-15
Stopped at: 05-03 created, awaiting approval
Next action: approve `.paul/phases/05-sequencer-grid/05-03-PLAN.md`, then `/paul:apply`
Resume file: .paul/phases/05-sequencer-grid/05-03-PLAN.md
Resume context:
- **3278/3278 on GCC, Clang and MSVC** with `DISPLAY` unset; three cross-checks green
  (`verify-geometry.py` now 152 lengths + 50 type-scale values). VST3 installed, hashes matched,
  moduleinfo clean
- **The checkpoint was approved without answering the question it asked.** Step 8 put the swing
  divergence to the user — the prototype glides to each step's centre and waits when swing delays
  the next hit (`app.js:719`'s CSS transition), ours is linear in musical time — and the reply was
  "approved" with no comment. The linear sweep therefore stands UNJUDGED, not endorsed. Worth
  re-asking once there is a reason to look
- **The plan specified a mechanism the domain made unnecessary.** It called for a double-buffer with
  an atomic index; a velocity is 0-127 by invariant, so 7 bits, and 8 lanes plus a 6-bit step fit in
  one 64-bit word. The fix deleted the mechanism instead of building it
- **The lookahead correction shipped untested, and my first test for it also could not fail.**
  `getDisplayPositionInSteps` had one occurrence in the tree — its own declaration — so inverting the
  sign passed 3208/3208. The replacement compared against the emitted step with a tolerance of one
  whole step while the correction is 0.256 of a step; confirmed useless by running the inverted
  mutant against it. The check that works pins the exact relation to 1e-9
- **I wrote a comment specifying one design and built another.** `PluginProcessor.h:562` says the LED
  fires "when this position reaches the step, which is where Task 3 does it"; Task 3 counted down
  frames. `/simplify` found it, the comment was right, and the restructure deleted the queue, the
  frame arithmetic and the sample-rate read
- **Driving the LEDs from the raw step failed ZERO checks** before that was pinned. The correction is
  the whole point of the plan and nothing tested which timeline the LEDs used
- **Two theme-API misreads that `/code-review` missed.** `theme::accentFill`'s second parameter is the
  INTENSITY and it applies the accent bar's 0.3/0.7 — passing the computed 0.4/0.6 composed them.
  `surface::wellShadow`'s fourth parameter is `depth` in pixels and was given an alpha
- **`Playhead.h` was in no header list, so none of its nine constants were policed.** That is how
  `kTrailGap` shipped as 0 under a comment saying `right: 3px`. Seven now checked. The general gap
  remains: enrolment is manual and per-constant, so every plan polices only what it remembers
- **The efficiency pass measured and two of my three suspicions were wrong by orders of magnitude.**
  `resolveChannelSettings` per tick is 9 ns; `publish` costs +1 ns. The real cost was the repaint:
  475 us a frame redrawing furniture. Culling each piece took steady state from 3.7% of a core to
  1.3%
- Two findings deferred with measurements in PROJECT.md: the LED and meter should be child
  Components (~86 us a frame, and `Playhead.h` argues for it in this same diff), and `paintStrip`
  culls against a bounding box so two distant lit strips repaint all five

