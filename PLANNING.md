# Handoff: Forró Box — VST3 / Plugin Implementation

## Overview

**Forró Box** is a rhythm-production instrument plugin for Brazilian forró percussion. It is a
step-sequencer-driven drum machine with five instrument channels (zabumba, triângulo, pandeiro,
ganzá, bateria), four *regional groove profiles* that reload the entire plugin state, a global
humanisation control (`CACHAÇA`), a swing engine, three global timbre characters, and MIDI export
by drag-out.

The design philosophy is **precision instrument, not novelty toy**: the Brazilian identity lives in
the *behaviour* (groove profiles, ghost notes, humanisation, instrument naming) rather than in
decorative illustration. Visual lineage is Teenage Engineering EP-133 / OP-1 and oeksound plugins —
flat, molded, backlit hardware; monospaced readouts; colour used strictly for instrument identity.

**Target for this handoff: a VST3 plugin.**

---

## About the Design Files

The files in this bundle are **design references created in HTML/CSS/JS** — a working prototype that
demonstrates the intended look, layout, and behaviour. **They are not production code to port
line-by-line.**

The task is to **recreate this design as a native VST3 plugin** using the target environment's
established patterns:

- The **HTML/CSS** describes the intended UI: geometry, colour, type, states, and motion. Recreate it
  in the plugin's GUI layer (JUCE `Component`s + custom `LookAndFeel`, or a WebView UI — see below).
- The **`audio.js` Web Audio engine** is a *sound-design sketch*, not a DSP reference implementation.
  It documents the intended character of each voice (envelope shapes, frequency ranges, noise/tone
  balance). Reimplement these as proper C++ synthesis voices, or replace them with sample playback
  if the product ships with a sample library.
- The **`data.js` groove/profile data is directly portable** — it is the musical content and should be
  carried over essentially verbatim (as C++ tables, JSON, or a binary resource).
- The **`app.js` state model and sequencer scheduling** describe the intended behaviour and should be
  re-expressed as a plugin processor + parameter tree, driven by the host's timeline rather than by
  `setInterval`.

If any part of the prototype conflicts with correct plugin practice (threading, parameter automation,
sample-accurate timing, state persistence), **correct plugin practice wins** — the prototype is
constrained by the browser.

---

## Fidelity

**High-fidelity (hifi).** All colours, typography, spacing, sizing, states, and motion in this
prototype are final design intent, not placeholders. Recreate the UI to match closely.

Two deliberate exceptions:

1. **The chassis is a fixed 1200×780 px design** that letterbox-scales to fit its container. A VST3
   window should keep this aspect ratio and support host resizing by scaling the whole UI.
2. **The `LOAD` buttons, `LOAD IR…`, `SYNC`, and the preset name cycler are non-functional stubs** in
   the prototype. They are designed but not implemented; their real behaviour is specified in
   Unimplemented / Stubbed Controls.

---

## Recommended VST3 Architecture

### Stack

| Concern | Recommendation |
|---|---|
| Framework | **JUCE 8** (`AudioProcessor` + `AudioProcessorEditor`), VST3 target via `juce_audio_plugin_client` |
| Plugin type | **Instrument** (`isMidiEffect=false`, `producesMidi=true`, `wantsMidiInput=true`), synth flag on |
| Parameters | `juce::AudioProcessorValueTreeState` (APVTS) |
| State | APVTS `ValueTree` → `getStateInformation` / `setStateInformation` |
| Timing | Host `AudioPlayHead` (`getPosition()` → PPQ) when `SYNC` is on; internal phase accumulator otherwise |
| Voices | Custom synth voices (see Voice Specifications), or sample playback |
| GUI | Custom `LookAndFeel_V4` subclass + hand-drawn `Component`s (recommended), **or** `juce::WebBrowserComponent` reusing this HTML |

### GUI approach — two viable paths

**Path A — native JUCE UI (recommended for a shipping product).** Rebuild the layout with JUCE
`Component`s and a custom `LookAndFeel`. Gives correct DPI handling, low overhead, no web runtime,
and proper parameter attachment (`SliderAttachment`, `ButtonAttachment`). All geometry and colour in
this document is specified in px so it maps directly to `Rectangle<int>` layout via
`FlexBox`/`Grid`.

**Path B — WebView UI.** JUCE 8's `WebBrowserComponent` with native integration
(`WebSliderRelay`, `WebToggleButtonRelay`, `WebSliderParameterAttachment`) can host this HTML almost
as-is, which preserves the design exactly and speeds up iteration. Costs: larger binary, web runtime
dependency, and some hosts/DAWs are fussy about embedded webviews. If choosing this, the prototype's
`forrobox.css` and DOM can be reused, but replace `app.js`'s audio and clock with message-passing to
the C++ processor.

**This document is written so either path is possible.** Prefer Path A unless the team explicitly
wants to keep the HTML.

### Threading contract

- **Audio thread**: sequencer advance, voice triggering, DSP, limiter. No allocation, no locks.
- **Message thread**: all GUI. Reads atomics/FIFO for meters, playhead position, and trigger flashes.
- **Trigger→UI communication**: lock-free FIFO or an atomic "last triggered step + velocity" per
  channel. The UI's hit visualiser and playhead read this at frame rate.
- **Profile loading** touches many parameters at once — do it on the message thread via
  `APVTS`/`beginChangeGesture`, and have the audio thread pick up new pattern tables through a
  double-buffer or `AbstractFifo` swap, never by mutating shared tables in place.

---

## Design Tokens

All tokens are defined as CSS custom properties in `forrobox.css` (`:root` for dark,
`[data-theme="light"]` for the light theme). Both themes must be supported.

### Colours — Dark theme (default)

| Token | Hex | Role |
|---|---|---|
| `--bg` | `#141414` | Window background / chassis |
| `--panel` | `#1E1E1E` | Channel strips, buttons, inset panels |
| `--raised` | `#242424` | Header, side panel, footer (raised surfaces) |
| `--sunken` | `#0F0F0F` | Sequencer well, recessed groups, meters |
| `--fg` | `#E8E8E8` | Primary text, knob indicator lines |
| `--fg-dim` | `rgba(232,232,232,0.5)` | Secondary text, inactive button labels |
| `--fg-faint` | `rgba(232,232,232,0.3)` | Micro-labels, section labels |
| `--line` | `rgba(255,255,255,0.09)` | Hairline dividers |
| `--line-strong` | `rgba(255,255,255,0.20)` | Button borders, knob tracks |
| `--active` | `rgba(255,255,255,0.7)` | Active/selected fill |
| `--hub` | `#181818` | Knob hub fill |
| `--danger` | `#FF4136` | Mute, limiter GR, Ciclotron accent |
| `--screen` | `#0A0A0A` | Readout/LCD background |
| `--screen-fg` | `#D8D8D8` | Readout text |

### Colours — Light theme (OP-1 cream)

| Token | Hex |
|---|---|
| `--bg` | `#E6E1D6` |
| `--panel` | `#F1EDE4` |
| `--raised` | `#FAF7F0` |
| `--sunken` | `#D6D0C3` |
| `--fg` | `#191919` |
| `--fg-dim` | `rgba(25,25,25,0.55)` |
| `--fg-faint` | `rgba(25,25,25,0.34)` |
| `--line` | `rgba(0,0,0,0.12)` |
| `--line-strong` | `rgba(0,0,0,0.28)` |
| `--active` | `rgba(0,0,0,0.78)` |
| `--hub` | `#EFEADF` |
| `--screen` | `#CFC8B8` |
| `--screen-fg` | `#2A2620` |

`--danger` is unchanged across themes.

### Instrument accent colours (identical in both themes)

| Instrument | Token | Hex |
|---|---|---|
| Zabumba | `--c-zabumba` | `#E8650A` |
| Triângulo | `--c-triangulo` | `#00C2C7` |
| Pandeiro | `--c-pandeiro` | `#F2C200` |
| Ganzá | `--c-ganza` | `#7ABF6E` |
| Bateria | `--c-bateria` | `#E84646` |

Bateria sub-channel colours (used for the four dots on the Bateria strip):
`BB #E84646`, `CX #F2887A`, `HH #F4B6AE`, `TOM #9B2F2F`.

**Colour discipline (important):** accent colours are used *only* for instrument identity — the
accent bar, the sequencer row chip, active step pads, the trigger LED, the hit visualiser, and that
channel's knob value arcs. Never for general UI chrome. `--c-zabumba` doubles as the single global
brand accent (wordmark dot, CACHAÇA knob, DRAG MIDI CTA) because zabumba is the anchor instrument.

### Typography

Two families, loaded from Google Fonts in the prototype. For a plugin, **embed the fonts as binary
resources** (both are open-licensed: Space Grotesk and IBM Plex Mono are OFL).

| Token | Stack | Use |
|---|---|---|
| `--sans` | **Space Grotesk** (400/500/600/700) | All labels, names, buttons |
| `--mono` | **IBM Plex Mono** (400/500/600) | All numeric readouts, LCD-style screens, tabular values |

Type scale actually used (px, all with the letter-spacing shown — spacing is a big part of the look):

| Element | Size | Weight | Letter-spacing | Transform |
|---|---|---|---|---|
| Wordmark `FORRÓ·BOX` | 17 | 700 | 0.13em | — |
| BPM readout | 22 | 500 | 0.02em | mono |
| BPM `BPM` suffix | 9 | 400 | 0.12em | mono, 55% opacity |
| Global knob readout (`SWING`/`CACHAÇA`) | 16 | 500 | 0.01em | mono |
| Global knob name | 9.5 | 600 | 0.18em | uppercase |
| Strip instrument name | 12.5 | 600 | 0.10em | — |
| Strip index (`01`…`05`) | 9 | 400 | — | mono |
| Sample name | 12.5 | 500 | 0.01em | — |
| Knob micro-label (`VOL`, `PITCH`…) | 9 | 400 | 0.07em | uppercase |
| Section label (`SEQUENCER`, `REGIONAL PROFILES`) | 9 | 500 | 0.20em | uppercase |
| Sequencer row label | 10.5 | 600 | 0.06em | — |
| Profile name | 11 | 600 | 0.08em | — |
| Profile description | 9.5 | 400 | — | line-height 1.4 |
| Timbre name | 11 | 600 | 0.06em | — |
| Timbre sub-label | 8 | 400 | 0.10em | uppercase |
| Quick-switch code (`CAM`…) | 10 | 500 | 0.06em | mono |
| `STYLE` label | 8.5 | 400 | 0.20em | uppercase |
| Button label (generic) | 10 | 500 | 0.08em | uppercase |
| `DRAG MIDI` label | 11 | 700 | 0.16em | uppercase |
| Footer label (`MASTER`, `OUTPUT`) | 9 | 400 | 0.14em | uppercase |
| Tooltip | 11 | 400 | — | mono |

### Spacing, radius, shadow

- **Radius**: `--r`, default **2px**, user-tweakable to **0px** ("hard"). Composite radii are
  `calc(var(--r) + 1px)` … `calc(var(--r) + 3px)` for nested groups. Keep this as a global UI
  setting.
- **Spacing**: no formal scale; the layout uses 1px hairlines, and gaps of 4/5/7/9/11/12/14/16/18px.
- **Recessed (screen/inset) shadow**, dark theme:
  `inset 0 1px 3px rgba(0,0,0,0.45), inset 0 0 0 1px rgba(0,0,0,0.20)`
  Light theme: `inset 0 1px 2px rgba(0,0,0,0.14)`.
- **Raised panel highlight**: `inset 0 1px 0 rgba(255,255,255,0.04)` dark;
  `inset 0 1px 0 rgba(255,255,255,0.5)` light.
- **Sequencer well**: `inset 0 2px 6px rgba(0,0,0,0.40)` dark; `inset 0 2px 5px rgba(0,0,0,0.10)` light.
- **Window drop shadow** (prototype only; irrelevant in a plugin host):
  `0 40px 120px rgba(0,0,0,0.6)`.
- **Screen text glow**: `text-shadow: 0 0 8px <screen-fg at 30%>` on all mono readouts.

---

## Window & Scaling

- **Design size: 1200 × 780 px.** Fixed aspect ratio 20:13.
- The prototype applies a uniform `scale()` transform so the chassis always fits its container, and
  recomputes on resize via a `ResizeObserver`.
- **Plugin equivalent**: set `setResizable(true, true)`,
  `getConstrainer()->setFixedAspectRatio(1200.0 / 780.0)`, and implement `resized()` by applying a
  global transform: `setTransform(AffineTransform::scale(getWidth() / 1200.0f))` on a fixed-size
  1200×780 child component. This keeps all layout maths in design px.
- Suggested default window: **1200×780** on desktop; minimum ~**840×546** (0.7×) remains legible.
- Row structure is a 4-row CSS grid: **header 72px / matrix `1fr` / sequencer 196px / footer 56px**.

---

## Layout — Geometry

Whole window: 4 rows as above. Row 2 (`.main`) is a 2-column grid: **channel matrix `1fr` + side
panel 280px**.

### Header (72px)

`display:flex; align-items:center; gap:12px; padding:0 16px`. Background is a subtle vertical
gradient: `linear-gradient(180deg, <raised + 3% white>, <raised>)`, bottom border `1px --line`,
plus `inset 0 1px 0 rgba(255,255,255,0.05)` top highlight.

Contents left→right:

1. **Logo lockup** — 36×27px inline SVG mark + wordmark, `gap:10px`.
   The mark is three overlapping monoline silhouettes in a 46×34 viewBox:
   - *Sanfona* (accordion bellows), left: quadrilateral `M3 10 L13 7.5 L13 26.5 L3 29 Z` plus three
     vertical pleat lines, stroke `--fg-dim`, width 1.5.
   - *Zabumba* (drumhead), centre: circle `cx=24.5 cy=18.5 r=8.8`, stroke `--fg`, width 1.7, plus
     crossed tension rods in `--fg-faint`, width 1.
   - *Triângulo*, right: open triangle `M31 6 L41.5 24 L23.5 24` + beater line
     `35.5,13.5 → 41,10.5`, stroke `--c-zabumba`, width 1.9, round caps/joins.
   Wordmark: `FORRÓ` + `·` in `--c-zabumba` + `BOX`.
2. **BPM cluster** (`gap:8px`): BPM readout, `SYNC` toggle button, then `÷2` and
   `×2` mini-buttons (`gap:4px`).
3. **Transport** (`gap:6px`): play and stop, each 34×34px.
4. Flexible spacer.
5. **Global knob group** — the visual centre of the header, `margin-left:auto`.
6. Flexible spacer.
7. **Right cluster** (`gap:10px`): preset cycler, then the `STYLE` segmented control.

### Channel matrix

`display:grid; grid-template-columns:repeat(5, 1fr); gap:1px; background:--line` — the 1px gap
*is* the divider. Each strip: `background:--panel; padding:12px 11px 10px; display:flex;
flex-direction:column`.

The **zabumba strip carries `.anchor`**: its background is
`color-mix(in srgb, var(--panel) 88%, var(--c-zabumba))` — a barely-perceptible warm tint marking it
as the anchor instrument. Keep this subtle.

Strip contents top→bottom:

1. **Head row**: instrument name (left) · trigger LED + index `01`–`05` (right, `gap:7px`).
2. **Accent bar**: 4px tall, `margin:8px 0 9px`, filled with the instrument colour, radius 1px, and
   a soft glow `0 0 10px <colour at accent-intensity × 35%>`.
3. **Sample slot**: `display:flex; gap:7px` — sample name (flex:1, ellipsised) + `LOAD` button
   (9px label, `padding:4px 7px`).
   Current names: `Couro Aberto`, `Aço Aberto`, `Pandeiro Médio`, `Ganzá Seco`, `Kit Minimal`.
4. **Hit visualiser**: 14px tall, `margin-top:9px`.
5. 1px divider (`margin:11px 0`).
6. **Knob grid**: `grid-template-columns:1fr 1fr; gap:9px 6px`, knobs centred — `VOL`, `PITCH`,
   `DECAY`, `PAN`, each 32px.
7. 1px divider.
8. **Pattern cycler**: `‹` + `PAT 01` screen + `›`, `gap:5px`.
9. **Mute/Solo row**: two equal-flex buttons `M` and `S`, `gap:5px`, `margin-top:8px`.
10. **Ghost Prob**: label row (`Ghost Prob` left, `NN%` right, mono) + horizontal fader.
11. **Bateria only**: sub-channel dots — four 8px circles in the sub colours + label
    `BB · CX · HH · TOM ↗`, `gap:5px`, `margin-top:9px`, whole row clickable.

### Sequencer (196px)

`background:--sunken; padding:12px 16px 14px`, top border 1px, well shadow as specified.

- **Head row** (`margin-bottom:11px`): `SEQUENCER` section label + hint text
  `CLIQUE O NOME P/ ISOLAR` (`gap:12px`) on the left; on the right `STEPS` + `16`/`32` buttons.
- **Grid**: 5 rows, `gap:7px`. Each row is
  `grid-template-columns:92px 1fr; gap:10px; align-items:center`.
  - **Row label**: 4×18px colour chip (radius 1px) + instrument name, `gap:7px`, clickable
    (isolate). Default colour `--fg-dim`, hover/isolated `--fg`.
  - **Pads**: `display:grid; grid-template-columns:repeat(N, 1fr); gap:5px`, each pad **26px tall**,
    radius `--r`.
- **Playhead**: absolutely positioned overlay.

### Side panel (280px)

`background:--raised`, left border 1px, `padding:13px 14px; gap:11px`, `overflow:hidden`.
Sections are `flex-shrink:0` so they never compress.

1. **`REGIONAL PROFILES`** section label, with a right-aligned `CUSTOM` tag (mono, 9px,
   `--c-pandeiro`, `letter-spacing:0.12em`) that fades in when the state is dirty.
2. **Profile list**: 4 buttons, `gap:5px`, each `padding:7px 10px`, `background:--panel`,
   `border:1px --line`, radius `--r`.
   - Inactive: name only.
   - **Active**: `background:--active`, name in `--bg`, and the 3-line description becomes visible
     (`display:block`) in `rgba(0,0,0,0.6)`. A small `●` in `--c-zabumba` floats right.
   - Only one is active; selecting one is a full state reload.
3. **`TIMBRE / CONVOLUTION`** section label.
4. **Timbre list**: 3 rows, `gap:4px`, each `padding:6px 10px` — name + sub-label on the left, a 7px
   LED on the right. Active row: border `--active`, background
   `color-mix(in srgb, var(--panel) 70%, var(--active))`, LED `--c-ganza` with
   `box-shadow: 0 0 6px --c-ganza`.
5. **Mix row**: 28px `MIX` knob (`--c-triangulo`) + `LOAD IR…` button (flex:1), `gap:10px`.
6. **Bundle footer**: pushed to the bottom (`margin-top:auto`, top border 1px, `padding-top:12px`) —
   a 7px green LED + `BUNDLE: MINIMAL` in mono.

### Footer (56px)

`display:flex; align-items:center; gap:20px; padding:0 18px; background:--raised`, top border 1px.

1. `MASTER` label + 120px horizontal fader.
2. `LIMITER` toggle (on by default) + 56×6px gain-reduction meter (radius 999px, fill `--danger`
   growing right→left, `transition: width 60ms linear`).
3. **`DRAG MIDI`** CTA, horizontally centred via `margin:0 auto`.
4. `OUTPUT` label + segmented `STEREO` / `MULTI-OUT` toggle, pushed right with `margin-left:auto`.

---

## Components

### Knob

The single most-used control. Flat, minimal, drawn as SVG in a 100×100 viewBox, rendered at 28/32/54px.

**Geometry**
- Sweep: **−135° to +135°** (270° total), 0° = pointing up.
- **Track arc**: radius 38, `stroke --line-strong`, width 5, round caps — full sweep, always visible.
- **Value arc**: radius 38, `stroke <instrument colour>`, width 5, round caps.
  - *Unipolar*: from −135° to the current angle.
  - *Bipolar* (`PITCH`, `PAN`): from 0° (centre) to the current angle, in either direction.
- **Hub**: circle radius 30, `fill --hub`, `stroke --line`, width 1.5.
- **Indicator line**: from centre (50,50) to (50,16), `stroke --fg`, width 5, round cap, rotated to
  the value angle. This flat line *is* the knob's identity — no cap, no bevel, no notch.
- Whole SVG gets `drop-shadow(0 1px 1px rgba(0,0,0,0.35))`; the value arc gets a subtle
  `drop-shadow(0 0 2px <colour at 40%>)`.
- Micro-label sits below, 9px uppercase `--fg-faint`, `gap:3px`.

**Interaction** (all of this should be preserved — it is what makes the plugin feel like a tool)

| Gesture | Behaviour |
|---|---|
| Vertical drag | Change value. Full range ≈ 160px of travel. Cursor becomes `ns-resize`. |
| `Shift` + drag | Fine mode — 0.18× sensitivity. |
| Scroll wheel | Increment; `Shift` for fine steps. |
| Double-click | Type an exact value (prototype uses `prompt()`; a plugin should show an inline text editor). |
| Right-click | Reset to default. Shows a `reset` tooltip. |
| Hover | Show value tooltip above the knob. |
| Arrow keys | ±1 step when focused. Focus ring = hub stroke turns `--active`. |

**Tooltip**: mono 11px, `background --screen`, `color --screen-fg`, `border 1px --line-strong`,
radius `--r`, `padding:2px 7px`, positioned above-centre, fades in 120ms.

### Global knobs (SWING & CACHAÇA)

These are the plugin's two signature controls and are deliberately **the visual centre of the
header**.

- Knob diameter **54px** (vs 32px in strips, 28px in the side panel).
- Wrapped in a shared recessed group: `padding:6px 18px 5px`, radius `calc(--r + 3px)`,
  background `radial-gradient(120% 160% at 50% -30%, <zabumba at 12%> , --sunken)`,
  border `1px color-mix(in srgb, var(--c-zabumba) 25%, var(--line-strong))`,
  shadow `inset 0 1px 3px rgba(0,0,0,0.4), 0 0 18px <zabumba at 12%>`.
- Each knob has its label + readout stacked to its right (`gap:11px` knob→meta, `gap:4px` within).
  Readout: mono **16px**, min-width 46px, on a `--screen` background.
- Separated by a 1px × 42px divider in `--line-strong`.
- `SWING` value arc is `--fg` (neutral); `CACHAÇA` is `--c-zabumba`.

### BPM

- Mono **22px**, `background --screen`, `color --screen-fg`, `padding:3px 10px`, min-width 78px,
  centred, radius `--r`, recessed shadow + text glow. Cursor `ns-resize`.
- Suffix ` BPM` at 9px, 55% opacity, `letter-spacing:0.12em`.
- Drag vertically (0.5 BPM per px), scroll to nudge ±1, double-click to type. Range **40–300**.
- `÷2` / `×2` mini-buttons halve/double, clamped to range.
- `SYNC` toggle: when on, tempo follows the host and the BPM field should become read-only
  (display host tempo).

### Buttons

Base (`.btn`): 10px uppercase 500, `letter-spacing:0.08em`, `color --fg-dim`, transparent
background, `border 1px --line-strong`, radius `--r`, `padding:5px 9px`.
- Hover: `color --fg`, `border-color --fg-dim`.
- Active press: `transform: scale(0.96)`.
- **On state**: `background --active`, `color --bg`, `border-color --active`.

**Transport buttons**: 34×34px, `background --panel`, `border 1px --line-strong`, centred 14px
icon. Hover → `background --sunken`. Press → `scale(0.94)`.
**Play, when playing**: `background --c-ganza`, `border-color --c-ganza`, icon `#0D1A0B`, plus
`box-shadow: 0 0 12px <ganza at 55%>`.

**Mute/Solo**: mono 11px, flex:1.
- `M` on → `background --danger`, white text.
- `S` on → `background --c-pandeiro`, text `#1A1500`.

**Arrow buttons** (`‹` `›`): 22×26px, `background --panel`, `border 1px --line-strong`,
`color --fg-dim` → `--fg` on hover, press `scale(0.92)`.

### STYLE segmented control

Replaces what used to be four loose buttons (which overflowed the header).

- `STYLE` micro-label (8.5px, `letter-spacing:0.2em`, `--fg-faint`) + the segment group, `gap:7px`.
- Group: `background --sunken`, `border 1px --line-strong`, radius `calc(--r + 1px)`,
  `overflow:hidden`, `inset 0 1px 2px rgba(0,0,0,0.3)`.
- Segments show **3-letter codes**: `CAM`, `CAR`, `PET`, `UNI` — mono 10px, `padding:7px 9px`,
  divided by `border-right: 1px --line` (last has none). Full profile name in the tooltip.
- Hover: `color --fg` + `background <fg at 8%>`. Selected: `background --active`, `color --bg`.
- Should behave as a radio group / `role="tablist"`.

### Fader

- Track: 4px tall, `background --line-strong`, radius 999px.
- Fill: instrument or `--fg` colour, radius 999px, saturation scaled by accent intensity.
- Thumb: 12px circle, `background --fg`, `box-shadow: 0 1px 3px rgba(0,0,0,0.4)`.
- Click-to-jump and drag. A vertical variant exists (16px wide) but is unused in the current layout.

### Step pad

- **26px tall**, radius `--r`, `gap:5px` between pads.
- **Off**: `linear-gradient(180deg, rgba(255,255,255,0.03), rgba(0,0,0,0.18))` +
  `inset 0 1px 1px rgba(0,0,0,0.4), inset 0 0 0 1px rgba(0,0,0,0.25)` — a recessed, unlit pad.
  Light theme: `linear-gradient(180deg, rgba(0,0,0,0.10), rgba(0,0,0,0.05))`.
- **On (backlit)**:
  `radial-gradient(120% 100% at 50% 22%, <colour + 22% white>, <colour> 70%)` +
  `inset 0 1px 0 <colour + 35% white>, 0 0 9px <colour at accent×45%>`.
  Opacity encodes velocity: `0.32 + (vel/127) × 0.68`.
- **Ghost note** (velocity ≤ 42): pad renders as off but with a 3px centred dot in the instrument
  colour at 90% opacity.
- **Beat marker** (every 4th step): adds `inset 0 0 0 1px --line`.
- **Currently playing**: `outline: 1px solid --active; outline-offset: 1px; z-index:2`.
- **Hover**: `border-color --line-strong`. **Press**: `scale(0.9)` plus a
  `padpress` animation — 190ms `cubic-bezier(.3,.7,.3,1.4)`, scaling 1 → 0.86 → 1 (slight overshoot).
- **Trigger flash**: brightness pulse on the beat, `1 + strength` decaying to 1 over ~260ms, where
  `strength = 0.9 + (vel/127) × 0.9`.
  In the prototype this is driven from JS on a rAF loop rather than a CSS animation, because
  CSS animations can be throttled in background contexts. In a plugin, drive it from the UI timer
  reading the trigger FIFO.

### Playhead

- Absolutely positioned 3px-wide vertical bar spanning all five rows (`top:-3px; bottom:-3px`),
  `z-index:5`.
- Fill: `linear-gradient(180deg, <pandeiro + white>, --c-pandeiro)`,
  `box-shadow: 0 0 10px --c-pandeiro, 0 0 3px white`, radius 3px.
- **Trailing glow**: a 26px-wide gradient behind it
  (`linear-gradient(90deg, transparent, <pandeiro at 16%>)`) trailing to the left of the bar.
- Motion: it *interpolates* between steps — the prototype sets
  `transition: left <stepDuration>ms linear` and moves it to the centre of each step as that step
  fires, producing continuous sweep rather than jumping. **Reproduce the continuous sweep**: in a
  plugin, drive the x-position from the current PPQ/step phase on a 60fps UI timer.
- Hidden (`opacity:0`) when stopped.

### Hit visualiser (per channel)

Replaced the original per-channel waveform thumbnails, which were too small to be useful.

- **Trigger LED** in the strip head: 8px circle in the instrument colour, resting opacity 0.22.
  On trigger: opacity → `0.25 + vel×0.75`, `box-shadow: 0 0 <2 + vel×7>px <colour>`, then decays.
- **Activity meter** below the sample name: 14px tall, `background --sunken`, radius `--r`,
  recessed shadow, `overflow:hidden`. Inside:
  - **Fill**: `linear-gradient(90deg, <colour>, <colour at 30%>)`, `transform-origin: left center`,
    scaled horizontally by the current level and faded by `0.35 + level×0.65`.
  - **Ticks overlay**: 16 evenly spaced 1px divisions in the background colour at 55%, 50% opacity —
    reads as a step ruler.
- **Decay**: level is set to `max(level, velocity)` on trigger and multiplied by **0.82 per frame**
  (~60fps), giving a snappy exponential falloff. Muted/soloed-out channels do **not** light up.

### Drag MIDI

Designed as the footer's primary call to action.

- `padding:9px 30px`, radius `calc(--r + 2px)`, `gap:11px`, cursor `grab`.
- Background `linear-gradient(180deg, <zabumba at 14%> + panel, --panel)`,
  border `1.5px color-mix(in srgb, var(--c-zabumba) 45%, var(--line-strong))`.
- **Idle pulse**: 2.6s ease-in-out loop breathing the glow between
  `0 0 0 1px <zabumba 18%>` and `+ 0 0 20px <zabumba 28%>`.
- **`↓` arrow**: 16px, `--c-zabumba`, bobbing 2px on the same 2.6s cycle.
- **Hover / drag-active**: animation stops, border → solid `--c-zabumba`, background tint → 26%,
  `box-shadow: 0 0 0 1.5px --c-zabumba, 0 0 30px <zabumba at 55%>`.
- Press: `scale(0.98)`, cursor `grabbing`.
- Labels: `DRAG MIDI` (11px/700/0.16em) + `.mid` (mono 9px, `--c-zabumba` at 70%).

**Behaviour**: drag exports the current groove as a Standard MIDI File; click downloads the same
file. In a VST3 the drag should use the host's file-drag mechanism
(`juce::DragAndDropContainer::performExternalDragDropOfFiles` with a temp `.mid`), which is how
users get the groove into a DAW track. Filename pattern:
`forrobox_<profile>_<bpm>bpm.mid`.

### Bateria kit overlay

Because the drum kit has four sub-channels that cannot fit in a 1/5-width strip, they live in an
overlay.

- Trigger: clicking the four sub-dots row on the Bateria strip.
- Backdrop covers the matrix + side panel area, `color-mix(in srgb, var(--bg) 78%, transparent)`
  with `backdrop-filter: blur(3px)`, `z-index:40`. Clicking the backdrop closes it.
- Panel: **620px wide**, right-aligned, `background --panel`, left border `1px --line-strong`,
  `box-shadow: -20px 0 60px rgba(0,0,0,0.4)`, `padding:18px 20px`.
- **Entrance**: slides in from `translateX(24px)` + `opacity:0` over 200ms
  `cubic-bezier(.2,.7,.3,1)`.
- Header: `BATERIA · KIT` (14px/600, `BATERIA` in `--c-bateria`), `white-space:nowrap`, with a
  26×26px `✕` close button.
- Sub-line: *"Sequencie cada peça do kit. As batidas aparecem somadas na linha BATERIA do
  sequenciador principal."*
- Four rows, `gap:10px`, each `grid-template-columns:120px 1fr; gap:12px`:
  label block (name 12px/600 + full name 9px `--fg-faint`) then a pad grid with the same step count.
  Pads here are taller: **26px**.
  - `BB` / Bumbo, `CX` / Caixa, `HH` / Chimbal, `TOM` / Surdo.

---

## Interactions & Behaviour

### Transport & clock

- **Play/Stop** in the header; **Space** toggles play when the body has focus.
- The prototype uses a Web Audio lookahead scheduler: a 25ms `setInterval` scheduling any step whose
  time falls within a 100ms lookahead window, with `nextNoteTime` advanced by
  `stepDuration = 60 / bpm / 4` (sixteenth notes).
- **In the plugin, replace this entirely**: advance a step counter from the audio block's sample
  position so triggers are sample-accurate, and follow `AudioPlayHead` when `SYNC` is enabled
  (align step 0 to the host's bar/PPQ so the groove locks to the project).
- Stopping clears the playhead and all "playing" pad outlines, and resets the step counter to 0.

### Swing

Applied to **odd-numbered sixteenths only**:
`delay = (swing / 100) × 0.6 × stepDuration`.
At `swing = 100` an odd step is pushed 60% of a step later — a strong shuffle. `swing = 0` is
straight. Default per profile (16–54).

### CACHAÇA — humanisation

The signature control. It is *not* an effect; it is a humanisation amount applied at three points:

1. **Timing jitter**: every step is offset by
   `±(cachaça / 100) × 22ms × random()` (uniform, bipolar).
2. **Velocity variation**: each hit's velocity is multiplied by
   `1 − (cachaça/100) × 0.25 × random()` — i.e. up to 25% softer at maximum, never louder.
3. **Ghost-note probability multiplier**: see below.

**Easter egg — `CACHAÇA` ≥ 88%:** the plugin visibly "feels it".
- A warm wash fades in over the whole chassis, driven by `--drunk = clamp((cachaça − 65) / 35, 0, 1)`:
  `radial-gradient(120% 80% at 50% 118%, rgba(232,101,10,0.42), transparent 58%)` +
  `radial-gradient(140% 120% at 50% -20%, rgba(242,194,0,0.16), transparent 52%)` +
  `linear-gradient(180deg, rgba(232,101,10,0.06), rgba(232,101,10,0.13))`, `mix-blend-mode:screen`.
- At ≥88% the chassis gains a **6s ease-in-out sway** of ±0.18°, and the `CACHAÇA` label changes to
  **`♪ NO PONTO`** in `--c-zabumba` with a 1.6s opacity pulse; the readout also turns orange.
- Keep it tasteful — the groove is still usable at 100%; this is a reward, not a gimmick mode.

### Ghost notes

Per-channel `Ghost Prob` (0–100%) adds unwritten in-between hits, which is much of what makes the
groove feel human.

- On any step where the channel has **no** programmed hit:
  `chance = (ghostProb / 100) × (0.22 + (cachaça/100) × 0.6)`.
- If it fires: velocity `0.20–0.32` (normalised), timing offset `±10ms`.
- For bateria, ghosts are generated on the **hi-hat (`HH`) only**.
- Ghost notes are *not* written into the pattern and are *not* exported to MIDI — they are
  performance, not data.

### Mute / Solo / Isolate

- **Mute** (`M`) silences a channel; its sequencer row dims to 32% opacity.
- **Solo** (`S`) — if any channel is soloed, non-soloed channels are silent.
- **Isolate** — clicking a sequencer *row label* visually isolates that row (others dim to 32%)
  **without affecting audio**. It's a focus aid for editing. Clicking again clears it. Only one row
  can be isolated.

### Step editing

- Clicking a pad toggles between velocity **0** and **100**.
- On the collapsed `BATERIA` row, clicking edits the **caixa (`CX`)** — the backbeat. Per-piece
  editing is in the kit overlay. The collapsed row *displays* the max velocity across all four
  sub-channels.
- Editing anything marks the state **dirty** → the `CUSTOM` tag appears and the active profile
  highlight clears (the state is no longer that profile).

### Step count

- `16` / `32` toggle. Switching **tiles** the existing pattern rather than clearing it:
  `newArray[i] = oldArray[i % oldLength]`, so 16→32 duplicates the bar and 32→16 truncates.
- Applies to all channels and all bateria sub-channels.

### Profile loading

Selecting a regional profile (from the side list or the `STYLE` control) is a **full state reload**:
BPM, swing, cachaça, all five patterns, all bateria sub-patterns, mute states, and the timbre
character. It also clears the dirty flag and re-selects the profile, and flashes all active pads
(brightness 1.6 → 1 over 340ms) as confirmation.

### Timbre character

Three global characters, applied as a parallel dry/wet character bus with a `MIX` knob (0–100%,
default 40%):

| Timbre | Sub-label | Lowpass | Drive (tanh) | Wet |
|---|---|---|---|---|
| `HI-FI` | Limpo, encorpado | 16 kHz | 1.2 | `mix × 0.5` |
| `LO-FI` | Fita, 12-bit | 5.2 kHz | 2.4 | `mix` |
| `CICLOTRON™` | **TOTAL DISTORTION™** | 9 kHz | 9.0 | `mix` |

Dry gain is `1 − wet × 0.5`. Filter changes are smoothed (`setTargetAtTime`, 20ms).

**Ciclotron™ is an in-joke and is treated as one visually.** When active:
- The whole chassis gets `filter: saturate(0.9) contrast(1.06)` and a scanline overlay
  (`repeating-linear-gradient(0deg, rgba(0,0,0,0.14) 0 1px, transparent 1px 3px)` at 50% opacity)
  that **flickers** on a 4s `steps(1)` loop (brief dips to 0.16/0.28 opacity).
- The `CICLOTRON™` name gets slight chromatic aberration:
  `text-shadow: 1.2px 0 <danger at 70%>, -1.2px 0 <triangulo at 70%>`.
- Its sub-label `TOTAL DISTORTION™` turns `--danger` and blinks on a 1.4s `steps(1)` loop.

### Limiter & master

- `LIMITER` on by default: threshold −6dB, knee 0, ratio 20:1, attack 2ms, release 120ms.
  Off = threshold 0dB, ratio 1:1 (bypassed rather than removed, to avoid a click).
- The **GR meter** shows gain reduction in dB, polled per frame, growing right→left.
- **Master** fader applies `gain = (value/100)²` (perceptual taper).

### Tooltips & hints

- Knob/BPM tooltips as described above.
- The sequencer shows a persistent hint: `CLIQUE O NOME P/ ISOLAR`.
- `STYLE` segments carry the full profile name as their tooltip.
- All UI copy is **Brazilian Portuguese** where it is instructional, and untranslated where it is a
  technical/musical term (`SWING`, `STEPS`, `LIMITER`, `MASTER`, `OUTPUT`, `PAT`, `MIX`).

---

## State Management

### Global state

| Field | Type | Default | Range / values | Notes |
|---|---|---|---|---|
| `bpm` | int | 132 | 40–300 | Overridden by host when `sync` |
| `playing` | bool | false | | |
| `sync` | bool | false | | Follow host tempo |
| `steps` | int | 16 | 16 \| 32 | |
| `currentStep` | int | −1 | | Transient |
| `swing` | float | 38 | 0–100 | |
| `cachaca` | float | 22 | 0–100 | |
| `presetIdx` | int | 0 | 0–7 | Label-only stub |
| `activeProfile` | enum | `campina` | campina \| caruaru \| petrolina \| sp | |
| `dirty` | bool | false | | Shows `CUSTOM` tag |
| `timbre` | enum | `hifi` | hifi \| lofi \| ciclo | |
| `charMix` | float | 40 | 0–100 | |
| `limiterOn` | bool | true | | |
| `outputMode` | enum | `stereo` | stereo \| multi | Multi-out unimplemented |
| `master` | float | 82 | 0–100 | |
| `isolated` | id? | null | | Visual only |
| `bateriaOpen` | bool | false | | UI only |

### Per-channel state (×5)

| Field | Default (zab / tri / pan / gan / bat) | Range |
|---|---|---|
| `vol` | 82 / 68 / 72 / 64 / 74 | 0–100 |
| `pitch` | 0 | −12…+12 semitones (bipolar) |
| `decay` | 58 / 40 / 46 / 30 / 50 | 0–100 |
| `pan` | 0 / +22 / −18 / +12 / 0 | −50…+50 (bipolar; displayed `L##`/`C`/`R##`) |
| `ghost` | 12 / 8 / 14 / 6 / 10 | 0–100 % |
| `mute` | false | |
| `solo` | false | |
| `pattern` | 1 | 1–8 (stub cycler) |

### Pattern grid

`grid.<channel>` is an array of `steps` velocities (0–127); `grid.bateria` is an object of four such
arrays keyed `bb`/`cx`/`hh`/`tom`.

### VST3 parameter mapping

Expose as automatable APVTS parameters: `bpm` (when not synced), `swing`, `cachaca`, `charMix`,
`master`, `limiterOn`, `timbre` (choice), `steps` (choice), `outputMode` (choice), and per channel
`vol`, `pitch`, `decay`, `pan`, `ghost`, `mute`, `solo` (35 channel params).
The **pattern grid and active profile are state, not parameters** — persist them in the APVTS
`ValueTree` as a child node (or as a base64 blob) so they save with the project and with presets,
but don't expose 160+ step values as automation lanes.

**Preset/state persistence must round-trip**: profile id, dirty flag, full grid, step count, and all
of the above.

---

## Voice Specifications

These describe the **intended character** of each voice, as prototyped in Web Audio. `pitchFactor =
2^(pitch/12)`; `decayScale = 0.4 + (decay/100) × 1.4`; `v` = normalised velocity.

| Voice | Synthesis |
|---|---|
| **Zabumba** | Sine, pitch swept `96Hz → 46Hz` (×pitchFactor) exponentially over 60% of duration. `dur = (0.16 + 0.18v) × decayScale`. Exponential amp decay, 2ms attack. Plus a 12ms bandpassed noise click at 1.4kHz, amp `0.25v`, for the beater. |
| **Triângulo** | Five square oscillators at `5400/6850/8120/9700/11200 Hz` (×pitchFactor, each detuned ±0.5% randomly), amplitudes `0.12/(i+1)`, summed through a bandpass at 7.6kHz (Q 0.7). **Open vs closed by velocity**: `v > 0.55` → `dur = 0.45 × decayScale`, else `0.06 × decayScale`. This velocity-driven articulation is the instrument's defining behaviour. |
| **Pandeiro** | Membrane: sine `330Hz → 180Hz` over 70% of duration, amp `0.55v`. Jingles: white noise highpassed at 6.5kHz, amp `0.40v`, `dur × 0.9`. `dur = (0.10 + 0.10v) × decayScale`. |
| **Ganzá** | White noise through bandpass at `6800Hz` (×pitchFactor, Q 1.2), 4ms attack, `dur = (0.035 + 0.03v) × decayScale`, amp `0.6v`. |
| **Bateria — BB** | Sine `130Hz → 48Hz` over 60%, `dur = 0.14 × decayScale`, amp `1.0v`. |
| **Bateria — CX** | Noise highpassed at 1.7kHz (amp `0.6v`) + triangle at 190Hz (amp `0.4v`, 70% duration). `dur = 0.14 × decayScale`. |
| **Bateria — HH** | Noise highpassed at 9kHz, `dur = (0.03 + 0.04v) × decayScale`, amp `0.45v`. |
| **Bateria — TOM** | Sine `190Hz → 110Hz` over 70%, `dur = 0.2 × decayScale`, amp `0.8v`. |

**Signal chain**: each channel → gain → stereo pan → character bus (dry + lowpass→tanh→wet) →
limiter → master → output.

If the product ships with **samples** instead, keep the same per-channel controls; `PITCH` becomes
playback-rate or a pitch shift, `DECAY` becomes an amplitude-envelope release/truncation, and the
triângulo's velocity-split should map to separate open/closed samples.

---

## Musical Content — Regional Profiles

This is the heart of the product and should be ported verbatim. Notation: 16 characters per bar,
`.` = rest, `1`–`9` = velocity level (`level × 14`, so `9` = 126). Spaces are cosmetic.
Patterns tile to fill 32 steps.

### CAMPINA GRANDE (`CAM`) — default on load
*"Pé-de-serra puro — sanfona, zabumba e triângulo. Swing médio, balanço solto. Timbre HI-FI, bateria em silêncio."*
`bpm 132 · swing 38 · cachaça 22 · timbre HI-FI · bateria MUTED`

```
zabumba    9..5 ..6. 8..4 ..6.
triangulo  7474 7474 7474 7474
pandeiro   ..6. 9..4 ..6. 9..5
ganza      6363 6363 6363 6363
bb         9... .... 9... ....
cx         .... 9... .... 9...
hh         .5.5 .5.5 .5.5 .5.5
tom        .... .... .... ..4.
```

### CARUARU (`CAR`)
*"Forró tradicional pernambucano. Peso extra na zabumba, swing alto. Timbre HI-FI, balanço pesado."*
`bpm 138 · swing 54 · cachaça 32 · timbre HI-FI`

```
zabumba    9..6 .57. 9..6 .47.
triangulo  7575 7575 7575 7575
pandeiro   ..7. 9..5 ..7. 9..6
ganza      7474 7474 7474 7474
bb         9... ..6. 9... ..6.
cx         .... 9..3 .... 9..4
hh         6.6. 6.6. 6.6. 6.6.
tom        .... ...4 .... ..5.
```

### PETROLINA (`PET`)
*"Forró eletrônico do São Francisco. Bateria presente, groove seco. Timbre LO-FI, cachaça baixa."*
`bpm 128 · swing 26 · cachaça 16 · timbre LO-FI`

```
zabumba    9... 9..4 9... 9..6
triangulo  5.5. 5.5. 5.5. 5.5.
pandeiro   .... 7..3 .... 7..4
ganza      8484 8484 8484 8484
bb         9... ..5. 9..4 ....
cx         .... 9... .... 9...
hh         6868 6868 6868 6868
tom        .... .... ...5 ..6.
```

### UNIVERSITÁRIO (`UNI`)
*"Forró universitário, limpo e pop. Quantizado, cachaça quase zero. Timbre HI-FI, pulso reto."*
`bpm 124 · swing 16 · cachaça 6 · timbre HI-FI`

```
zabumba    9... 6... 9... 6...
triangulo  8888 8888 8888 8888
pandeiro   ..7. ..7. ..7. ..7.
ganza      7575 7575 7575 7575
bb         9... .... 9... ....
cx         .... 9... .... 9...
hh         .7.7 .7.7 .7.7 .7.7
tom        .... .... .... ....
```

---

## MIDI Export

Standard MIDI File, **type 0, PPQ 96**, one track, sixteenth = 24 ticks.

- Note-on `0x99` (channel 10), note-off `0x89`, gate length **80% of a step**.
- Tempo meta (`FF 51 03`) from current BPM; time signature meta (`FF 58 04`) = 4/4.
- Muted channels are **excluded**. Ghost notes are **not** exported.
- General MIDI percussion mapping:

| Voice | Note |
|---|---|
| Zabumba | 36 (Bass Drum 1) |
| Triângulo | 81 (Open Triangle) |
| Pandeiro | 54 (Tambourine) |
| Ganzá | 82 (Shaker) |
| Bateria BB | 36 |
| Bateria CX | 38 (Acoustic Snare) |
| Bateria HH | 42 (Closed Hi-Hat) |
| Bateria TOM | 45 (Low Tom) |

Events are collected, sorted by tick, then delta-encoded as VLQ. Reference implementation:
`exportMIDI()` in `audio.js`.

**Also worth doing in the plugin (not in the prototype):** emit the same notes as live MIDI output
on the plugin's MIDI out bus, so the groove can drive other instruments. The plugin already declares
`producesMidi`.

---

## Unimplemented / Stubbed Controls

Designed and present in the UI, but **not functional** in the prototype. Specifications for real
behaviour:

| Control | Current | Intended |
|---|---|---|
| `LOAD` (per strip) | No-op | Open a file browser; load a user sample for that channel; update the sample name. Should accept drag-and-drop onto the strip. |
| `LOAD IR…` | No-op | Load an impulse response for the convolution stage; `MIX` becomes the convolution wet amount. |
| `SYNC` | Toggles visual state only | Follow host tempo and transport; lock step 0 to the host bar. BPM field becomes read-only. |
| Preset name cycler (`‹ PÉ-DE-SERRA 01 ›`) | Cycles 8 labels only | Real preset system: save/load full plugin state. Labels present: `PÉ-DE-SERRA 01`, `BAIÃO SECO`, `XOTE LENTO`, `ARRASTA-PÉ`, `XAXADO 88`, `FORRÓ ELÉTRICO`, `QUADRILHA`, `PISADINHA`. |
| `PAT 01`–`08` (per strip) | Cycles label only | Per-channel pattern variation slots — 8 storable patterns per channel, so channels can run different-length variations. |
| `MULTI-OUT` | Toggles visual state only | Route each channel to its own output bus (5 stereo buses or 5 mono + master). Declare the extra buses in the VST3 bus layout. |
| `BUNDLE: MINIMAL` | Static | Indicates the loaded sample bundle; would become a bundle selector if more packs ship. |

---

## Tweakable UI Settings

The prototype exposes these as a small tweaks panel; in the plugin they belong in a **settings/gear
menu**, persisted globally (not per-project):

| Setting | Values | Default |
|---|---|---|
| Theme | Dark / OP-1 Light | Dark |
| Corner radius | 0px (hard) / 2px | 2px |
| Accent intensity | 35–100% (scales colour saturation and glow strength) | 100% |
| Display font | IBM Plex Mono / JetBrains Mono / Space Mono | IBM Plex Mono |
| Default step count | 16 / 32 | 16 |

Accent intensity is implemented as `--accent-i` (0–1) feeding
`filter: saturate(0.3 + accent-i × 0.7)` on accent-coloured elements and scaling every accent glow.

---

## Accessibility & Input Notes

- Knobs are `role="slider"` with `aria-label` and `aria-valuenow`, focusable, arrow-key adjustable.
- The `STYLE` control is a `role="tablist"`.
- Focus visible on knobs (hub stroke → `--active`).
- **Space** = play/stop.
- All text meets contrast requirements against its background in both themes; accent colours are
  used for fills and indicators, not for small text on coloured grounds.
- Right-click is used for knob reset — in a plugin, ensure this doesn't collide with the host's
  parameter context menu (or move reset to `Alt`+click / double-click and put automation options in
  the right-click menu, which is the DAW convention).

---

## Assets

- **No bitmap assets.** The logo is inline SVG; all textures, meters, and pads are CSS.
- **Fonts**: Space Grotesk + IBM Plex Mono (plus optional JetBrains Mono / Space Mono for the font
  tweak). All OFL-licensed — embed them as plugin binary resources rather than loading from a CDN.
- The logo mark's full path data is in the Header section and in the `.logo-mark` SVG in
  `app.js` (`buildHeader`).

---

## Files

| File | Contents |
|---|---|
| `Forró Box.html` | Entry point: font loading, stylesheet, script order, bundler thumbnail |
| `forrobox.css` | **All visual design**: tokens, both themes, every component, all motion |
| `data.js` | **Musical content**: instruments, accent colours, channel defaults, the four regional profiles with full patterns, timbre definitions, preset labels, `buildGroove()` |
| `audio.js` | Web Audio engine: voice synthesis, channel routing, character bus, limiter, `exportMIDI()` |
| `controls.js` | Reusable `Knob` and `Fader` classes with all drag/scroll/type/reset interaction |
| `app.js` | State model, DOM construction, sequencer, scheduler, humanisation, profile loading, visualiser loop, scaling |
| `tweaks-panel.jsx`, `tweaks-bridge.jsx` | UI settings panel (prototype-only scaffolding — not part of the plugin design) |
| `Forró Box (standalone).html` | Self-contained single-file build, for viewing the prototype offline |
| `uploads/UIUX.md` | The original UI/UX design brief that this prototype was built from |

**To view the prototype**: open `Forró Box (standalone).html` in a browser, press Play (or Space),
and turn `CACHAÇA` up past 88 to see the easter egg. Audio requires a user interaction first
(browser autoplay policy).

### Suggested implementation order

1. Plugin skeleton + APVTS parameter tree + state persistence.
2. Sequencer clock (internal, then host-synced) + the four profiles' pattern tables.
3. Voices + per-channel routing + limiter/master. Verify the grooves sound right before touching UI.
4. UI shell: chassis, scaling, tokens/theme, the `Knob` and pad components (these three carry most of
   the look).
5. Sequencer grid + playhead + hit visualisers.
6. Side panel: profile loading (full state reload) + timbre characters.
7. MIDI export/drag-out + live MIDI out.
8. Easter egg, Ciclotron treatment, settings menu.
