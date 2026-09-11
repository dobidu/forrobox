---
phase: 04-ui-shell
plan: 01
subsystem: ui
tags: [juce, fonts, design-tokens, headless-render, pixel-measurement, cmake, fonttools]

requires:
  - phase: 01-plugin-skeleton
    provides: the editor, ids::channelInfos, the CMake target and cross-check pattern
  - phase: 03-voices-mix-bus
    provides: "the render-then-measure discipline, and the human-verify checkpoint split"
provides:
  - seven embedded OFL font weights with a reproducible asset build
  - both theme palettes, cross-checked against forrobox.css on every build
  - the chassis — four regions, five strips, one scale transform
  - ChassisLayout::StripLayout — reserved head row, accent bar and controls box per strip
  - the headless UI measurement harness (tests/UiTest.cpp), every instrument self-tested
affects: [04-02 knob, 04-03 step pad, 04-04 header and footer, 05 sequencer grid, 06 side panel]

tech-stack:
  added: [fonttools (build-time asset tool, output committed)]
  patterns:
    - "UI claims proved by headless render + pixel measurement; looking at it is a separate checkpoint"
    - "Design values cross-checked against the stylesheet on every build, never transcribed and trusted"
    - "A shared mechanism does not imply a shared value — split the field where the spec splits"

key-files:
  created:
    - scripts/build-fonts.py
    - scripts/verify-theme.py
    - src/Theme.h
    - src/Theme.cpp
    - src/Typography.h
    - src/Typography.cpp
    - src/Chassis.h
    - src/Chassis.cpp
    - src/LookAndFeel.h
    - src/LookAndFeel.cpp
    - tests/UiTest.cpp
    - assets/fonts/
  modified:
    - CMakeLists.txt
    - src/PluginEditor.h
    - src/PluginEditor.cpp

key-decisions:
  - "Space Grotesk instanced offline into four committed statics; JUCE 8.0.12 cannot select a variation axis"
  - "Ink mass, not advance width, is the font-weight instrument (0.45% spread would pass with four copies of one weight)"
  - "theme::Shadows carries TWO raised-edge highlights, because the stylesheet specifies two"
  - "theme::mix interpolates in float; juce::Colour::interpolatedWith quantises the proportion to 8 bits"
  - "The Knob and step pad stay custom Components; ForroBoxLookAndFeel stays thin"

patterns-established:
  - "Every measurement instrument is self-tested against a known answer INCLUDING a case it must reject"
  - "A value cross-check and a pixel probe are different guarantees; a token needs both"
  - "One layout derivation read by both paint and the tests — geometry only paint can see is untestable"

duration: ~4h (2026-09-09 apply) + ~1h (2026-09-11 simplify + unify)
started: 2026-09-09T01:28:00Z
completed: 2026-09-11T00:47:00Z
description: "The chassis, both cross-checked palettes, seven embedded font weights and a headless pixel harness — plus two rendering defects the 1288 checks could not see"
type: Summary
about: "Forró Box"
---

# Phase 4 Plan 01: Chassis, Tokens and Fonts — Summary

**The plugin draws its own chassis at 1200×780 under one scale transform, in both themes, with
seven embedded weights and every colour cross-checked against `forrobox.css` on every build — and
the `/simplify` pass found two rendering defects that 1288 passing checks could not see.**

## Performance

| Metric | Value |
|--------|-------|
| Duration | ~4 h apply (2026-09-09) + ~1 h simplify/unify (2026-09-11) |
| Started | 2026-09-09T01:28:00Z |
| Completed | 2026-09-11T00:47:00Z |
| Tasks | 4 auto + 1 blocking checkpoint, all complete |
| Checks | 1092 → **1348**, on three compilers |
| Negative controls | 20 (apply) + 10 (simplify) = **30, all detecting** |

## Acceptance Criteria Results

| Criterion | Status | Evidence |
|-----------|--------|----------|
| AC-1: seven weights, real and provably distinct | **Pass** | Each face reports its own family and style; ink mass strictly increasing with ≥5% steps (417.3 / 523.2 / 572.3 / 617.9). Both OFL files embedded and asserted in the binary |
| AC-2: every token matches `forrobox.css` and cannot drift | **Pass** | `verify-theme.py` green on every build: 14 tokens × 2 themes, 5 accents, 4 bateria colours, 2 tweakables, **+4 raised-edge highlights added this plan**. Mutation of any single value fails, naming it |
| AC-3: design px, one transform | **Pass** | Child stays exactly 1200×780 at every editor size; header 72 / sequencer 196 / footer 56 / side panel 280 / 5 columns with 1 px gaps; 20:13 held. Six reference renders at 1×/1.5×/2× |
| AC-4: surfaces read as the prototype, both themes | **Pass** | Measured: header `(39,39,39)`, strips `(30,30,30)` = `--panel`, sequencer `(15,15,15)`, footer `(36,36,36)` = `--raised`. Anchor tint `#362720`, the only warm strip. Accent bars exact. Gaps show `--line` over `--bg` |
| AC-5: the instrument proves itself | **Pass** | Every helper self-tested with a rejection case; `DISPLAY` unset throughout. Two instruments were found invalid and fixed — see Deviations |
| AC-6: nothing regressed, three compilers | **Pass** | 1348/1348 under GCC, Clang and MSVC. Windows VST3 builds and installs. `data.js`, `forrobox.css`, `app.js`, `controls.js` unmodified. Editor opens in Ableton Live 12 — screenshot confirmed at the checkpoint |

## Task Commits

| Task | Commit | Type | Description |
|------|--------|------|-------------|
| Plan | `2b64715` | docs | Phase 4 as 4 plans; 04-01 scoped |
| Tasks 1–4 | `249d24b` | feat | Fonts, tokens, chassis, harness |
| Control fixes | `beb5264` | test | Closed the gap the controls found; fixed the checkpoint artefact |
| Checkpoint pause | `fdac04b` | docs | Paused at the blocking human-verify |
| `/simplify` | `21d98b5` | refactor | Two spec defects, one law, one fill |

## What Was Built

| File | Purpose | Lines |
|------|---------|-------|
| `scripts/build-fonts.py` | Fetches, instances and name-patches seven weights; `--verify` proves byte reproducibility | 280 |
| `scripts/verify-theme.py` | Cross-checks tokens, accents, bateria colours, tweakables and the highlight recipes against the CSS | 337 |
| `src/Theme.h` / `.cpp` | Both palettes, 5 accents, shadows, `mix`, `saturated`/`accentFill` | 194 / 105 |
| `src/Typography.h` / `.cpp` | Seven faces, the 21-row type scale, one tracked-text layout | 186 / 167 |
| `src/Chassis.h` / `.cpp` | Region and strip geometry, `StripLayout`, all painting | 207 / 272 |
| `src/LookAndFeel.h` / `.cpp` | Deliberately thin: mode, radius, accent intensity, consumed JUCE colour IDs | 78 / 48 |
| `tests/UiTest.cpp` | The headless harness and the UI suite (a fourth suite, same executable) | 1301 |

## Decisions Made

| Decision | Rationale | Impact |
|----------|-----------|--------|
| Space Grotesk instanced offline into four committed statics | JUCE 8.0.12 has no variation-axis setter, so the VF renders every weight at its fvar default of 300 Light — silently | Do not "simplify" this to loading the VF. `build-fonts.py` reproduces every byte |
| Ink mass is the weight instrument | Advance widths span 0.45% across four weights; a width assertion would pass with four copies of one | Reused by every later UI plan |
| `theme::Shadows` carries two raised-edge highlights | The stylesheet specifies `.header` at white 0.05 in both themes and `.side, .footer` at 0.04/0.50 | A shared mechanism does not imply a shared value |
| `theme::mix` interpolates in float | `interpolatedWith` quantises the proportion to 8 bits and round-trips premultiplied: 27/255 where the spec's 12% gives 24 | The anchor tint is exact — measured `#362720` |
| `ChassisLayout::StripLayout` is public geometry | A rectangle only `paint` can see is a rectangle no test can check — the struct's own stated rule, previously unapplied to the strip interior | 04-02/03/04 drop controls into `interior.controls` |

## Deviations from Plan

### Summary

| Type | Count | Impact |
|------|-------|--------|
| Auto-fixed (apply) | 5 measurement errors | All mine, all before any code was wrong |
| Auto-fixed (`/simplify`) | 2 rendering defects + 2 dead/partial laws | Essential; found by the required flow |
| Scope additions | 1 | The highlight cross-check + pixel probes |
| Deferred | 5 | Logged below |

### Auto-fixed during APPLY — five measurement errors

`isTransparent()` is `alpha == 0`, so the alpha check asserted its own opposite; `checkEqual` on a
`juce::uint8` printed a character; saturation is the wrong instrument for "subtle" at brightness
0.21; the strip-divider composite is over `--bg`, not `--panel`; and `inkMass` over `--panel`
scores ~300 on an empty region, making the head-row check an assertion that could not fail —
`contrastMass` exists because of that.

### Auto-fixed during `/simplify` — two rendering defects

**1. The light theme's header highlight shipped at white 0.50 where the spec says 0.05.** One
`raisedHighlight` field served what `forrobox.css` specifies as two values: `.fb-window > .header`
is 0.05 in both themes (css:87, no light override), `.side, .footer` are 0.04 dark / 0.50 light
(css:600-601). Max-channel error 7/255 over `--raised` `#faf7f0`. Split into `headerHighlight` and
`raisedHighlight`; `paintRaisedHighlight` now takes the colour.

**2. The footer's highlight never rendered at all.** `paintFooter` drew it on row 0 and then painted
`--line` over the same row. `.footer` carries `border-top` *and* an inset shadow, which in the CSS
box model are two different rows. The side panel escaped only because its border is vertical; its
highlight is now inset one column to match.

Also: `ForroBoxLookAndFeel::accent(theme::Accent)` discarded its argument and returned zabumba for
every input (zero callers; 04-02 would have got orange knobs on four of five strips) — deleted.
`--accent-i` shipped at one of its two specified sites — `theme::accentFill` adds the saturation
law. `UiTest.cpp` asserted `large < large + 1.0`, making a two-sided bound one-sided.

### Scope addition

`verify-theme.py` gained the two highlight recipes in both themes — the gap its own docstring named
as uncovered is exactly where the defect landed. `UiTest` gained pixel probes for them, because a
value cross-check does not prove the value reaches a pixel; that probe is what found defect 2.

## Issues Encountered

| Issue | Resolution |
|-------|------------|
| `fontTools --update-name-table` fails at wght 600 (`Cannot find Axis Values`) — STAT declares no named value there | Name IDs 1/2/4/6/16/17 set explicitly |
| Instanced fonts not byte-reproducible while fetched ones were | `head.modified` and its checksum; `recalcTimestamp = False` — assigning the field is not enough |
| `writeReferenceRenders` upscaled the 1× bitmap under a comment claiming that is what the editor does | It is not; `setTransform` is applied by the PARENT. Fixed, and each render's far corner is now asserted |
| My first pixel-equivalence harness reported 0 differing pixels for a deliberately broken variant | `Chassis::paint` opens with `fillAll`, which wiped the injected "old" fill — the harness compared the new code to itself. Rebuilt as two binaries differing only in `paintMatrix`; controls then detected 4,770 and 19,080 px |

## Negative Controls

**30 run, 30 detecting** (20 during apply after two rounds, 10 during `/simplify`).

| Control | Target | Result |
|---------|--------|--------|
| A–D | each highlight recipe, per theme, in `verify-theme.py` | detect |
| E | `accentSpecs` / `channelInfos` order `static_assert` | fails the build |
| G, H | the `paintMatrix` equivalence harness itself | detect (4,770 / 19,080 px) |
| I | tracking zeroed in the now-single tracked-text path | detect |
| J | light header back to 0.50 | detect |
| K | footer highlight painted then overpainted | detect, both themes |

One control (F) did **not** detect and that was the point: it proved the harness invalid before any
claim rested on it.

## Measured Wins

| Change | Before | After |
|--------|--------|-------|
| `paintMatrix` — fill only uncovered columns | 548.7 µs | 3.46 µs (158×) |
| Tracked text via one `GlyphArrangement` | 243.6 µs | 65.0 µs |

`paintMatrix` was verified equivalent, not assumed: **0 of 9,753,796 pixels differ** across six sizes
in both themes.

## Deferred Items

| Item | Effort | Why deferred |
|------|--------|--------------|
| Neither embedded font carries U+266A (♪) for Phase 8's `♪ NO PONTO` | S | Phase 8 needs a fallback face, a drawn glyph or different copy. `build-fonts.py` reports coverage on every run |
| No font subsetting — embedded fonts are ~788 KB | S | Nothing forces it yet; revisit if bundle size matters |
| The remaining shadow/gradient recipes are not cross-checked | M | The recessed/well/pad layers and the header gradient are still hand-transcribed. The two that diverged are now covered; the docstring states the rest as a known gap |
| Render the **editor** in tests, not only the chassis | M | AC-3's scale claim is proved against test code reproducing the transform; nothing renders `ForroBoxAudioProcessorEditor`. A wrong denominator in `resized()` would pass every current check |
| The OFL blobs are not tied to their own family | S | The licence check proves each blob contains the OFL text, not that `SpaceGroteskOFL_txt` is Space Grotesk's — a swapped `forrobox_add_binary_data` order would pass |

## Skill Audit

| Skill | Priority | Invoked | Notes |
|-------|----------|---------|-------|
| `/graphify` | required | ○ **deliberately skipped** | Reason recorded in the plan, not silently omitted: this plan's inputs are literal hex/px values, which a graph does not carry. **Due at 04-02**, whose input is `controls.js`'s interaction semantics |
| `/code-review` | required | ○ **not applicable** | Scope is the audio thread, DSP, processor and APVTS. This plan added no processor code. **Applicable at 04-02**, which attaches parameters |
| `/simplify` | required | ✓ | Four parallel angles; found both rendering defects |
| `/impeccable` | optional | ○ | Not invoked; the checkpoint was answered from the render plus a live Ableton screenshot |

## Next Phase Readiness

**Ready:**
- `theme::` tokens, accents and shadows, all cross-checked; `type::` faces and the 21-row scale
- `ChassisLayout::StripLayout` — `headRow`, `accentBar` and a non-empty reserved `controls` box per
  strip, asserted, so 04-02 places a knob in settled geometry
- The headless harness: `renderComponent`, `pixelAt`, `checkPixel(Near)`, `inkMass`,
  `contrastMass`, `inkWidth`, `warmth` — each self-tested with a rejection case
- `theme::saturated` / `accentFill`, which 04-02's knob and fader arcs also need (`0.4 + i * 0.6`)

**Concerns:**
- The zabumba anchor tint is spec-exact (`#362720`, measured) but reads stronger than "barely
  perceptible" against a near-black panel. Approved as-is at the checkpoint; if it is ever revisited
  that is a **spec** deviation, not a code fix
- `ForroBoxLookAndFeel` is deliberately thin and 04-02 will feel pressure to grow it. The accent
  accessor deleted this plan is the precedent for what that pressure produces

**Blockers:** None.

---
*Built with PAUL Framework · Phase: 04-ui-shell, Plan: 01*
*Completed: 2026-09-11*
