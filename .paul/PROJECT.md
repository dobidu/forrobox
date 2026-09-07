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
| Version | 0.0.0 |
| Status | Prototype (HTML/CSS/JS design reference complete) |
| Last Updated | 2026-09-06 |

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

### Active (In Progress)

None yet.

### Planned (Next)

Suggested implementation order from the handoff (adapted for the native-JUCE GUI path):

- [ ] Plugin skeleton + APVTS parameter tree + state persistence
- [ ] Sequencer clock (internal, then host-synced) + the four profiles' pattern tables
- [ ] Voices + per-channel routing + limiter/master — verify grooves sound right before UI
- [ ] UI shell: chassis, scaling, design tokens/themes, Knob and step-pad components
- [ ] Sequencer grid + playhead + per-channel hit visualisers
- [ ] Side panel: profile loading (full state reload) + timbre characters
- [ ] MIDI export / drag-out + live MIDI out
- [ ] Easter egg, Ciclotron treatment, settings menu

### Out of Scope

- Line-by-line port of the prototype's JavaScript — the prototype is a design reference; correct
  plugin practice (threading, sample-accurate timing, automation, state persistence) wins wherever
  the two conflict
- WebView GUI (`juce::WebBrowserComponent`) — evaluated and rejected in favour of native JUCE
- The prototype's React tweaks panel — becomes a native settings/gear menu instead
- Dynamic non-aspect-preserving window resizing — the chassis is a fixed 20:13 design that scales
- Decorative Brazilian iconography, wooden panels, skeuomorphic 808-clone skins, São João palettes
  (explicitly forbidden by the design brief)

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
| Synthesised voices first, samples optional later | No sample library to ship yet; voice specs are fully documented in the handoff | 2026-09-06 | Active |
| Continue in PAUL rather than re-incubating in SEED | `PLANNING.md` already carries the full spec and an implementation order | 2026-09-06 | Active |
| Prototype kept in-repo as design reference, not as a build target | Fastest way to check visual and behavioural intent against the plugin | 2026-09-06 | Active |

## Success Metrics

| Metric | Target | Current | Status |
|--------|--------|---------|--------|
| Grooves judged authentic against the prototype (A/B listening) | All 4 profiles | - | Not started |
| Timing accuracy of triggers | Sample-accurate; step 0 locked to host bar when synced | - | Not started |
| State round-trip (profile, dirty flag, full grid, step count, all params) | Lossless save/reload | - | Not started |
| Time from plugin open to a usable groove | Under 30 s, zero config | - | Not started |
| Audio-thread safety | No allocation or locks in the audio callback | - | Not started |
| DAW validation | Passes VST3 validator; loads in Reaper, Live, Bitwig | - | Not started |
| UI fidelity vs. prototype | Both themes match closely at 1×, 1.5×, 2× | - | Not started |

## Tech Stack / Tools

| Layer | Technology | Notes |
|-------|------------|-------|
| Framework | JUCE 8 | `juce_audio_plugin_client`, VST3 target |
| Plugin format | VST3 | Instrument, MIDI in and out |
| Parameters | `juce::AudioProcessorValueTreeState` | Automatable params; grid/profile as a child node |
| State | APVTS `ValueTree` | `getStateInformation` / `setStateInformation` |
| Timing | `AudioPlayHead` (PPQ) or internal phase accumulator | Host-synced when `SYNC` is on |
| DSP | Custom C++ voices | Per handoff voice specifications; character bus + limiter |
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
*Last updated: 2026-09-06*
