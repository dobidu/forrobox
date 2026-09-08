---
description: "Producers get authentic, human-feeling Brazilian forró percussion grooves inside their DAW without hiring a percussionist or programming every hit by hand."
type: Project
about: "Forró Box"
---

# Forró Box

## What This Is

Forró Box is a rhythm-production instrument plugin (VST3) for Brazilian forró percussion. It is a
step-sequencer-driven drum machine with five instrument channels — zabumba, triângulo, pandeiro,
ganzá and bateria — four regional groove profiles that reload the entire plugin state, a global
humanisation control (`CACHAÇA`), a swing engine, three global timbre characters, and MIDI export by
drag-out. A high-fidelity HTML/CSS/JS prototype exists in this repo and is the design source of
truth; the deliverable is a native plugin, not a port of the prototype's code.

## Core Value

Producers get authentic, human-feeling Brazilian forró percussion grooves inside their DAW without
hiring a percussionist or programming every hit by hand.

## Current State

| Attribute | Value |
|-----------|-------|
| Type | Application (audio plugin) |
| Version | 0.1.0-dev |
| Status | Sequencer complete and silent. Phase 3 planned: hybrid voice engine, 3 plans |
| Last Updated | 2026-09-08 |

## Requirements

### Core Features

- Five-channel step sequencer (16/32 steps) with per-step velocity, ghost notes and pattern tiling
- Four regional groove profiles (Campina Grande, Caruaru, Petrolina, Universitário) that perform a
  full state reload — BPM, swing, cachaça, all patterns, mutes and timbre
- `CACHAÇA` humanisation: timing jitter, velocity variation and ghost-note probability from one knob
- Synthesised percussion voices with per-channel VOL / PITCH / DECAY / PAN, plus three global timbre
  characters (HI-FI, LO-FI, CICLOTRON™) through a dry/wet character bus into a limiter
- MIDI export by drag-out to a DAW track (SMF type 0, GM percussion map) and live MIDI out

### Validated (Shipped)

- [x] HTML/CSS/JS design prototype — full UI, Web Audio voice sketches, sequencer, MIDI export
- ✓ VST3 instrument that builds on Linux and Windows and loads in a DAW — Phase 1
- ✓ Full automatable parameter surface: 45 params in 6 groups, IDs fixed — Phase 1
- ✓ Lossless state round-trip, hardened against malformed project data — Phase 1
- ✓ Audio-thread contract established: no allocation, no locks, no I/O in `processBlock` — Phase 1
- ✓ Repeatable two-platform build; test suite green under GCC, Clang and MSVC — Phase 1
- ✓ Four regional groove profiles generated from `data.js` and cross-checked against it on every
      build — Phase 2
- ✓ Sample-accurate sixteenth-note clock with swing, driven from the audio block's position — Phase 2
- ✓ Host sync: host tempo and transport followed, step 0 locked to the host bar, loops and jumps
      handled — Phase 2
- ✓ Internal transport, deliberately outside both the parameter surface and persisted state — Phase 2
- ✓ Lock-free pattern handover: the audio thread reads the grid with no lock, no allocation and no
      torn read — Phase 2

### Active (In Progress)

- [ ] Voices & mix bus — seven synth voices plus a sampled zabumba, `CACHAÇA` humanisation,
      character bus and limiter (Phase 3, 3 plans). Engine decision settled 2026-09-08: hybrid

### Planned (Next)

Suggested implementation order from the handoff (adapted for the native-JUCE GUI path):

- [x] Plugin skeleton + APVTS parameter tree + state persistence — Phase 1
- [x] Sequencer clock (internal, then host-synced) + the four profiles' pattern tables — Phase 2
- [ ] Voices + per-channel routing + limiter/master — verify grooves sound right before UI
- [ ] UI shell: chassis, scaling, design tokens/themes, Knob and step-pad components
- [ ] Sequencer grid + playhead + per-channel hit visualisers
- [ ] Side panel: profile loading (full state reload) + timbre characters
- [ ] MIDI export / drag-out + live MIDI out
- [ ] Easter egg, Ciclotron treatment, settings menu

### Emerged During Phase 2

- [ ] One published struct for the audio→UI channel. `currentStep`, the packed velocities and
      `emittedSteps` are separate atomics; the step and its velocities are ordered but not
      group-atomic. Phase 5's trigger FIFO is where this belongs
- [ ] Non-4/4 meters rotate the pattern: a 16-step pattern cannot both fit a 12-step bar and stay 16
      steps. Pinned by tests; a "pattern length follows the meter" option would be the real answer
- [ ] The loop-point split needs the host to report loop points. Hosts that omit them get the
      backwards-jump re-anchor, which prevents duplicates but cannot recover the step at the wrap
- [ ] Split `LockedState` into read and write handles once Phase 4's editor exists, which would
      delete `publishIfChanged`

### Emerged During Phase 1

- [ ] Install-location discovery generalised beyond Ableton, or `FORROBOX_VST3_DIR` documented as
      the supported path for other hosts
- [x] Decide the sample library's architectural role before Phase 3 — settled 2026-09-08: the four
      zabumba one-shots ship as velocity layers, the four tempo-locked loops do not ship
- [ ] Adopt `juce::UnitTest` when the suite next grows, rather than the hand-rolled harness

### Out of Scope

- Line-by-line port of the prototype's JavaScript — the prototype is a design reference; correct
  plugin practice (threading, sample-accurate timing, automation, state persistence) wins wherever
  the two conflict
- WebView GUI (`juce::WebBrowserComponent`) — evaluated and rejected in favour of native JUCE
- The prototype's React tweaks panel — becomes a native settings/gear menu instead
- Dynamic non-aspect-preserving window resizing — the chassis is a fixed 20:13 design that scales
- Decorative Brazilian iconography, wooden panels, skeuomorphic 808-clone skins, São João palettes
  (explicitly forbidden by the design brief)
- An accented product name in VST3 metadata — JUCE corrupts it at the module-info layer, so the
  display name is ASCII `Forro Box`. Accented parameter and group names are unaffected and kept
- `pluginval` and other new tooling for v0.1 — the DAW's own scan and load is the load proof

## Target Users

**Primary:** Music producers and beatmakers working in a DAW who want forró/baião percussion
- Comfortable with grooveboxes and step sequencers; expect plugin conventions to hold
- Want a usable groove within 30 seconds of opening the plugin (zero-config path must work)
- Two coexisting mental models: deterministic groovebox (`CACHAÇA` at 0) and generative (raised)

**Secondary:** Brazilian regional-music producers who will judge the grooves for authenticity

## Context

**Business Context:**
Differentiated by behaviour rather than decoration — the regional groove profiles and humanisation
are the product. Positioned against generic drum machines and against novelty "world percussion"
sample packs. A light (OP-1 cream) theme is a deliberate differentiator in a dark-only plugin market.

**Technical Context:**
The design prototype in this repo is the visual and behavioural specification, documented in full in
`PLANNING.md` (handoff) and `uploads/UIUX.md` (original brief). `data.js` holds the musical content
and is directly portable. `audio.js` is a sound-design sketch, not a DSP reference. Real-time audio
constraints (no allocation or locks on the audio thread) govern the architecture.

## Constraints

### Technical Constraints

- Real-time audio thread: no allocation, no locks. Sequencer advance, voice triggering, DSP and
  limiter run there; all GUI on the message thread reading atomics/lock-free FIFO
- Profile loading touches many parameters at once — message thread via APVTS, with the audio thread
  picking up new pattern tables via double-buffer or `AbstractFifo` swap, never in-place mutation
- Triggers must be sample-accurate from the audio block's sample position, and lock step 0 to the
  host bar/PPQ via `AudioPlayHead` when `SYNC` is on
- Pattern grid and active profile are state, not parameters — persisted in the APVTS `ValueTree`
  rather than exposed as 160+ automation lanes
- Fixed 1200×780 design at aspect ratio 20:13; scale via a global transform on a fixed-size child
- Fonts (Space Grotesk, IBM Plex Mono) embedded as binary resources, not loaded from a CDN
- Knob right-click reset must not collide with the host's parameter context menu

### Business Constraints

- High-fidelity design mandate: colours, typography, spacing, sizing and motion in the prototype are
  final intent, not placeholders
- All instructional UI copy stays in Brazilian Portuguese; technical/musical terms stay untranslated

### Compliance Constraints

- Bundled fonts must remain OFL-licensed and be attributed accordingly

## Key Decisions

| Decision | Rationale | Date | Status |
|----------|-----------|------|--------|
| JUCE 8 (`AudioProcessor` + `AudioProcessorEditor`), VST3 target | Handoff-recommended stack; mature VST3 support and parameter/state plumbing | 2026-09-06 | Active |
| Native JUCE GUI (custom `LookAndFeel` + hand-drawn Components), not WebView | Correct DPI handling, low overhead, no web runtime, real parameter attachments; some DAWs are fussy about embedded webviews | 2026-09-06 | Active |
| Instrument plugin: `producesMidi=true`, `wantsMidiInput=true`, synth flag on | Groove must be able to drive other instruments as well as sound on its own | 2026-09-06 | Active |
| ~~Synthesised voices first, samples optional later~~ | No sample library to ship yet; voice specs are fully documented in the handoff | 2026-09-06 | **Superseded 2026-09-08** |
| Hybrid engine: zabumba from the four `ZAB_LOW` one-shots, the other seven lanes synthesised | The one-shots are genuine single hits and sound better than a synthesised zabumba, but the library covers nothing else — no bateria pieces, no triângulo open/closed pair, and its four other files are tempo-locked 4-bar loops that cannot carry per-step velocity, ghost notes or `CACHAÇA` jitter | 2026-09-08 | Active |
| The four `ZAB_LOW` one-shots are velocity layers, not round-robin variants, and the mapping is derived from measured RMS | Measured: peaks cluster within 1.1 dB across 01-03 then fall 20 dB at 04, while RMS spreads 10x. Round-robin would put a 20 dB jump between adjacent hits. Hard-coding the file order would silently reorder if a sample is replaced | 2026-09-08 | Active |
| Continue in PAUL rather than re-incubating in SEED | `PLANNING.md` already carries the full spec and an implementation order | 2026-09-06 | Active |
| Prototype kept in-repo as design reference, not as a build target | Fastest way to check visual and behavioural intent against the plugin | 2026-09-06 | Active |
| Display name is ASCII `Forro Box`; accented parameter/group names kept | JUCE's `infoW.fromAscii()` sign-extends bytes, corrupting the accent in VST3 class metadata a scanning host reads. Parameter names travel a correct UTF-16 path | 2026-09-07 | Active |
| 45 automatable parameters in 6 groups; pattern grid and profile as a `ValueTree` child | Fixed IDs for Phase 4 to attach to; 256 grid values must not become automation lanes | 2026-09-07 | Active |
| Non-automatable state reachable only through an RAII lock handle | A host may call `setStateInformation` off the message thread; a lock covering only some paths advertises safety it does not provide | 2026-09-07 | Active |
| Install target discovered from the host's own scanner record | Assuming a convention-permitted folder installed the plugin where nothing reads. `FORROBOX_VST3_DIR` overrides | 2026-09-07 | Active |
| Windows VST3 built with MSVC 2022 through WSL interop: UNC source, local build dir | MSVC is JUCE's first-class path; UNC reads are fast, its artifact writes are not | 2026-09-07 | Active |
| Git repository with per-group commits; nothing pushed to any remote | Phase transition needs a commit, and DSP phases need bisectable history | 2026-09-07 | Active |
| Groove tables generated from `data.js`, never transcribed, and cross-checked on every build | A wrong digit is not a crash or a failed test — it is a groove that is subtly wrong with no way to know which digit. My own first extractor mislabelled a profile | 2026-09-07 | Active |
| The clock is driven by a musical position and a rate, and holds no position of its own | `PLANNING.md` requires alignment derived from absolute host PPQ each block, not a local counter. One code path for synced and free; host loops and jumps have nothing to contradict | 2026-09-07 | Active |
| Partition equality is: step indices exact, sample positions within one sample | Bit-identity is unreachable once position-driven, and host sync requires that. Stated in the tests rather than quietly relaxed | 2026-09-07 | Active |
| `playing` is neither an automatable parameter nor persisted state | A play toggle on an automation lane fights the host transport, and a plugin that resumes playing when a project opens is hostile | 2026-09-07 | Active |
| Negative host positions accepted, so the groove plays through a count-in on the same grid | The windowed mapping handles them; refusing them would silence the count-in for no benefit | 2026-09-07 | Active |
| Pattern handover uses JUCE's documented `SpinLock`/`GenericScopedTryLock` pairing, not a hand-rolled seqlock | `juce_Convolution.h:250-253` names it for exactly this job. The seqlock needed atomic bytes (30x the copy cost) and could latch its generation odd, wedging the audio thread on a stale table for a session | 2026-09-08 | Active |
| Every writer publishes automatically, via the state handle's destructor | "Every writer must remember" is the invariant that fails. Both production state methods had bypassed it | 2026-09-08 | Active |

## Success Metrics

| Metric | Target | Current | Status |
|--------|--------|---------|--------|
| Grooves judged authentic against the prototype (A/B listening) | All 4 profiles | - | Not started |
| Timing accuracy of triggers | Sample-accurate; step 0 locked to host bar when synced | Achieved (4/4). Step sequence independent of buffer size; positions within one sample across partitions | Achieved |
| State round-trip (profile, dirty flag, full grid, step count, all params) | Lossless save/reload | Lossless — 606 checks, 3 compilers | Achieved |
| Time from plugin open to a usable groove | Under 30 s, zero config | - | Not started |
| Audio-thread safety | No allocation or locks in the audio callback | Zero allocations measured by counter; the only lock is a try-lock the audio thread never waits on | On track |
| DAW validation | Passes VST3 validator; loads in Reaper, Live, Bitwig | Loads in Ableton Live 12; validator deferred | On track |
| UI fidelity vs. prototype | Both themes match closely at 1×, 1.5×, 2× | - | Not started |

## Tech Stack / Tools

| Layer | Technology | Notes |
|-------|------------|-------|
| Framework | JUCE 8 | `juce_audio_plugin_client`, VST3 target |
| Plugin format | VST3 | Instrument, MIDI in and out |
| Parameters | `juce::AudioProcessorValueTreeState` | Automatable params; grid/profile as a child node |
| State | APVTS `ValueTree` | `getStateInformation` / `setStateInformation` |
| Timing | `AudioPlayHead` (PPQ) or an internal position, both feeding one position-driven clock | Host-synced when `SYNC` is on |
| Pattern handover | `juce::SpinLock` + `GenericScopedTryLock` | Latest-value publication to the audio thread; JUCE's documented pairing |
| DSP | Custom C++ voices (7 lanes) + sample playback (zabumba) | Per handoff voice specifications; character bus + limiter |
| Samples | Four `ZAB_LOW` one-shots embedded via `juce_add_binary_data` | 48 kHz / 24-bit stereo, resampled once at `prepareToPlay`; ~450 KB |
| GUI | Custom `LookAndFeel_V4` + hand-drawn Components | Fixed 1200×780 child, global scale transform |
| Fonts | Space Grotesk, IBM Plex Mono (OFL) | Embedded as binary resources |
| Design reference | HTML/CSS/JS prototype in repo | `Forró Box (standalone).html`, `forrobox.css`, `data.js`, `audio.js`, `controls.js`, `app.js` |

## Specialized Flows

See: .paul/SPECIAL-FLOWS.md

Quick Reference:
- `/code-review` (required) → Audio-thread / DSP / processor code
- `/graphify` (required) → Spec and codebase lookup
- `/impeccable` (optional) → GUI fidelity vs. the prototype
- `/simplify` (required) → Quality cleanup before closing a phase

## Links

| Resource | URL |
|----------|-----|
| Design project | https://claude.ai/design/p/2711c990-e0c0-4599-954d-27093aec88f5 |
| Handoff spec | PLANNING.md |
| Original UI/UX brief | uploads/UIUX.md |

---
*PROJECT.md — Updated when requirements or context change*
*Last updated: 2026-09-08 — Phase 3 planned; hybrid engine decided*
