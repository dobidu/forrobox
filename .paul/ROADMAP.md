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
Status: In progress
Phases: 5 of 8 complete (63%)

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
| 5 | Sequencer grid | 4 | In progress (3/4) | - |
| 6 | Side panel | TBD | Not started | - |
| 7 | MIDI out | TBD | Not started | - |
| 8 | Polish | TBD | Not started | - |

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

### Phase 6: Side panel

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
- [ ] 06-04: The remaining cleanup — `HitZone`, `ViewState`, `ids::lanes` as one array of structs,
      and the ChassisRig last

**Split into four at Phase 6 planning, with the user's agreement.** The cleanup is split across the
phase rather than done in one plan: 06-01 carries only the two items the side panel would otherwise
duplicate, and 06-04 carries the four that 06-02 and 06-03 reshape — the ChassisRig above all, whose
30 sites are the last thing that should be hoisted, not the first.

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

### Phase 7: MIDI out

**Goal:** The groove leaves the plugin — draggable as a `.mid` file and playable as live MIDI on
the plugin's output bus.
**Depends on:** Phase 2 (grid and bpm as the export source)
**Research:** Unlikely (`exportMIDI()` in `audio.js` is a working reference)

**Scope:**
- SMF type 0, PPQ 96, GM percussion map, 80% gate, muted channels excluded, ghosts excluded
- Drag-out via `performExternalDragDropOfFiles` with a temp `.mid`
- Live MIDI out on the plugin's MIDI bus
- Filename pattern `forrobox_<profile>_<bpm>bpm.mid`

### Phase 8: Polish

**Goal:** The details that make it feel like a finished instrument rather than a working one.
**Depends on:** Phase 6
**Research:** Unlikely

**Scope:**
- `CACHAÇA` easter egg: warm wash from 65%, sway and `♪ NO PONTO` at 88%
- Ciclotron™ treatment: scanline flicker, chromatic aberration on the label, blinking sub-label
- Settings/gear menu: theme, corner radius, accent intensity, display font, default step count
- ~~GR meter wired to real limiter reduction~~ — **done in 04-05.** `MixBus::gainReductionDb` already
  existed with an atomic exchange accessor, so this line predated the data, and a dead meter beside a
  working `LIMITER` toggle would have been the dishonest kind of stub

---
*Roadmap created: 2026-09-06*
*Last updated: 2026-09-16 — Phase 5 complete; the sequencer is playable, legible and reaches every lane*
