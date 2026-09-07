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
Phases: 1 of 8 complete

## Phases

**Phase Numbering:**
- Integer phases (1, 2, 3): Planned milestone work
- Decimal phases (2.1, 2.2): Urgent insertions (marked with [INSERTED])

Phases execute in numeric order.

| Phase | Name | Plans | Status | Completed |
|-------|------|-------|--------|-----------|
| 1 | Plugin foundation | 3 | ✅ Complete (3/3) | 2026-09-07 |
| 2 | Sequencer clock | 3 | 🚧 Planning (0/3) | - |
| 3 | Voices & mix bus | TBD | Not started | - |
| 4 | UI shell | TBD | Not started | - |
| 5 | Sequencer grid | TBD | Not started | - |
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

### Phase 2: Sequencer clock

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
- [ ] 02-01: Musical content — four profiles verbatim, velocity decoder, tiling, cross-check script
- [ ] 02-02: Clock core — sample-accurate step advance from block position, swing, internal tempo
- [ ] 02-03: Host sync via `AudioPlayHead` + lock-free double-buffer handover

### Phase 3: Voices & mix bus

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

### Phase 4: UI shell

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

### Phase 5: Sequencer grid

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
- GR meter wired to real limiter reduction

---
*Roadmap created: 2026-09-06*
*Last updated: 2026-09-07 — Phase 1 complete*
