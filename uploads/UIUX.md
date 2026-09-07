# FORRÓ BOX — UI/UX Design Brief

## Overview & Core Design Philosophy

Forró Box is a precision instrument for rhythm production. The UI must feel like one.

The visual language draws from **Teenage Engineering** (OP-1 field, EP-133 K.O.II, OP-Z, Pocket Operators) — industrial-minimal, high-contrast, functional beauty. Controls earn their place. Nothing decorates; everything communicates.

**Anti-references (explicitly forbidden):**
- Painted zabumba / triangle iconography
- Wooden panels or "vintage Brazilian" warm browns
- Any caricature of northeastern Brazilian visual culture
- Skeuomorphic drum machine skins (no Roland TR-808 clone aesthetic)
- Forró festival / São João color palettes (flags, stars, checkered patterns)

The identity of "forró" lives in the sound and the behavior — not the surface. The UI is a precision tool that happens to produce something culturally specific.

**Primary visual references:**
- Teenage Engineering EP-133 K.O.II: matte dark chassis, backlit pads, silk-screened labels, color-coded zones
- Teenage Engineering OP-1 field: clean white/cream chassis, minimal typography, satisfying physical-feel simulation
- oeksound Soothe 3: ultra-clean dark UI, precise control layout, strong typographic hierarchy, no chrome noise
- u-he Satin: premium feel, restrained color, analog depth without skeuomorphism

**One-sentence design target:** A drum machine that looks like it was designed by engineers who love forró — not by marketers who Google image searched it.

---

## Layout

### Overall structure

Fixed-size plugin window. Suggested: **1200 × 780px** (scalable 1x/2x/1.5x for HiDPI).

Three horizontal zones:

```
┌─────────────────────────────────────────────────────────────┐
│  HEADER BAR                                                 │
│  Logo · BPM · SYNC · SWING · CACHAÇA · Preset · Style      │
├──────────────────────────────────┬──────────────────────────┤
│                                  │                          │
│  CHANNEL MATRIX (5 columns)      │  SIDE PANEL             │
│  Zabumba · Tri · Pandeiro        │  Regional Profiles       │
│  Ganzá · Bateria                 │  Timbre / Convolution    │
│                                  │  Asset pack indicator    │
├─────────────────────────────────-┴──────────────────────────┤
│  SEQUENCER STRIP                                            │
│  16/32 step grid · per-channel color · pattern length       │
├─────────────────────────────────────────────────────────────┤
│  FOOTER BAR                                                 │
│  Master Vol · Limiter · Drag MIDI · Output mode             │
└─────────────────────────────────────────────────────────────┘
```

### Header bar

Single horizontal strip, ~60px tall.

Left cluster:
- **FORRÓ BOX** wordmark — tight, geometric, all-caps, no logo mark
- BPM display: monospace numeric readout, large, click-to-type or scroll-to-change
- SYNC button: small backlit toggle (syncs to host transport)
- ÷2 / ×2 buttons: halve or double internal tempo relative to host

Center cluster:
- **SWING** — large rotary encoder with numeric readout below. Range 0–100. 0 = quantized, 100 = maximum groove push.
- **CACHAÇA** — same scale as SWING, same control style. Position in header signals these are global and primary.

Right cluster:
- Preset selector: small display + prev/next arrows. No dropdown — cycle or popup grid.
- Regional Style quick-switch: 4 labeled buttons (CAMPINA / CARUARU / PETROLINA / SP). These are fast-access; the full details are in the side panel.

### Channel matrix

5 columns. Equal width, ~170px each. Height ~360px.

Each column (instrument strip):
```
[ INSTRUMENT NAME — all-caps, large ]
[ COLOR ACCENT BAR — 4px, full width ]
[ WAVEFORM PREVIEW — small, current sample ]
[ SAMPLE SLOT — name + load button ]
[ —————————————————— ]
[ VOL knob ]  [ PITCH knob ]
[ DECAY knob ] [ PAN knob ]
[ —————————————————— ]
[ PATTERN — display + prev/next ]
[ MUTE ] [ SOLO ]
[ GHOST PROB — small slider, 0–100% ]
```

Zabumba column is slightly wider or visually anchored differently as the main rhythmic spine — subtle, not loud.

Bateria column has sub-channel indicators (BB / CX / HH / TOM) visible as small colored dots; clicking expands or opens a secondary sub-view.

### Sequencer strip

Full width, ~140px tall.

- 16 pads default, toggle to 32
- Each pad: lit when active, dim when inactive, instrument-colored
- Pads show velocity as brightness (not separate bar — brightness IS velocity)
- Ghost note pads: same color, half brightness, small dot indicator
- Active-step playhead: thin vertical line sweeping across
- Per-instrument row toggle: click instrument name label on left to isolate/highlight that row
- Loop length control: drag right edge of pad grid to set loop length per instrument (TE-style)

### Side panel

~280px wide, full height.

**Regional Profiles section:**
- 4 profile buttons: CAMPINA GRANDE / CARUARU / PETROLINA / SÃO PAULO
- Active profile: high-contrast highlight
- Below each: 3-line description of what it applies (timbre + swing amount + pattern default)
- "CUSTOM" state appears when user deviates from profile

**Timbre / Convolution section:**
- Three options: HI-FI / LO-FI / CICLOTRON™
- Ciclotron™ gets a small internal joke label: e.g. "TOTAL DISTORTION" in tiny type underneath
- IR load button for custom IR (advanced)
- Mix knob: dry/wet for convolution

**Asset pack indicator:**
- Small status line: "BUNDLE: MINIMAL" or "BUNDLE: EXTENDED"
- If extended pack not installed: dim indicator + download prompt

### Footer bar

Single strip, ~50px.

- Master volume: fader or large knob, left
- Limiter: on/off toggle + gain reduction meter (small)
- **DRAG MIDI**: prominent drag target zone, center. Glows when hovered. "↓ DRAG MIDI" label.
- Output mode: STEREO / MULTI-OUT toggle (multi-out = individual DAW tracks per channel)

---

## Controls

### Knobs / Rotary encoders

Style: flat, minimal. No chrome, no 3D bevel.

- Circle with a single indicator line (not a notch — a line)
- On hover: shows numeric value in small overlay
- Double-click: type exact value
- Right-click: reset to default, copy value, paste value
- Modifier + drag: fine control (Shift = ÷10 resolution)

Size hierarchy:
- Primary (SWING, CACHAÇA, Master Vol): 48px diameter
- Channel controls (VOL, PITCH, DECAY, PAN): 32px diameter
- Secondary (convolution mix, etc.): 24px diameter

### Step pads

TE EP-133 style: physical-feel simulation. Subtle press animation (scale 0.96 on click). Not flat-flat — just enough feedback to feel real.

Colors: each instrument has one assigned accent color. Pad uses that color at full brightness when active.

### Buttons

Two types:
- **Toggle** (MUTE, SOLO, SYNC, output mode): flat rectangle, backlit. Active = accent color fill, inactive = subtle outline.
- **Momentary** (drag MIDI, ÷2 ×2): same shape, lighter press animation.

No rounded-corner excess. Corners: 2px radius max.

### Pattern display per channel

Small OLED-style display (~100×24px):
- Shows pattern name/number
- ← → arrows on either side
- Background: near-black with bright text (monospace)

### Faders

Only used for Ghost Prob (small horizontal) and potentially Master Vol. Style: thin track, round thumb, no chrome.

---

## Visual Style

### Color scheme

**Background base:** `#141414` (near-black, not pure black — avoids harsh contrast fatigue)

**Surface layers:**
- Panel background: `#1E1E1E`
- Raised sections (side panel, header): `#242424`
- Sunken sections (sequencer, displays): `#0F0F0F`

**Typography base:** `#E8E8E8` (off-white, not pure white)

**Instrument accent colors (one per channel):**
- Zabumba: `#E8650A` (deep amber — primary, anchor color)
- Triângulo: `#00C2C7` (electric teal)
- Pandeiro: `#F2C200` (warm yellow)
- Ganzá: `#7ABF6E` (sage green)
- Bateria: `#E84646` (coral red)

These are ONLY used for instrument identification. Not for decoration.

**Accent / interactive:** `#FFFFFF` at 70% for active states, 20% for inactive outlines.

**Danger / warning:** `#FF4136` (used sparingly, only for irreversible actions or errors).

**Alternative light theme option (OP-1 inspired):**
If dark-only feels too constrained, consider offering a light mode:
- Background: `#F0EDE6` (warm off-white)
- Instrument accents: same hues, shifted slightly warmer
- Typography: `#1A1A1A`
This would be unusual in the plugin world and highly differentiated.

### Typography

Single typeface family. Suggestions (open source, JUCE-compatible):
- **IBM Plex Mono** — monospace, technical, warm. Good for numeric displays.
- **JetBrains Mono** — clean, legible at small sizes.
- **Space Grotesk** — geometric sans for labels and headings.

Hierarchy:
- Instrument names: 13px, all-caps, letter-spacing 0.12em, medium weight
- Knob labels: 10px, all-caps, letter-spacing 0.08em, regular weight
- Numeric displays: 16–20px monospace, tabular figures
- Preset / pattern names: 12px, mixed case, regular
- Section labels (SEQUENCER, REGIONAL PROFILES): 9px, all-caps, letter-spacing 0.2em, subdued color (50% opacity)

No decorative fonts. No script. No hand-drawn aesthetic.

### Iconography

Minimal. Use text labels over icons where possible — reduces cultural ambiguity.

Where icons are needed:
- Play/stop/record: standard geometric shapes (triangle, square, circle)
- Mute: strikethrough speaker — or just the text "M"
- Solo: just the text "S"
- Download/drag: simple arrow — no cloud icons

No instrument illustrations. No northeastern imagery.

---

## UX Considerations

### Workflow

Primary workflow for a new session:
1. Pick Regional Profile (or start blank)
2. Each channel auto-loads a matching default pattern
3. SWING + CACHAÇA set the humanization feel
4. Per-channel: adjust VOL/PITCH/DECAY to taste
5. Step sequencer visible at all times — can edit directly
6. Drag MIDI to export current groove

The plugin should be usable within 30 seconds of opening it. Zero-config path must work.

### Groovebox vs. generative

Two mental models must coexist:
- **Groovebox mode**: user controls every step manually in the sequencer. Deterministic. CACHAÇA is 0.
- **Generative mode**: user sets patterns, raises CACHAÇA/SWING, the plugin surprises. Non-deterministic.

The UI must not force one model. CACHAÇA at 0 = transparent deterministic tool. Raised = generative. Same interface, different behavior.

### Presets and Regional Profiles

Regional profiles are **starting points**, not locks. User should feel free to deviate. The profile name in the header dims or shows "CUSTOM" state when the user has modified anything relative to the loaded profile. No warnings, no friction — just acknowledgment.

### Responsiveness

Plugin has a fixed base resolution (1200×780). Must support:
- 1x (standard)
- 1.5x (common Windows HiDPI)
- 2x (Retina Mac)

No dynamic resizing required for v1. Scalable via JUCE's Component scaling.

### Accessibility

- All interactive elements must be reachable and operable via keyboard (tab order)
- Color is never the only signal — instrument identity uses both color AND position AND label
- Contrast ratios: minimum 4.5:1 for text, 3:1 for UI controls (WCAG AA)

### Microinteractions

Keep subtle, functional:
- Pad press: scale to 0.96, 80ms ease-out back
- Knob hover: value tooltip appears, 150ms fade
- Drag MIDI zone: subtle pulse/glow when hovered with cursor
- Profile switch: sequencer pads briefly flash new colors (300ms), not a hard cut
- CACHAÇA > 80%: very subtle, almost imperceptible warm tint shift on background (easter egg — the plugin is "drunk")

### What to discuss with the designer

Key open questions to bring to the Claude design session:

1. **Dark vs. light mode** — commit to one or support both? Light mode would be strongly differentiated from competitors.
2. **Bateria sub-channel view** — how does expanding BB/CX/HH/TOM work without breaking the layout? Overlay panel? Second row?
3. **Channel strip density** — 5 columns at 1200px = ~170px each. Is that enough real estate for 4 knobs + pattern display + mute/solo + ghost prob? May need to reconsider control sizing or layout per strip.
4. **CACHAÇA easter egg at 100%** — how visual / how subtle? Could be a moment of joy in the plugin. Needs careful calibration between "fun" and "professional tool."
5. **Ciclotron™ visual treatment** — this mode should feel slightly different. Subtle visual degradation (scanlines? slight desaturation?) or keep it clean and let the sound speak?
6. **Sequencer step length per instrument** — TE-style drag-to-resize is powerful but complex to communicate. What's the affordance?
