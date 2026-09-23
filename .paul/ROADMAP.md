---
description: "Forró Box — milestone and phase structure"
type: Roadmap
about: "Forró Box"
---

# Roadmap: Forró Box

## Overview

Forró Box is a rhythm-production instrument plugin (VST3) for Brazilian forró percussion: a
five-channel step sequencer with regional groove profiles, a `CACHAÇA` humanisation engine,
synthesised percussion voices and MIDI drag-out. A high-fidelity HTML/CSS/JS prototype already
defines the look and behaviour; the journey runs from plugin skeleton and parameter tree, through
clock and voices, into a native JUCE recreation of the chassis, and out to MIDI export and polish.

## Current Milestone

**v0.1 Initial Release** (v0.1.0)
Status: In progress — Phase 9 added before release
Phases: 8 of 9 complete (89%)

## Phases

**Phase Numbering:**
- Integer phases (1, 2, 3): Planned milestone work
- Decimal phases (2.1, 2.2): Urgent insertions (marked with [INSERTED])

Phases execute in numeric order.

| Phase | Name | Plans | Status | Completed |
|-------|------|-------|--------|-----------|
| 1 | Plugin foundation | 3 | ✅ Complete (3/3) | 2026-09-07 |
| 2 | Sequencer clock | 4 | ✅ Complete (4/4) | 2026-09-08 |
| 3 | Voices & mix bus | 3 | ✅ Complete (3/3) | 2026-09-08 |
| 4 | UI shell | 6 | ✅ Complete (6/6) | 2026-09-14 |
| 5 | Sequencer grid | 4 | ✅ Complete (4/4) | 2026-09-16 |
| 6 | Side panel | 6 | ✅ Complete (6/6) | 2026-09-20 |
| 7 | MIDI out | 3 | ✅ Complete (3/3) | 2026-09-21 |
| 8 | Polish | 5 | ✅ Complete (5/5) | 2026-09-23 |
| 9 | Content & convolution | 5 | In progress (1/5) | - |

## Phase Details

### Phase 1: Plugin foundation ✅ Complete 2026-09-07

**Outcome:** A VST3 instrument building on Linux and Windows, exposing 45 grouped parameters with a
lossless state round-trip, verified loading in Ableton Live 12. Test suite green under GCC, Clang
and MSVC. Silent by design until Phase 3.

**Goal:** A VST3 instrument that builds on Linux and Windows, loads in a host, exposes the full
automatable parameter set, and round-trips its complete state through save/reload.
**Depends on:** Nothing (first phase)
**Research:** Unlikely (JUCE 8.0.12 already on the machine; MSVC 2022 Build Tools present on the
Windows host and reachable via WSL interop)

**Scope:**
- CMake project acquiring JUCE 8.0.12 via FetchContent with a `JUCE_PATH` local override
- VST3 + Standalone targets: instrument, `producesMidi=true`, `wantsMidiInput=true`
- Processor/editor skeleton: silent processor, fixed 1200×780 editor with 20:13 aspect constraint
- APVTS parameter tree — globals plus 7 params × 5 channels
- Pattern grid and active profile as a `ValueTree` child node, not automation lanes
- `getStateInformation` / `setStateInformation` lossless round-trip
- Windows VST3 built natively with MSVC through WSL interop, installed and loaded in a real host

**Plans:**
- [x] 01-01: CMake + JUCE + Linux VST3/Standalone target that builds and loads — complete 2026-09-06
- [x] 01-02: APVTS parameter tree + grid/profile state node + state round-trip — complete 2026-09-07
- [x] 01-03: Windows VST3 via MSVC through WSL interop + host load verification — complete 2026-09-07

### Phase 2: Sequencer clock ✅ Complete 2026-09-08

**Outcome:** A sample-accurate sixteenth-note sequencer with swing, correct in both internal and
host-synced modes, reading real pattern data through a lock-free handover. Step 0 locks to the host
bar in 4/4; host tempo, transport, loops and jumps are all followed. The four regional grooves are
generated from `data.js` and cross-checked against it on every build. 606 checks green under GCC,
Clang and MSVC. Still deliberately silent — nothing consumes the velocities until Phase 3.

**Goal:** Sample-accurate step advance driving nothing yet, correct in both internal and
host-synced modes, with the four profiles' pattern tables loaded and swappable.
**Depends on:** Phase 1 (APVTS parameters for bpm/swing/steps/sync; ValueTree grid storage)
**Research:** Unlikely (host-sync via `AudioPlayHead` is a well-trodden JUCE path)

**Scope:**
- Step counter advanced from the audio block's sample position, not a timer
- Swing on odd sixteenths: `delay = (swing/100) × 0.6 × stepDuration`
- `SYNC` on: follow `AudioPlayHead` PPQ, lock step 0 to the host bar
- Pattern tables for all four profiles ported verbatim from `data.js`
- 16/32 step tiling (`new[i] = old[i % oldLen]`)
- Lock-free double-buffer + atomic index for pattern table handover

**Plans:**
- [x] 02-01: Musical content — four profiles verbatim, velocity decoder, tiling, cross-check script — complete 2026-09-07
- [x] 02-02: Clock core — sample-accurate step advance from block position, swing, internal tempo — complete 2026-09-07
- [x] 02-03: Host sync — position-driven clock via `AudioPlayHead`, bar lock, transport follow — complete 2026-09-07
- [x] 02-04: Lock-free pattern handover — `SpinLock`/try-lock publication — complete 2026-09-08

**Split at 02-03 planning:** the original 02-03 carried both host sync and the pattern handover. They
are separate subsystems that fail in different ways — one is a timing question, the other a
concurrency question — and host sync alone is three tasks. Splitting keeps each plan independently
verifiable.

### Phase 3: Voices & mix bus ✅ Complete 2026-09-08

**Outcome:** The plugin makes its own sound, end to end. Eight voices — seven synthesised, zabumba
from three measured velocity layers — through `CACHAÇA` humanisation, a character bus, a limiter and
a squared-taper master. The four grooves were A/B'd against the prototype and approved, and the full
chain does not clip: 0.571 / 0.806 / 0.890 / 0.669 where the unlimited engine reached 1.454.
1092 checks green under GCC, Clang and MSVC; 92 negative controls across the phase.

**Goal:** The four profiles audibly match the prototype. Verified by A/B listening before any UI
work begins.
**Depends on:** Phase 2 (clock must trigger something)
**Research:** Unlikely (all eight voices fully specified in PLANNING.md)

**Scope:**
- Eight synth voices: zabumba, triângulo (velocity-split open/closed), pandeiro, ganzá, and
  bateria BB/CX/HH/TOM
- Per-channel gain → stereo pan, honouring VOL/PITCH/DECAY/PAN
- `CACHAÇA` humanisation: timing jitter, velocity variation, ghost-note probability
- Character bus (dry + lowpass → tanh → wet) for HI-FI / LO-FI / CICLOTRON™
- Limiter (−6 dB, 20:1, 2 ms/120 ms) and master with squared taper
- Mute / solo logic

**Plans:**
- [x] 03-01: `VoiceEngine` + seven synth voices + sampled zabumba + per-channel gain/pan/mute/solo — complete 2026-09-08
- [x] 03-02: `CACHAÇA` humanisation — per-step jitter on a 32 ms delayed origin, per-hit velocity variation, ghost notes — complete 2026-09-08
- [x] 03-03: Character bus (HI-FI / LO-FI / CICLOTRON™) + limiter + master, then A/B listening against the prototype — complete 2026-09-08

**Engine decided at planning: hybrid.** Zabumba plays the four user-supplied `ZAB_LOW` one-shots;
the other seven lanes are synthesised from PLANNING.md's Voice Specifications. This supersedes
"synthesised voices first, samples optional later" — that decision was recorded when no library
existed. The library's four tempo-locked loops stay out: a fixed 4-bar performance cannot carry
per-step velocity, ghost notes or jitter, and it has no coverage for bateria or the triângulo's
open/closed pair at all.

### Phase 4: UI shell ✅ Complete 2026-09-14

**Outcome:** The chassis reads as the prototype at 1×, 1.5× and 2× in both themes — header, five
channel strips and footer, every control a custom Component wired to a real parameter. Six plans,
2637 checks green under GCC, Clang and MSVC with `DISPLAY` unset, and three design cross-checks
comparing 130 lengths and 44 type-scale values against `forrobox.css`, `controls.js` and `app.js` on
every build. Four blocking visual checkpoints approved. `ids::outputMode` stopped being inert: six
VST3 output buses, per-channel routing, verified in Ableton Live 12.

The phase's recurring lesson was that a green suite proves nothing about a check that cannot fail —
roughly a dozen were found across the six plans by negative controls, `/code-review` and `/simplify`,
including two measurement instruments that could not report the difference they existed to measure.


**Goal:** The chassis reads as the prototype does — correct at 1×, 1.5× and 2×, in both themes,
with the two components that carry most of the look built and reusable.
**Depends on:** Phase 1 (parameter attachments)
**Research:** Unlikely (all geometry and colour specified in px and hex)

**Scope:**
- Fixed 1200×780 child with a global scale transform; 20:13 aspect constraint
- Design tokens for both themes; embedded OFL fonts as binary resources
- Custom `LookAndFeel`; header / matrix / sequencer / footer row structure
- Knob component: 270° sweep, track + value arc, flat indicator line, unipolar and bipolar
- Knob interaction: drag, shift-fine, wheel, type-to-set, reset, hover tooltip, arrow keys
- Step-pad component: recessed off, backlit on, velocity-as-opacity, ghost dot, beat marker
- **Added at Phase 4 planning:** the header and footer controls wired to real parameters — BPM,
  SYNC, ÷2/×2, transport, the 54 px SWING/CACHAÇA pair, the STYLE segmented control, MASTER,
  LIMITER and its GR meter, OUTPUT

**Plans:**
- [x] 04-01: Chassis + scale transform + both themes as cross-checked tokens + seven embedded font weights + headless pixel harness — planned 2026-09-09, closed 2026-09-11
- [x] 04-02: Knob — 270° sweep, track/value arc, bipolar variant, full interaction set from `controls.js`; the strip's full interior reserved and the 20 strip knobs attached — closed 2026-09-11
- [x] 04-03: Step pad + the strip's own controls (base btn, mute/solo, arrow, fader); the strip finished — closed 2026-09-12
- [x] 04-04: The header — logo lockup, BPM cluster with its own drag law, real transport, the two signature 54 px knobs in their recessed group, preset stub and the STYLE control — closed 2026-09-13
- [x] 04-05: The footer — MASTER fader, LIMITER with a live GR meter, DRAG MIDI stub, OUTPUT segmented; and the `HeaderBar`/`FooterBar` split, each bar owning its own layout — closed 2026-09-13
- [x] 04-06: Multi-out — five extra stereo buses in the VST3 bus layout and per-channel routing, so `ids::outputMode` drives something real — closed 2026-09-14

**Scope amended at planning.** ROADMAP originally gave the grid to Phase 5 and the side panel to
Phase 6 and left the header and footer controls owned by no phase, while Phase 4's goal is that the
chassis reads as the prototype. An unpopulated header does not, and the header holds the two
signature 54 px knobs — the Knob component's most important instance. 04-04 closes that gap rather
than a phase being inserted later.

**04-02's scope extended at planning, with the user's agreement.** ROADMAP named only the Knob
component. Two additions: the plan reserves the strip's ENTIRE interior stack from
`PLANNING.md:284-294` (hit visualiser, dividers, knob grid, cycler, mute/solo, ghost-prob row,
bateria dots) and fills only the knob grid; and it places the twenty strip knobs attached to real
parameters. The reason is 04-01's own miss — it computed the strip's content rect and discarded it,
so its "reserve their boxes" deliverable was unreachable by the plans that needed it. Reserving the
whole stack once means 04-03 and Phase 5 add components without re-flowing the strip.

**04-04 split into three at 04-04 planning, with the user's agreement.** The header alone is six
clusters and every new interaction law; the footer is four items that mostly reuse them. They are
two vertical slices, each independently verifiable, and the precedent is 02-03's split for the same
reason. Then `ids::outputMode` — whose fate the ROADMAP assigned to this plan — was decided as
**implement multi-out for real**, which is a VST3 bus-layout and per-channel routing change, not a
footer one. Bundling an audio-routing change into a footer plan would repeat the mistake 02-03's
split avoided: two subsystems that fail in different ways, in one plan. Phase 4 becomes six plans.

**Two phase boundaries settled at 04-04 planning.** The `STYLE` segmented control is DRAWN here and
reflects the persisted `activeProfile`, but clicking performs no reload — Phase 6's "full reload"
is that phase's headline deliverable. The GR meter IS wired for real, against ROADMAP's Phase 8
line: `MixBus::gainReductionDb` already exists with an atomic exchange accessor, so Phase 8's line
predates the data, and a dead meter beside a working `LIMITER` toggle would be the dishonest kind
of stub.

**04-05 carries a refactor the previous plan's UNIFY mandated.** `/simplify` recorded at 04-04 that
`Chassis` must not grow a third region without splitting first — 1354 lines, ~41% header-only. The
footer plan therefore opens with `HeaderBar`, as a pure move proved pixel-identical against the
committed renders, before a single footer control is added.

**Two deferrals recorded at 04-05 planning, not asked about.** `DRAG MIDI`'s 2.6 s idle pulse and
bobbing arrow (`PLANNING.md:499`) go to Phase 7 with the MIDI export — an animated call to action for
a control that does nothing is the loudest possible lie, and it would introduce this plugin's first
animation timer. And `OUTPUT` is drawn and attached for display but READ-ONLY until 04-06 lands the
routing, using the same `setReadOnly` treatment `Button` and `BpmField` already carry.

**04-03/04-04 divided by USAGE at 04-03 planning, with the user's agreement.** ROADMAP listed the
transport buttons and the STYLE segmented control under 04-03's button family, but both are used
only in the header and footer — which 04-04 owns and places. Building them a plan early would leave
two components unplaced and unproven in situ. They move to 04-04; 04-03 takes the step pad plus the
controls that land in the channel STRIP, and finishes it. Still four plans.

**Right-click settled at 04-02 planning by `/graphify`.** `PLANNING.md:370` says right-click resets
the knob; `PLANNING.md:876-878`, five hundred lines away, qualifies that — *"in a plugin, ensure
this doesn't collide with the host's parameter context menu (or move reset to `Alt`+click /
double-click and put automation options in the right-click menu, which is the DAW convention)"*.
Since double-click is already type-to-set, reset goes to **Alt+click** and right-click falls through
to the host. Spec-directed, not a deviation, and it retires PROJECT.md's standing constraint.

**Fonts settled at planning, with one verified refinement.** Space Grotesk is published as a
variable font ONLY (google/fonts carries no statics; the upstream repo has no SemiBold at all), and
JUCE 8.0.12's `Typeface` API exposes no variation-axis setter — so loading the variable font renders
every weight at its fvar default of **300, Light**. The four static weights are therefore instanced
offline with `fonttools` and committed, with their name tables patched, because the instancer leaves
name ID 1 as "Space Grotesk Light" for every weight. IBM Plex Mono ships real statics at all three
weights needed and is used as fetched. Both OFL files ship.

**Two decisions taken at 04-06 planning, with the user.** A per-channel stem carries that channel's
voices only — pre-character, pre-limiter, pre-master — so the five stems summed deliberately do NOT
equal the main mix; `tanh` is not distributive and the limiter acts on the sum. And the main bus keeps
the full mix in MULTI-OUT rather than going silent, because a host with the aux buses disabled would
otherwise produce silence with no indication why.

### Phase 5: Sequencer grid ✅ Complete (4/4 plans, 2026-09-16)

**Goal:** The sequencer is playable and legible — editing works, and the playhead and hit
visualisers respond to real triggers without touching the audio thread.
**Depends on:** Phase 3 (real triggers to visualise), Phase 4 (pad component)
**Research:** Unlikely

**Scope:**
- Five-row pad grid, 16/32 columns, click-to-toggle, `BATERIA` row edits caixa
- Bateria kit overlay for BB/CX/HH/TOM
- Continuous playhead sweep driven from step phase on a 60 fps UI timer
- Trigger FIFO from audio thread to UI; per-channel LED and activity meter with 0.82/frame decay
- Row isolate (visual only), mute/solo row dimming, dirty-state `CUSTOM` tag

**Plans:**
- [x] 05-01: The grid — five rows of pads that show the real pattern and edit it, plus the attachment
      lifetime guard extracted before a sixth copy — closed 2026-09-14
- [x] 05-02: One group-atomic publication to replace the three separate atomics, the continuous
      playhead, and the per-channel LEDs and activity meters — closed 2026-09-15
- [x] 05-03: The grid answers writers other than itself — `STEPS` 16/32 with pattern tiling, and
      refresh on host recall and steps automation — closed 2026-09-15
- [x] 05-04: The bateria kit overlay, row isolate and mute/solo dimming — closed 2026-09-16

**Split into three at Phase 5 planning, with the user's agreement.** The ROADMAP scope spans three
subsystems that fail in different ways — a pad grid that edits state, an audio-to-UI publication
path, and row state reflection — which is the division 02-03 and 04-04 both used. 05-02 carries the
one audio-thread change and PROJECT.md's Phase 2 item: `currentStep`, the packed velocities and
`emittedSteps` are separate atomics, ordered but not group-atomic.

**Then FOUR at 05-02 planning, with the user's agreement.** 05-01 handed forward two items its own
scope had excluded — the `STEPS` 16/32 buttons, which no plan had ever claimed, and the grid's
failure to refresh when a writer other than itself changes the pattern. I had recorded both against
05-02 at 05-01's close; counting the work at planning showed that makes 05-02 five tasks across
three subsystems including the audio thread. Both are message-thread pattern writes and neither has
anything to do with what the audio thread publishes, so they become 05-03 by the same test that
split 02-03 and 04-04 — two subsystems that fail in different ways do not share a plan. The overlay
work moves to 05-04.

**One divergence from the prototype is deliberate and goes to 05-02's checkpoint.** `movePlayhead`
(`app.js:719`) sets `transition: left <stepDur>ms linear` and lets the browser interpolate, so the
prototype glides to each step's centre and waits there when swing delays the next trigger. JUCE has
no CSS transition, so ours is driven from the clock's position and is linear in musical time. Which
reads better under swing is a judgement, and the user makes it at the checkpoint.

**Tiling is the PROCESSOR's, decided with the user at 05-03 planning.** The prototype has one path
to a step change — `setSteps` (`app.js:581`), a button click. A plugin has two, and the second is
host automation, which can arrive with no editor open. A UI-owned tiling would therefore make the
same automation produce a different groove depending on whether a window happened to be open. An
APVTS listener plus an `AsyncUpdater` behaves the same either way; the hop is required rather than
stylistic, because `parameterChanged` is called on whatever thread set the value — the audio thread
for automation — and the write takes a lock.

**And switching to 16 tiles nothing, which is not an omission.** The prototype truncates its array;
our storage is always 32 slots with `steps` as a view (02-01's decision, recorded in
`expandPattern`'s own doc: *"the window selects which slots are read; it never decides their
contents"*). Slots 16-31 stop being read, and because a later switch to 32 tiles over them, nothing
that survives is ever observable. Truncating would destroy work for no reachable benefit.

**The `CUSTOM` tag moves to Phase 6, decided with the user at 05-04 planning.** This ROADMAP line put
it in Phase 5, but the spec puts it in the SIDE PANEL: `app.js:277` builds it inside the
`REGIONAL PROFILES` section header and `PLANNING.md:314` describes it there. That region is a
reserved 280 px that is still empty and belongs to Phase 6. Shipping the tag here would mean starting
Phase 6's region inside a Phase 5 plan — the scope bleed that splitting 02-03 and 04-04 avoided — and
`dirty` only becomes meaningful once something can load a profile to be dirty against.

**Two overlay divergences decided with the user at 05-04 planning.** `css:556`'s
`backdrop-filter: blur(3px)` has no JUCE equivalent short of capturing the region and blurring it per
open, so the `--bg` 78% scrim the same rule specifies carries the separation alone and the divergence
is recorded where the scrim is painted. And the 200 ms slide-in IS built: 05-02's 60 Hz polls are
already the infrastructure, so it is no longer a new species of thing, and the entrance is one of the
few places the prototype's motion is load-bearing rather than decorative.

**One spec conflict resolved the established way.** `PLANNING.md:519` says the backdrop covers "the
matrix + side panel area"; `app.js:37` appends the subview to `#fb-window` and `css:554` is
`position: absolute; inset: 0`, so it covers the whole chassis. The design source wins over
PLANNING's prose — the ruling 04-03 made for `.pad.beat`'s duplicate declaration.

**The attachment lifetime guard opens 05-01.** PROJECT.md records it as worth doing early in Phase 5,
before a sixth copy; 04-05 opened the same way with the `HeaderBar` split `/simplify` had mandated.

### Phase 6: Side panel ✅ Complete (6/6 plans, 2026-09-20)

**Outcome:** Selecting a regional profile performs a correct full state reload from either entry
point, with the `CUSTOM` dirty tag, timbre rows, the `MIX` knob and a confirmation pad flash. Six
plans, 3822 checks green under GCC, Clang and MSVC, and a FOURTH cross-check — `verify-charset.py`
— that reads every string literal in `src/` and every test message literal.

Two items were judged and REJECTED with the user rather than built (the profile-load announcement
and `ViewState`), and in both cases the premise recorded for them turned out to be wrong when the
code was read. Three of the phase's recorded premises were overstated the same way.

The phase's recurring lesson extended Phase 4's: a green suite proves nothing about a check that
cannot fail — and neither does a comment. Across 06-04 to 06-06 the reviews found **five false
claims written into comments during the plans that wrote them**, two checks that could not fail,
a silent enrolment escape, a tokenizer bug in a checker built to prevent silence, and 20 mojibake
lines in a passing run. Each was caught by mutating the thing, not by reading its exit code.


**Goal:** Selecting a regional profile performs a correct full state reload without a click, a
glitch, or an audio-thread data race.
**Depends on:** Phase 2 (pattern table swap), Phase 4 (UI primitives)
**Research:** Unlikely

**Scope:**
- Profile list and `STYLE` segmented control, both driving the same reload
- Full reload: bpm, swing, cachaça, all patterns, bateria sub-patterns, mutes, timbre
- Message-thread parameter gestures; audio thread picks up tables via buffer swap
- Timbre rows with LED state; character `MIX` knob
- Confirmation pad flash on reload
- The `CUSTOM` dirty tag — moved here at 05-04 planning, because `app.js:277` builds it inside the
  side panel's preset row and shipping it in Phase 5 would have started this region early

**Plans:**
- [x] 06-01: `PatternPads` — one rectangle of pads for the grid and the overlay, extracted before
      the side panel makes a third copy. Its second item, publishing the channel gate, was judged
      and rejected with the user rather than built; see PROJECT.md
- [x] 06-02: The side panel — profile list, `CUSTOM` tag, timbre rows with LED state, `MIX` knob,
      `LOAD IR…` stub and the bundle footer — closed 2026-09-17
- [x] 06-03: Profile loading as a full state reload, driven from both the list and `STYLE`, with the
      dirty flag and the confirmation pad flash — closed 2026-09-20
- [x] 06-04: Accented text as real UTF-8 with the charset pinned in CMake, deleting the verifier's
      escape machinery and the twelve-character allowlist — closed 2026-09-20. Grew a fourth
      cross-check, `verify-charset.py`: `/code-review` found that the plan converted ~19 literals
      no test covered, and a mutation proved it (a mangled `CACHAÇA` ran 3778/3778 GREEN)
- [x] 06-05: The production tidies — `PatternPads` owns the whole flash, `SelectableTile`,
      `HitZone`, one `kUiPollHz`, `ids::lanes` as one array of structs — closed 2026-09-20.
      `ViewState` was judged and REJECTED at planning. The review passes found three false
      claims and two checks that could not fail IN THIS PLAN'S OWN WORK, plus a silent
      enrolment escape: hoisting the poll rates into `Surface.h` moved them out of
      `verify-geometry.py`'s coverage gate, which kept passing
- [x] 06-06: The test seams and the ChassisRig last — `HeaderBar::getStyleControl`, root-space child
      collection, then the rig — closed 2026-09-20. The reviews found this plan's own work had a
      verb with no caller, a claim proved on one example and generalised to 228, and a live
      instance of the bug it added a type to prevent

**Split into four at Phase 6 planning, with the user's agreement.** The cleanup is split across the
phase rather than done in one plan: 06-01 carries only the two items the side panel would otherwise
duplicate, and 06-04 carries the four that 06-02 and 06-03 reshape — the ChassisRig above all, whose
30 sites are the last thing that should be hoisted, not the first.

**Then SEVEN at 06-04 planning, with the user's agreement.** The line above named four items for
06-04. Counting them at planning found ELEVEN, recorded across three plans — 06-02 and 06-03 each
handed forward more than this line anticipated — and they span four subsystems that fail in
different ways: a source-encoding and build change, a processor/state design change, five production
hoists, and a test-only rig. That is the division 02-03, 04-04 and 05-02 all used. The order is
fixed by two dependencies rather than by preference: the charset fix goes FIRST because every plan
that ships more accented strings makes it bigger, and the ChassisRig goes LAST because its API is
shaped by the announcement (06-05) and by the two test seams that precede it in 06-07.

**Then SIX again at 06-05 planning, with the user — the announcement was judged and REJECTED, and
the premise I had recorded for it was wrong.** At 06-04 planning I wrote that a load arriving from
`setStateInformation`, a preset recall or a future undo "refreshes and flashes nothing". Reading
the code first killed it: three independent polls already follow a programmatic load, the missing
flash is what the spec ASKS for (`loadProfile(id, flash)` takes the flash as a parameter —
`app.js:113` and `:282` pass `true`, `boot` at `:757` passes `false`, because the flash confirms a
GESTURE), and `cyclePreset` — the "preset recall" I cited — does not call `loadProfile` at all.
Both production callers exist today and both are already correct. What remained was duplication,
and a counter would have bought it at the price of a flash that fires a poll interval late and a
headless test that must drive a poll to see it — the coupling argument that rejected the channel
gate at 06-01. The duplication is collapsed inside 06-05 instead. Phase 6 is seven plans minus one:
**six**.

**Ciclotron™'s visual treatment stays in Phase 8, confirmed with the user at Phase 6 planning.**
`PLANNING.md:617-640` gives it a chassis-wide `saturate/contrast` filter, a flickering scanline
overlay and chromatic aberration on its own name. Phase 6 ships the timbre rows, their LED state and
the `MIX` knob — the selection and the audio. The joke is polish, and it belongs beside the easter
egg and the settings menu.

**A cleanup plan comes first, and Phase 5's close fixed its order.** Six items, three of which this
phase's scope needs anyway: `PatternPads` (the overlay and the grid are two copies of "a component
showing a slice of the pattern", and both of 05-04's fixes were re-fixes of the grid's own bugs);
~~publishing the resolved channel gate~~ (judged and rejected at 06-01); a `HitZone` component so containers stop
hit-testing layout rectangles by hand; `ViewState`, which `PLANNING.md:676-677` already describes and
which is where `dirty` belongs; `ids::lanes` as one array of structs; and the ChassisRig LAST, at 30
sites, because what it should expose is downstream of the first and third.

### Phase 7: MIDI out ✅ Complete (3/3 plans, 2026-09-21)

**Outcome:** The groove leaves the plugin three ways — a cross-checked `.mid`, a native drag, and
live MIDI carrying the humanised performance. Three plans, 3906 checks green under GCC, Clang and
MSVC, and a FIFTH cross-check that RUNS the prototype's own `exportMIDI` under Node and compares 24
states byte for byte.

The phase's recurring lesson was about checks that pass for the wrong reason. 07-02's drag threshold
silently disarmed the existing test meant to guard dragging — it passed because no drag ever
started. 07-03's note-off balance check passed only *because* the bug it should have caught was
truncating notes; fixing the bug made the check fail. And an allocation assertion guarding the whole
audio-thread contract was structurally blind, because its own warm-up pre-grew the buffer it
measured. Each was found by mutating the thing, never by reading a green line.


**Goal:** The groove leaves the plugin — draggable as a `.mid` file and playable as live MIDI on
the plugin's output bus.
**Depends on:** Phase 2 (grid and bpm as the export source)
**Research:** Unlikely (`exportMIDI()` in `audio.js` is a working reference)

**Scope:**
- SMF type 0, PPQ 96, GM percussion map, 80% gate, muted channels excluded, ghosts excluded
- Drag-out via `performExternalDragDropOfFiles` with a temp `.mid`
- Live MIDI out on the plugin's MIDI bus
- Filename pattern `forrobox_<profile>_<bpm>bpm.mid`

**Plans:**
- [x] 07-01: The Standard MIDI File writer — type 0, PPQ 96, cross-checked byte for byte against
      the prototype's own `exportMIDI` run under Node ✅ 2026-09-21
- [x] 07-02: Drag-out via `performExternalDragDropOfFiles`, the filename, and the DRAG MIDI
      animation deferred here from 04-05 — the CTA stops lying ✅ 2026-09-21
- [x] 07-03: Live MIDI out on the plugin's bus — the only audio-thread change ✅ 2026-09-21

**Split into three at Phase 7 planning, with the user's agreement.** The ROADMAP scope names four
concerns that fail in different ways: a byte format (wrong ticks, and a delta-encoded stream shifts
everything after one wrong VLQ), an OS integration (temp-file lifetime, the host's drag mechanism),
a real-time path (`processBlock` emitting MIDI), and an animation. That is the division 02-03,
04-04 and 05-02 all used. The animation rides with the drag because an animated call to action for
a control that does nothing is what 04-05 deferred it to avoid.

**Live MIDI out emits the HUMANISED performance, decided with the user at Phase 7 planning.**
`PLANNING.md:827` only says "emit the same notes", which is ambiguous, and it matters: the FILE is
explicitly un-humanised — `exportMIDI` uses `step * stepTicks` with no swing term, and
`PLANNING.md:584` keeps ghosts out of the pattern entirely. Live MIDI reusing that would drift
against the plugin's own audio the moment swing or `CACHAÇA` is non-zero, so a doubled instrument
would play out of time with the groove it is doubling. Live MIDI therefore emits from the engine's
own trigger path — swing, jitter and ghosts included — and the file stays the stored grid. Two
different data paths, deliberately.

**Two 07-02 decisions taken with the user at planning.** `PLANNING.md:505` says "click downloads
the same file", which a plugin cannot do. CLICK opens `juce::FileChooser::launchAsync` with the
filename pre-filled — the faithful translation, and it must be the async form: `JUCE_MODAL_LOOPS_PERMITTED=1`
is set on the TEST target only, deliberately, because modal loops in a plugin are what that default
forbids. And the dragged temp file is written into a `forrobox` folder under the system temp
directory and swept on the NEXT export, NOT deleted in the drag's completion callback: that callback
fires when the DRAG ends, which is not the instant the receiving application has finished reading,
and a host that copies lazily would get a file that vanished underneath it — failing as a silently
empty MIDI track.

**The filename does not track the dirty flag.** `app.js:454` writes
`forrobox_${state.activeProfile || "custom"}_${state.bpm}bpm.mid`, and `markCustom()` sets `dirty`
without ever clearing `activeProfile` — so an edited CAMPINA still exports as
`forrobox_campina_<bpm>bpm.mid` in the prototype too.

**And `|| "custom"` is NOT dead here — I claimed it was, and 07-02 proved otherwise.** At planning I
wrote that `State::activeProfile` "only ever holds one of the four ids". `State::readFrom`
(`src/ForroBoxState.cpp:106-111`) preserves an unrecognised profile string VERBATIM, deliberately, so
that "a project saved by a newer build must not lose its profile". A host project carrying `../../x`
therefore produced `forrobox_../../x_132bpm.mid`, which `File::getChildFile` resolves — writing
outside the temp folder. The id is now sanitised by CHARACTER and falls back to `custom`, which is
the same word the prototype reaches for. A `jassert` did not cover it: asserts compile out of the
Release build that is the only one that ever opens someone else's project file.

**The reference implementation is RUN, not transcribed.** `PLANNING.md:825` names `exportMIDI()` in
`audio.js`, and `audio.js:311` assigns it to `window.FB_AUDIO`. Node 24 is on this machine with
`Blob` as a global, so the prototype's own function produces the expected bytes and the C++ is
compared against them — the same standing as `data.js` for the groove tables, where Phase 2 decided
"generated, never transcribed, and cross-checked on every build".

**The live note-off gate is SELECTABLE, decided with the user at 07-03 planning.** The file export
gets its gate for free — 19 of 24 ticks — but a humanised live hit has no step boundary to measure
against: it is jittered off the grid, and a ghost is jittered again. Two answers are defensible and
both ship, as a new global CHOICE parameter `midi_gate`: **FIXED** (40 ms, tempo-independent, what
hardware drum machines send, and the default) and **STEP** (80% of the current step, so a live note
and an exported note agree at a steady tempo). This is the plugin's **46th** parameter — the count is
asserted in four places in `tests/StateRoundTripTest.cpp` as a deliberate tripwire, and moving it to
46 is a conscious act. It gets no UI: `PLANNING.md` specifies no such control and the design mandate
forbids inventing one, so hosts expose it generically and Phase 8's settings menu can attach later.

**Live MIDI follows `audible` — mute AND solo — and that differs from the file on purpose.**
`exportMIDI` reads `chans[id].mute` and never looks at solo, which 07-01 wrote into `ChannelGate`'s
header. Live MIDI taps `VoiceEngine::playVelocity`, which sits downstream of the engine's own
audibility gate, so what leaves as MIDI is exactly what you hear. Two rules, two reasons.

### Phase 8: Polish ✅ Complete (5/5 plans, 2026-09-23)

**Outcome:** The five details that make it feel finished. A fresh instance plays the profile it
claims; a gear menu carries five global settings and an ABOUT panel; three display fonts are
selectable; `CACHAÇA` past 65 washes the chassis and past 88 sways it; and `CICLOTRON™` degrades the
whole interface. 4410 checks green under GCC, Clang and MSVC.

The phase's recurring lesson was about instruments that cannot see what they exist for. 08-04's
reviews found a wash check that passed with every draw disabled and a baseline fix whose mutation
survived the entire suite; 08-05's found two animations running at DOUBLE SPEED, invisible because
both drivers call one `advance` and every check drives it by hand. Three wall-clock thresholds were
written and all three failed on a busy machine rather than on the code — the structural claims they
proxied for are asserted by counters now.

**Goal:** The details that make it feel like a finished instrument rather than a working one.
**Depends on:** Phase 6
**Research:** Unlikely

**Scope:**
- **A fresh instance plays the profile it claims** — added at Phase 8 planning, see below
- `CACHAÇA` easter egg: warm wash from 65%, sway and `♪ NO PONTO` at 88%
- Ciclotron™ treatment: scanline flicker, chromatic aberration on the label, blinking sub-label
- Settings/gear menu: theme, corner radius, accent intensity, display font, default step count
- **An ABOUT panel** — authorship and the project link, added at 08-01 UNIFY, see below

**Plans:**
- [x] 08-01: A fresh instance loads CAMPINA for real, without clobbering a restored project
      ✅ 2026-09-21
- [x] 08-02: The gear menu — a global settings store, four settings live, and the ABOUT panel
      with clickable links ✅ 2026-09-22
- [x] 08-03: The display font — JetBrains Mono and Space Mono embedded, the fifth setting wired,
      and `Face::monoSemiBold` deleted ✅ 2026-09-22
- [x] 08-04: The `CACHAÇA` easter egg — the wash from 65, the sway, the orange readout and
      `♪ NO PONTO` from 88 ✅ 2026-09-22
- [x] 08-05: The Ciclotron™ treatment — the chassis degrades, the scanlines flicker, and the
      character gets its trademark and its aberration ✅ 2026-09-23

**Split into two at 08-02 planning, with the user's agreement on the SCOPE.** The user chose all
five settings plus ABOUT over a four-setting recommendation. The scope is honoured in full; the
SPLIT follows this project's own repeated test, applied at 02-03, 04-04, 05-02, 06-04 and 07-01 —
two subsystems that fail in different ways do not share a plan. 08-02 is a vertical slice that works
end to end; 08-03 is an asset and build change that happens to add one menu item.

**Two decisions taken with the user at 08-02 planning, both to minimise invented design.** The
settings are a `juce::PopupMenu` and the ABOUT panel reuses `KitOverlay`'s approved treatment —
because `PLANNING.md:902` calls `tweaks-panel.jsx` "prototype-only scaffolding — **not part of the
plugin design**", and no gear icon exists in `forrobox.css`, `app.js` or the chassis layout. The
gear sits beside the logo in the header. A `PopupMenu` is drawn by JUCE against the existing
`LookAndFeel`, so the only invented thing in the whole plan is one button.

**Phase 4 built this plan's seams and said so.** `LookAndFeel.h:59`: *"Both tweakables are
user-facing in Phase 8's settings menu, so they are settable now rather than being constants that
have to be dug out later."* `setMode`, `setCornerRadius` and `setAccentIntensity` all exist with no
callers. All 101 colour reads go through `lnf.token()` at paint time, so a theme switch is a setter
plus a repaint — verified at planning, not assumed.

**The global store is the plugin's FIRST.** There is no `PropertiesFile`, no `ApplicationProperties`
and no `getUserApplicationDataDirectory` anywhere in `src/` — everything to date is per-project APVTS
and `ValueTree` state. `PLANNING.md:853` requires these settings to persist globally, so this is a
new subsystem rather than a variation on the existing one, and it is shared mutable state across
every plugin instance in the host's process.

**THE DISPLAY FONT'S "SPEC PROBLEM" WAS SMALLER THAN FIRST RECORDED, and the correction matters
more than the original claim.** At 08-02 planning I wrote that the type scale uses `monoRegular` 400,
`monoMedium` 500 and `monoSemiBold` 600 "across ~20 rows", so Space Mono — which publishes only 400,
700 and their italics, and is not variable — could supply neither 500 nor 600. Two of those three
statements were wrong.

**`Face::monoSemiBold` has ZERO users.** It is embedded and registered in `Typography.cpp`'s resource
table and no row of `typeSpecs` asks for it; only `monoRegular` and `monoMedium` appear. So a
switchable display font needs TWO weights, not three.

**And the one genuinely missing weight is settled by the design source, not by a trade-off.** For a
target weight of 500 with only {400, 700} available, CSS Fonts 4 §5.2 has the browser try 500 exactly,
find nothing, then walk DOWN — landing on 400. So rendering Space Mono's 500 as its 400 is what the
prototype does, not a compromise against it. Same resolution this project has used for every spec
conflict since 04-03's `.pad.beat`: the running design source wins.

So 08-03 embeds JetBrains Mono instanced at 400 and 500 (04-01's Space Grotesk machinery, already in
`scripts/build-fonts.py`), embeds Space Mono's 400 and maps its 500 to it, and DELETES the unused
`monoSemiBold` — both confirmed with the user at 08-03 planning.

**Scope amended at Phase 8 planning, and it opens the phase.** The three lines above are decoration;
this one is a correctness fix against PROJECT.md's own Success Metric — *"time from plugin open to a
usable groove: under 30 s, zero config"* — which is the only metric still reading **Not started**. A
fresh instance lights CAMPINA, shows an empty grid and plays silence. Recorded at Phase 5's close,
assigned to Phase 6, and Phase 6 closed without it; **the user confirmed it in a real host on
2026-09-21**, during 07-03's checkpoint.

**The prototype settles the fix, against the first instinct.** The user proposed starting in CUSTOM.
`app.js:757`'s `boot()` calls `loadProfile("campina", false)` — the prototype genuinely LOADS the
profile at startup and the `false` only suppresses the confirmation flash. Starting empty-and-CUSTOM
would fail the metric outright. The claim is made true rather than retracted.

**An ABOUT panel was added to scope at 08-01 UNIFY, at the user's request, and it is the phase's
first INVENTED control.** `ABOUT.md` shipped immediately — authorship, the links and the GitHub URL
are documentation and need no plan. The in-plugin panel does need one, and it needs a decision
recorded with it: `PLANNING.md` specifies no About control anywhere, and PROJECT.md's design mandate
is that a control the design source does not specify is not invented. The user asked for it
explicitly, which is the "explicit decision" that mandate requires — so it is a sanctioned deviation
rather than a silent one, and this line is where that is written down. It belongs behind the
settings/gear menu above rather than on the chassis, so the two plans should be planned together or
in that order.

**Authorship, settled at 08-01 UNIFY.** Forró Box is by **Carlos Eduardo Batista**
([npiq.cc](https://npiq.cc/)) and **Esmeraldo Filho**
([chicocorrea.bandcamp.com](https://chicocorrea.bandcamp.com/)), with no roles declared — the user's
choice. Note that `README.md`'s existing sample credit names **Chico Corrêa**, which is the same
person under an artist name; that line was left as it stands because it is a statement about the
samples, not about authorship.

**Four things are already right, which makes the fix small.** The parameter defaults were chosen from
CAMPINA and match it exactly — bpm 132, swing 38, cachaça 22, timbre HI-FI. What is missing is only
the GRID and BATERIA's mute. And `ForroBoxAudioProcessor::loadProfile` already exists and is already
correct, including the parameter-before-pattern order a `/code-review` finding produced.

**The real cost is the test suite, and it was measured at planning rather than discovered.** The
suite builds a processor **95 times** and `VoiceTest.cpp` alone calls `setStep` **63 times**, every
one starting from an empty grid by accident. The fix lands at the RIGS — `AudioRig` is used 77 times
and `ChassisRig` 34 — so roughly three seams cover it, and the handful of direct constructions each
need a judgement rather than a blanket clear: a test named for a FRESH instance may now legitimately
want the groove.
- ~~GR meter wired to real limiter reduction~~ — **done in 04-05.** `MixBus::gainReductionDb` already
  existed with an atomic exchange accessor, so this line predated the data, and a dead meter beside a
  working `LIMITER` toggle would have been the dishonest kind of stub

### Phase 9: Content & convolution

**Goal:** The three things that are still stubs when you open the plugin — one groove per profile,
a pattern cycler that cycles nothing, and a `LOAD IR…` button that does nothing.
**Depends on:** Phase 6 (the profile reload), Phase 3 (the character bus the convolution joins)
**Research:** Unlikely (`juce::dsp::Convolution` is in a module already linked)

**Scope:**
- **The groove tables extracted to `profiles.json`**, read by both the prototype and the plugin
- **More grooves per regional profile**, and a review of the four that exist
- **Per-channel pattern slots** — `PAT 01`–`08`, `PLANNING.md:844`
- **The preset system** — the top cycler, `PLANNING.md:843`
- **`LOAD IR…` and the convolution stage**, with `MIX` as its wet amount — `PLANNING.md:841`

**Plans:**
- [x] 09-01: `profiles.json` — the extraction, the cross-check reading JSON instead of regexes, and
      an audit of the four grooves that exist ✅ 2026-09-23
- [ ] 09-02: More grooves per profile — the content, drafted and put in front of a percussionist
- [ ] 09-03: Per-channel pattern slots — `PAT 01`–`08` as real storage
- [ ] 09-04: The preset system — the top cycler saves and loads full plugin state
- [ ] 09-05: `LOAD IR…` — an impulse response through `juce::dsp::Convolution`, `MIX` as its wet

**Added after Phase 8, from a pre-release review with the user.** Every phase to date built a
mechanism; this one is the first that is mostly CONTENT, and the three gaps were found by opening
the plugin and looking at it rather than by reading a spec.

**Three decisions taken with the user before planning.**

**The groove tables leave `data.js`, into a `profiles.json` both read.** This is the root fix
`STATE.md` has carried as deferred since Phase 2 — `verify-profiles.py` regex-parses `data.js` with
a hand-written brace matcher, and every groove added makes that worse. The user chose it over
editing `data.js` (which their own constraints mark read-only) and over letting new content live in
C++ where nothing would check it. Cost is front-loaded: the extraction is its own plan and the
cross-check is rewritten.

**BOTH cyclers become real, and they are different features.** `PLANNING.md:843` specs the top one
as a preset system saving full plugin state; `:844` specs the per-strip `PAT 01`–`08` as eight
storable patterns per CHANNEL, so channels can run variations of different lengths. Two subsystems
that fail in different ways, so two plans — 02-03's test, applied for the seventh time.

**The factory presets ARE the per-profile grooves, and this is the reading that reconciles the
two.** The user asked for "more patterns per regional profile", pointing at a cycler reading
`PÉ-DE-SERRA 01` while CAMPINA GRANDE was selected — and then chose the spec-faithful preset
system. Those are the same thing if the factory bank is organised by profile: CAMPINA GRANDE offers
`PÉ-DE-SERRA 01…N`, CARUARU its own list. `data.js:150`'s eight labels are rhythm names —
`BAIÃO SECO`, `XOTE LENTO`, `XAXADO 88` — not user slots, which supports it. Recorded as an
ASSUMPTION rather than a settled decision: it is stated here so 09-04 can be corrected at planning
rather than after.

**The musical content is drafted, not authored.** Judging whether a baião is right is a domain call
and Esmeraldo Filho is the percussionist. 09-02 derives grooves from the four that exist and from
each profile's own description, writes them to be easy to audition and revise, and puts them in
front of a human before they ship. Nothing in this project will claim a groove is authentic on my
authority.

---
*Roadmap created: 2026-09-06*
*Last updated: 2026-09-20 — Phase 6 is six plans; the load announcement was judged and rejected at 06-05 planning*
