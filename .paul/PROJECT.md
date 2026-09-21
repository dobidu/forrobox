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
| Status | Phase 6 complete (6/6). The side panel loads profiles as a full state reload; the charset is pinned; the phase's duplicated shapes each have one home. Phase 7 — MIDI out — is next |
| Last Updated | 2026-09-20 |

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
- ✓ Eight percussion voices — seven synthesised from PLANNING.md's specs, zabumba from three
      measured velocity layers — with per-channel VOL / PITCH / DECAY / PAN and mute/solo — Phase 3
- ✓ `CACHAÇA` humanisation: per-step timing jitter on a 32 ms delayed origin, per-hit velocity
      variation and ghost notes, every value keyed rather than drawn — Phase 3
- ✓ Character bus (HI-FI / LO-FI / CICLOTRON™), limiter and squared-taper master, smoothed per
      sample — Phase 3
- ✓ The four grooves match the prototype by ear, and the full chain does not clip: 0.571 / 0.806 /
      0.890 / 0.669 against 1.454 unlimited — Phase 3
- ✓ Fixed 1200×780 chassis with a single scale transform, both themes as cross-checked design
      tokens, and seven embedded OFL font weights — Phase 4
- ✓ Knob, step pad, fader, button family, segmented control, value screen and logo mark, each a
      custom Component with its interaction law taken from the prototype's own source — Phase 4
- ✓ The chassis reads as the prototype: header, five channel strips and footer populated and wired
      to real parameters, verified at 1×, 1.5× and 2× in both themes — Phase 4
- ✓ Headless pixel harness: every UI claim is a rendered measurement with `DISPLAY` unset, and every
      measurement instrument is self-tested against a synthetic subject with a known answer — Phase 4
- ✓ Multi-out: six VST3 output buses, each channel's voices to its own stereo bus with the main bus
      keeping the full mix, verified in Ableton Live 12 — Phase 4
- ✓ The sequencer is playable and legible: five rows of pads that show and edit the real pattern and
      follow every writer of it, `STEPS` 16/32 with the tiling law, a continuous playhead, per-channel
      LEDs and activity meters driven by the audio thread's own publication, the BATERIA kit overlay
      reaching the four lanes the collapsed row cannot, and row dimming for mute, solo and a
      visual-only isolate — Phase 5

- ✓ Selecting a regional profile performs a full state reload from either entry point — the side
      panel's list and the header's `STYLE` — with the `CUSTOM` dirty tag, timbre rows, the `MIX`
      knob and a confirmation pad flash, and no click, glitch or audio-thread race — Phase 6
- ✓ Accented text is written as the characters it means, with the source and execution charset
      pinned once in CMake, and a fourth cross-check that reads every literal in `src/` — Phase 6
- ✓ The duplicated shapes the phase surfaced each have one home: `PatternPads`, `SelectableTile`,
      `HitZone`, one `kUiPollHz`, one kit lane table, one click law, one `ChassisRig` — Phase 6

### Active (In Progress)

- [ ] MIDI out — the groove leaves the plugin: SMF type 0 drag-out and live MIDI on the plugin's
      bus (Phase 7)

### Planned (Next)

Suggested implementation order from the handoff (adapted for the native-JUCE GUI path):

- [x] Plugin skeleton + APVTS parameter tree + state persistence — Phase 1
- [x] Sequencer clock (internal, then host-synced) + the four profiles' pattern tables — Phase 2
- [x] Voices + per-channel routing + limiter/master — Phase 3; grooves A/B'd before any UI work
- [x] UI shell: chassis, scaling, design tokens/themes, Knob and step-pad components — Phase 4
- [x] Sequencer grid + playhead + per-channel hit visualisers — Phase 5
- [x] Side panel: profile loading (full state reload) + timbre characters — Phase 6
- [ ] MIDI export / drag-out + live MIDI out
- [ ] Easter egg, Ciclotron treatment, settings menu

### Emerged During Phase 6

- [x] **~~Accented text is written as `\xNN` escapes, and the escapes are greedy.~~ SHIPPED at 06-04.** The charset is pinned in CMakeLists.txt (`/utf-8` on MSVC, the probed GNU pair elsewhere) and the 36 escape sites are written as the characters they mean. `decode_c_escapes`, the split literals and the twelve-character allowlist are deleted. ORIGINAL: 06-02 shipped
      `"m\xc3\xa9dio"`, where `\xa9d` parses as a THREE-digit escape and is out of range: Clang
      refused it, GCC truncated it to `\x9d` silently, and `verify-profiles.py` could not see it
      because it compares SOURCE TEXT. The local fix was to split the literals, teach the verifier
      to join them, and add a runtime test asserting every non-ASCII character is one this text
      uses. That DETECTS the class; it does not make it unrepresentable — and it is the fourth
      local fix, after `PluginProcessor.cpp:985`, `Chassis.cpp:57` and `Chassis.cpp:421`
      (`"Pandeiro M\xc3\xa9" "dio"`, the identical word split the identical way, with no test).
      The root is the escapes. Write the literals as real UTF-8 and pin the charset once in CMake
      (`/utf-8` on MSVC, `-finput-charset=UTF-8 -fexec-charset=UTF-8` on GCC/Clang — NOT `u8"..."`,
      which is `const char8_t*` in C++20 and will not bind to these `const char*` fields). That
      deletes `decode_c_escapes`, `join_literals`, the split-literal comments and the allowlist
      test. Note the allowlist is currently hard-coded to twelve characters and omits `í ú à õ Ç É
      Ó`, so the first future string carrying one is a FALSE FAILURE. Measured at 06-04 planning:
      the source uses **23** distinct non-ASCII characters, so `É Ó Ç · × ÷ … ‹ › ↓ ↗` are all
      outside the list — it has not fired only because the check is scoped to `profileInfos` and
      `timbreSpecs`. **06-04, and 06-04 is now this and nothing else** — it removes code rather
      than adding it, and every plan that ships more strings first makes it bigger
- [ ] **The profile buttons are painted, not components, and 06-03 pays for it.** `TimbreRow` became
      a control; the four profile buttons stayed four rectangles in a layout struct plus a paint
      loop. The discriminator was the state SOURCE (a parameter versus the pattern state), which is
      not a property of a control. 06-03 must add the click, and its only options are a fifth
      hand-rolled container hit-test — joining `SequencerGrid::rowLabelAt`, `Chassis::mouseUp`,
      `Segmented::segmentBounds` and `KitOverlay::mouseUp` — or doing the conversion inside a plan
      whose subject is a seven-way state reload. A `ProfileButton` sibling of `TimbreRow` also
      deletes `refreshFromState`'s `resized()` call and `paintProfiles`' re-derivation of `active`.
      **06-03, FIRST task, before the click is wired**
- [ ] **`collectChildren<T>` hands back raw pointers with no coordinate space.** 06-02 found FOUR
      sites filtering controls by `c->getBounds()` — parent-relative — against chassis-space
      rectangles, correct only while every control was a direct child of the chassis. The side
      panel broke that and each site was converted to `boundsIn`. But `boundsIn` is opt-in and
      `collectChildren` already holds the root, so the discipline is enforced by a comment, which
      is what the enrolment gate exists to replace. Have the collector return root-space bounds
      alongside each pointer. **06-06**, before the ChassisRig in the same plan — the rig's API is
      shaped by it
- [ ] **A fourth copy of the 30 Hz poll rate.** `HeaderBar.h`, `FooterBar.h` and `SidePanel.h` each
      declare their own 30, and the newest one's comment says the other two "already settled on this
      rate for the same reason" — Chassis.h's own hoisting trigger. One `kUiPollHz` beside
      `PollTimer` in `Surface.h`; `seq::kPlayheadPollHz` stays separate because 60 Hz is a different,
      justified decision. Confirmed at 06-04 planning: three copies of 30 (`kHeaderPollHz`,
      `kFooterPollHz`, `kSidePanelPollHz`), with `kStepTilingPollHz` 15 and `kPlayheadPollHz` 60
      correctly separate. **06-06**
- [ ] **The ChassisRig's required API, now that two plans have named it.** 06-01 gave it "poll every
      view once"; 06-02 adds: that verb must cover the side panel; a `withState(fn)` mutate-then-
      settle verb (the new tests open `lockPatternState()` in five scoped blocks purely to set
      `dirty` or `activeProfile`, then refresh by hand); root-space child collection, per the item
      above; an "ink in a box" verb (`contrastMass(image, box, pixelAt(...))` now appears 43 times
      in three spellings, with the reference pixel picked by hand each time); and rendering a CHILD
      rather than the whole chassis. Re-measured at 06-04 planning: `tests/UiTest.cpp` now builds a processor **47** times, opens
      `lockPatternState()` **44** times and spells `contrastMass` **50** times — all three grew
      again during 06-03, and 06-05 did not move them. **06-06, still LAST**, after the two seams it
      is downstream of. Re-measured at 06-06 planning: 47 processor builds, **38** of them with a
      chassis, 35 `attachParameters`, 44 `lockPatternState` and 50 `contrastMass`

### Emerged During 06-06

- [ ] **The footer's `OUTPUT` toggle is still found by scanning.** `HeaderBar::getStyleControl` gave
      the header's STYLE control a name; the footer's toggle needs the same on `FooterBar`, and no
      plan has claimed it. One site, `tests/UiTest.cpp`
- [ ] **Four hand-built chassis rigs must mutate BETWEEN construction and `attachParameters`** — set
      `activeProfile`, set the `steps` parameter, apply a default profile — and `ChassisRig`'s
      constructor attaches unconditionally, so none can use it. The same shape three times. A second
      constructor taking a pre-attach callback covers them
- [ ] **`ChassisRig` has no state verb.** `withState`/`pollAll` were built, reached one caller, and
      were deleted on 02-04's no-caller rule. Whoever converts the ~20 remaining `lockPatternState`
      sites should add one THEN — and must not use a four-view `pollAll` for sites that refresh one
      view deliberately, or those checks stop distinguishing
- [ ] **18 hand-built processor rigs remain and are genuinely different composites** — editor rigs
      (`ForroBoxAudioProcessorEditor`), single-control rigs, `FakePlayHead` audio rigs and the
      processor-only "no UI exists" tests. An `EditorRig` is the next candidate if those grow
- [ ] **`verify-charset.py` now checks `tests/` message literals but not `src/` call shapes.** The
      rule it enforces — a non-ASCII literal must be a direct argument, never built into a
      `juce::String` on the way — is a TEST-message rule. `src/` has its own `CharPointer_UTF8`
      discipline, policed by nothing but comments in `Chassis.h` and `DragMidiButton.cpp`

### Emerged During 06-05

- [ ] **`Button` and `Segmented` still state the click law themselves.** 06-05 put it in
      `forrobox::isPlainClickInside` (Surface.h) and `HitZone`/`SelectableTile` call it, but
      `Button::mouseUp` gates on a `pressed` flag first and tests `contentBox()` rather than the
      local bounds, and `Segmented` gates the same way — so adopting it is a change to THEIR files,
      not to the two that were duplicating each other. Four sites, one law
- [ ] **`KitOverlay::mouseUp`'s containment clause restates what the panel already enforces.** The
      panel is a child with `setInterceptsMouseClicks (true, true)` (`KitOverlay.cpp:405`), so a
      ROUTED click inside it never reaches the overlay at all. The clause can only see
      press-on-scrim → drag-onto-panel → release, and its comment names the wrong case. Either drop
      it — leaving `mouseUp` as the honest "I am the scrim" — or rewrite the reason. This is why
      06-05's AC-4 is partial
- [ ] **`PatternPads.h` has the orphaned-doc-comment defect too**: `kPlayheadPollHz` was inserted
      between `kToggleOnVelocity`'s doc block and its declaration, so the block documents the wrong
      constant. The identical slip happened in `Surface.h` during 06-05 and was caught; this one is
      older and still there. A repeating insertion habit, not a one-off
- [ ] **`ValueTooltip.cpp:41` is a bare `startTimerHz (60)`** — a fourth poll rate with no name,
      numerically `seq::kPlayheadPollHz`. 06-05's "one name per decision" claim does not cover it
- [ ] **228 test-message literals carry non-ASCII as `const char*`** and render as mojibake in
      failure output — `juce::String (const char*)` reads Latin-1, the trap `Chassis.h` documents.
      Cosmetic, but it lands exactly when a check fails and someone is reading. The deep fix is two
      lines: a `const char*` overload on `check`/`checkEqual` in `TestHarness.h` doing `fromUTF8`,
      which fixes all 228 at once. A good 06-06 candidate, since that plan is test-focused

### Emerged During 06-04

- [ ] **`Chassis.cpp:88` builds the `FORRÓ·BOX` wordmark inline on every layout pass**, rather than
      from a `static const` like every neighbour in the same file. Pre-existing — 06-04 only changed
      the bytes — and flagged by `/simplify`'s efficiency pass so it is recorded rather than lost
- [ ] **The Portuguese half of the character repertoire is a hand inventory of a set a Unicode rule
      states exactly.** `tests/UiTest.cpp`'s `allowed` and `scripts/verify-charset.py` both enumerate
      26 accented letters; the rule is "non-ASCII, `category in {Ll,Lu}`, and the NFD base is ASCII".
      Strictly more general — no false-fail on a future `ñ` or `ë` — and it loses nothing, because
      every mojibake pair is still rejected via its SECOND character, which is never a letter. The
      SYMBOL half should stay enumerated: those have named drawing sites and widening the set should
      require saying what draws the new one. `/simplify` altitude
- [ ] **`struct Glyph`'s `U+XXXX` names are prose that nothing verifies against the bytes beside
      them.** A mistyped pair would test the wrong glyph under a truthful-sounding name. One
      `juce_wchar expected` field and one `checkEqual` closes it. `/simplify` altitude
- [ ] **`verify-charset.py`'s accent floor is 40 against a measured 57**, so a conversion that
      silently lost a third of the accents would still pass. Pin it at the true count and let bumps
      be deliberate, or make it per-file
- [ ] **`scripts/verify-charset.py` scans `src/` only.** `tests/` literals legitimately use
      characters this UI does not draw — a `±` in a tolerance message, and deliberate mojibake
      fixtures — so the DRAWN repertoire is the wrong rule for that tree. A `tests/` scan would need
      its own rule. Recorded rather than assumed closed

### Phase 6 cleanup — plan assignments settled at 06-04 planning

The ROADMAP's single 06-04 line named four items; counting at planning found eleven across three
plans. They were split into four plans by the 02-03 / 04-04 / 05-02 test — subsystems that fail in
different ways do not share a plan — and the order is fixed by two real dependencies, not taste.

| Plan | Items | Why here |
|------|-------|----------|
| **06-04** ✓ | UTF-8 charset flags — closed 2026-09-20 | A build and source-encoding change. FIRST: every plan shipping more accented strings makes it bigger, and 06-05 ships more |
| **06-05** ✓ | `PatternPads` owns the whole flash · `SelectableTile` · `HitZone` · one `kUiPollHz` · `ids::lanes` — closed 2026-09-20 (`ViewState` rejected at planning) | Six production hoists, all the same species — a duplicated shape given one home |
| **06-06** | `HeaderBar::getStyleControl` · root-space `collectChildren` · the ChassisRig | Test-only, and LAST: the rig's API is downstream of the two seams above it |

**~~The processor should ANNOUNCE a profile load.~~ JUDGED AND REJECTED at 06-05 planning, with the
user — and the premise I recorded at 06-04 planning was itself WRONG. I wrote that a load arriving
from `setStateInformation`, a preset recall or a future undo "refreshes and flashes nothing".
Reading the code before building it killed that on three counts.**

**The refresh already happens.** Three independent polls follow a programmatic load:
`PatternPads::refreshIfStateChanged` edge-detects `getPatternPublicationCount` at 60 Hz,
`HeaderBar::refreshFromProcessor` reads `selectedProfileIndex()` at 30 Hz, and
`SidePanel::refreshFromState` reads `activeProfile`/`dirty` at 30 Hz. `setStateInformation` writes
through `lockPatternState()`, whose handle publishes on release, so the pads see it. Nothing is
stale.

**The missing flash is SPEC-CORRECT, not a gap.** `loadProfile(id, flash)` takes the flash as a
PARAMETER and the prototype's three callers settle the rule: `app.js:113` (the STYLE quick-select)
and `app.js:282` (the side-panel list) pass `true`; `app.js:757` (`boot`) passes `false`. The flash
confirms a GESTURE. A project opening and flashing its whole grid would be wrong.

**And there is no third caller waiting.** I cited "a preset recall" — but `cyclePreset`
(`app.js:561`) does not call `loadProfile` at all; it advances a preset NAME and calls
`markCustom()`. Both production callers of `loadProfile` exist today and both are already correct.

**What is left is duplication, and a counter is the wrong answer to it.** One law — load, refresh
yourself, flash the pads — is stated at `HeaderBar.cpp:245` and `SidePanel.cpp:212` with two
byte-identical lambdas at `Chassis.cpp:603-604`. Routing it through a `profileLoadCount` would make
the flash fire up to a poll interval after the gesture and force every headless test to drive a
poll to observe it, where today it is synchronous with the click — the same coupling argument that
rejected the channel-gate publication at 06-01. The duplication is instead collapsed in 06-05 by
giving `PatternPads` the whole flash law and `Chassis` one named method instead of two lambdas.
**Do not re-raise.**

### Emerged During Phase 5

- [ ] **The overlay and the grid are two copies of "a component showing a slice of the pattern".**
      `rebuildPads`, `padFor`, `toggleCell`, `refreshFromState`, `refreshIfStateChanged`, `stepCount`,
      `lastPatternGeneration` and the dense row-major pad vector now exist twice, differing in a row
      table. This is not a speculative hoist: both of 05-04 Task 2's fixes were RE-FIXES of bugs the
      grid had already had — "read the pattern once and never again" (05-01's) and "record the
      generation outside the lock" (05-03's, which that plan shipped twice). `readStepWindow` and
      `snapshotPattern` were hoisted at 05-04's close; the rest wants `PatternPads`. **FIRST item of
      the Phase 6 cleanup plan**, before Phase 6's own views make a third copy
- [x] **~~The resolved mute/solo gate should be published with a counter.~~ JUDGED AND REJECTED at
      06-01, with the user.** The finding was that this codebase follows derived processor state
      through counters and the channel gate has none, so the UI recomputes it at 60 Hz. Reading the
      code before building it killed the premise on three counts. **The UI needs five bits, not the
      150-byte `Settings`** — both readers touch only `channels[i].audible` (`Chassis.cpp:542`,
      `SequencerGrid.cpp:381`), so the double-buffer the plan anticipated was never needed. **There
      is no duplicated LAW**, only a duplicated CALL to one pure function — which is what separates
      this from `PatternPads`, where the rule itself was copied and both copies grew the same bug.
      And **publishing would add a coupling the direct read does not have**: `resolveChannelSettings`
      is a pure function of relaxed atomic loads, correct from any thread at any time, whereas
      routing it through `processBlock` would make the UI's dimming depend on the audio thread having
      run — wrong in a suspended plugin and wrong in every headless test that mutes without
      rendering. The pattern needs publication because it lives behind a lock and the step snapshot
      because it is a transient event; this needs neither. The chain it was meant to remove is not
      load-bearing either: `/simplify` established `rowDimmed`'s real reasons (the `const` label
      painter, the rebuild, and five `repaint` calls with no early-out) and measured the resolve at
      11.7 ns. **Do not re-raise in 06-04.**
- [ ] **Three container-level rectangle hit-tests arrived in one plan.** Before 05-04 every
      `mouseUp`/`mouseMove`/`mouseExit` override in `src/` was on a CONTROL. `SequencerGrid`
      additionally reimplements hover tracking, cursor switching and targeted repaint, all of which
      `Button` already does. A ~25-line invisible `HitZone` with `onClick`, `onHoverChanged` and a
      cursor deletes ~50 lines from the grid and one from the chassis. Phase 6's side panel is the
      fourth instance
- [x] **~~`ViewState` for `{ isolated, bateriaOpen }`.~~ JUDGED AND REJECTED at 06-05 planning, with
      the user.** Two counts. It contradicts a Key Decision that is still Active: 05-04 recorded
      that the isolate "lives in `SequencerGrid` — not in `State`, not a parameter, not persisted…
      keeping it in the grid means there is no path by which it could reach the engine", and a
      struct shared with the overlay is precisely such a path. And it has no consumer — nothing
      reads the two fields together, which 02-04's own principle answers: a guarantee with no caller
      is not a guarantee. The item survived two corrections (06-01 claimed PLANNING groups all
      three; 06-02 established `dirty` is persisted and scoped it to two) without either revisiting
      whether the remaining two wanted grouping at all. **Do not re-raise.** ORIGINAL, kept because
      the `dirty` correction inside it is still load-bearing: **`ViewState` for
      `{ isolated, bateriaOpen }` — and NOT `dirty`. CORRECTED at 06-02.** I wrote
      this item at 06-01's close claiming `PLANNING.md:676-677` groups all three. It does not:
      :676-677 are `isolated` ("Visual only") and `bateriaOpen` ("UI only"), while **`dirty` is at
      :670**, inside the persisted block, and `:706` requires it to round-trip — "profile id, dirty
      flag, full grid, step count". It is serialised at `ForroBoxState.cpp:74` and read back at
      `:113`, whereas `isolated` and `bateriaOpen` appear in neither file by design. So 06-02's
      reading of `dirty` out of `State` is correct and the `ViewState` this item asks for must be
      scoped to the two UI-only fields. Pulling a persisted field into it would make
      `SidePanel::refreshFromState` a two-source read for two fields one action writes together —
      a profile load sets `activeProfile` and clears `dirty` in the same breath (`Profiles.cpp:192`)
- [ ] **`ids::lanes` should be one array of structs.** `kitLaneIds` and `kitPieceName`'s `names` are
      two parallel four-element tables bound positionally, and `KitOverlay::paintPanel` spells the
      short code a third way as `ids::lanes[lane].toUpperCase()`. `ids::channelInfos` states the rule
      in its own comment: "one array of structs makes divergence impossible instead of detectable"

- [x] **The grid has no writer but itself.** SHIPPED at 05-03. `SequencerGrid::refreshFromState` runs on attach and
      after its own `toggleCell`, and nothing else. Two other writers exist and neither reaches it:
      `setStateInformation` (`PluginProcessor.cpp:1018`) replaces the lanes wholesale on a project or
      preset recall, and `ids::steps` is a host-automatable choice that `processBlock` reads live
      (`PluginProcessor.cpp:576`) while `rebuildPads` snapshots it once. With the editor open, a
      recall shows the PREVIOUS pattern until the user clicks a pad, and a STEPS automation to 32
      leaves steps 16-31 invisible and uneditable behind a clock already playing them. Found by
      `/code-review` at 05-01; distinct from the empty-grid gap below, which is about nothing
      APPLYING a pattern rather than an applied one not ARRIVING. **Reassigned to 05-03 at 05-02
      planning** — I had put it on 05-02 at 05-01's close, and counting the work showed that makes
      05-02 five tasks across three subsystems. It is a message-thread pattern write with nothing to
      do with what the audio thread publishes
- [x] **Nothing owns the `STEPS` 16/32 buttons.** SHIPPED at 05-03. 05-01 reserves their boxes and leaves them empty;
      no plan claims them and the ROADMAP names `steps` only as a Phase 1 APVTS parameter.
      `PLANNING.md:606-607` fixes the law — switching TILES rather than clears,
      `newArray[i] = oldArray[i % oldLength]` — which is a pattern write of exactly Task 3's shape.
      **05-03**, with the refresh gap above: reassigned from 05-02 at 05-02 planning for the same
      sizing reason
- [ ] **`tests/UiTest.cpp` has no chassis rig, and `/simplify` has now said so three plans running.**
      22 sites build the identical six lines — `ForroBoxAudioProcessor` + `ForroBoxLookAndFeel` +
      `ValueTooltip` + `Chassis` + `setBounds` + `attachParameters` — against SEVEN existing
      single-control rigs (`KnobRig`, `ButtonRig`, `AttachedKnobRig`, `StepPadRig`,
      `AttachedFaderRig`, `ControlRig`, `AttachedBpmRig`). The composite is the one with no rig and
      the most repetition. It is not only tidiness: one of those sites carries a comment recording
      that the processor must be declared BEFORE the chassis or MSVC crashes the whole suite on
      freed parameters — an invariant re-established by hand 22 times and documented at one of them,
      which a rig's member order would make unrepresentable. Flagged at 05-01, 05-02 and 05-03;
      recorded here rather than deferred a fourth time in silence
- [ ] **The strip's LED and activity meter should be child Components, not painted by the strip.**
      `Playhead.h` argues it in 05-02's own diff — *"a narrow component repaints its own rectangle
      and the pads underneath it are untouched"* — and `GainReductionMeter` already does it at 30 Hz.
      05-02 put the chassis's only two 60 Hz movers into the PAINTED path instead, and `/simplify`
      measured the cost: `paintStrip` runs sixty times a second to move an 8 px dot and a 161 px bar,
      and 85% of each run is furniture redrawn identically. Culling each piece against the clip took
      steady state from ~612 us a frame (3.7% of a core) to ~220 us (1.3%) and is what shipped; the
      conversion would take it to ~86 us and delete the glow-expansion arithmetic at the repaint
      site, because the bounds would carry the glow the way `StepPad::boundsForPadRect` and
      `Playhead::boundsForLineAt` already do. Structural, so it wants its own change
- [ ] **`Chassis::paintStrip` culls against `g.getClipBounds()`, a bounding BOX.** Two lit strips at
      opposite ends therefore repaint all five — measured at 429 us against 152 us for two adjacent.
      The fix is to cull against the real region (`reduceClipRegion` then `isClipEmpty`), and it is
      largely subsumed by the component conversion above. `Chassis::paint` has the same shape one
      level up
- [ ] **`ScopedControlCallbacks` shares the LAW but leaves the LIST hand-copied.** Each owner passes a
      reset lambda naming its callbacks, which must agree with a second hand-maintained list — the
      `c.onX = ...` assignments — written 20-60 lines away, with nothing comparing the two.
      `/code-review` found `KnobAttachment`'s list carrying a callback it never installs: a 1-in-5
      divergence rate on the longest list, with no detector. The reset is now a function pointer, so
      a capturing lambda cannot compile, but the list is still stated twice. The deeper fix is to
      make INSTALLATION register its own clear — `knob.install<&Knob::onNudge> (...)` storing a
      captureless thunk — which deletes the reset parameter and all five lambdas and makes the bug
      unrepresentable. Deferred at 05-01's close: it rewrites five classes' construction and the plan
      boundary said Task 1 extracts the lifetime guard and nothing else
- [ ] **`StepPad` rasterises a `juce::DropShadow` per lit pad, per paint.** Measured by `/simplify`
      at 05-01: 19 us per lit pad, so campina's 57 lit pads add 1.62 ms to a single chassis render
      (3.24 -> 4.86 ms, +50%); `StepPad::paint` is 1.23% of all suite instructions and the glow is
      40% of that. Each call builds a 10-stop gradient and issues 9 gradient-filled rectangles, of
      geometrically identical shapes — there are 5 accents, one radius and at most 2 pad widths.
      Cache per (accent, width) into an Image, or replace the 9-section gradient with 3 concentric
      strokes. NOT this diff's code — it is 04-03's; 05-01 changed the multiplier from one pad in a
      test rig to 80 on screen
- [ ] **`flexRow` / `centredInRow` / `textBox` / `tileAcross` are a CSS box-model vocabulary parked in
      `Chassis.h`.** They are why `HeaderBar.h`, `FooterBar.h` and `SequencerGrid.h` each include a
      700-line header. A FOURTH job in that file, distinct from the three `ChassisLayout` already
      carries — so it belongs on Phase 6's list explicitly rather than riding along with them
- [ ] **A fresh instance claims a profile it is not playing.** `State` initialises `activeProfile` to
      `"campina"` and every lane to zero, so STYLE lights CAMPINA, the grid is empty and play is
      silent. Phase 6 owns the fix; it is PROJECT.md's own "usable groove in under 30 s, zero config"
      metric, and the grid is what made it visible

### Emerged During Phase 4

- [x] **The attachment lifetime guard is hand-copied across five classes.** — settled 2026-09-14:
      extracted at 05-01 Task 1 as `ScopedControlCallbacks`, declared BEFORE the
      `juce::ParameterAttachment` in all five so the parameter listener goes first. `/code-review`
      then found that `KnobAttachment`'s copied list cleared `onProportionChanged`, a callback
      `HeaderBar` installs and this class never did; removed, and the seam now has a test.
      ORIGINAL: Every attachment holds
      its control through a `juce::Component::SafePointer` and nulls the control's callbacks in its
      destructor, each re-explaining the use-after-free AddressSanitizer confirmed at 04-03.
      `/simplify` named the fix — a small `ScopedControlCallback` — and it was deferred at a phase
      close. Worth doing early in Phase 5, before a sixth copy
- [ ] **`Segmented::setReadOnly` has no production caller.** 04-05 gave it one and 04-06 took it
      away, which sits awkwardly against 02-04's "a guarantee with no caller is not a guarantee".
      Delete it, or give it the caller `/simplify` suggested: dim OUTPUT when no aux bus is enabled,
      which is the one real failure mode left — a host with no per-bus enable UI
- [ ] **`ChassisLayout` is three jobs** — region geometry, strip geometry, and a home for stub
      literals. Recorded at 04-04 and still deferred; Phase 6 replaces every stub literal and is the
      forcing function
- [ ] **A shared pressable protocol** for `Button`, `StepPad` and `DragMidiButton` — three instances
      of reserve-margin / reduce-inverse / `hitTest`, each carrying its own copy of the reasoning.
      `Fader` was re-judged twice and is NOT a fourth
- [ ] **DRAG MIDI's idle pulse and bobbing arrow** are deferred to Phase 7 with the MIDI export. An
      animated call to action for a control that does nothing is the loudest possible lie

### Emerged During Phase 3

- [x] **`ids::outputMode` ships inert and Phase 4 must decide its fate** — settled 2026-09-14:
      implemented for real in 04-06. Five aux stereo buses in the VST3 layout, per-channel routing,
      and the OUTPUT toggle live. Stems are pre-character, pre-limiter and pre-master, so the five
      summed deliberately do not equal the main mix; the main bus carries the full mix in both modes
      so a host that never enables the aux buses cannot go silent
- [ ] Per-voice gain and pan smoothing. The bus smooths its four values per sample; VOL and PAN are
      still constant per block per voice, so automating either steps at block boundaries. Named in
      the Phase 3 design input and left out of all three Phase 3 plans' scope
- [ ] Merge the two voice pools into one `PooledVoice`. 128 synth + 48 sample slots with two
      stealing policies; blocks per-strip `LOAD` and the zabumba *pá* articulation

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
      zabumba one-shots are embedded, three of them play as velocity layers, and the four
      tempo-locked loops do not ship
- [ ] Adopt `juce::UnitTest` when the suite next grows, rather than the hand-rolled harness

### Out of Scope

- Line-by-line port of the prototype's JavaScript — the prototype is a design reference; correct
  plugin practice (threading, sample-accurate timing, automation, state persistence) wins wherever
  the two conflict.

  For **tonal** conflicts, which that list does not reach, the tie-breaker recorded at Phase 3:
  reproduce an unchosen prototype default unless reproducing it would make the plugin audibly worse
  at levels the plugin actually reaches. Phase 3 kept Web Audio's default lowpass Q of 1.0 on that
  basis (free and audible) and declined its waveshaper's clamp outside [-1, 1] (the grooves reach
  1.454, so the clamp would cap at an arbitrary input level). Both are in `src/ParameterIDs.h`
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
| Hybrid held: seven synthesised lanes, zabumba from three measured velocity layers | The fourth `ZAB_LOW` file measures as a *pá* hit (centroid 527 Hz, 78% of energy above 160 Hz), not a low variant — an articulation classifier keeps it out rather than letting filename order pick the layers | 2026-09-08 | Active |
| `CACHAÇA`'s bipolar jitter is bought with a FIXED 32 ms reported latency | A hit jittered early must sound before its step, and that block is already rendered. One-sided jitter drifts the groove late and halves the range; a knob-scaled latency forces a host re-negotiation mid-session. 32 ms because a ghost's ±10 ms sits on top of the step's ±22 ms | 2026-09-08 | Active |
| Every humanisation value is a keyed hash of (seed, step, lane, purpose, index), not a draw from a stream | Conditional draws made muting one channel re-time another by up to 21 ms, and the first fix — drawing unconditionally — missed five per-lane detune draws one level down. A value that is a function of its key cannot be reached by gating, lane order, or anything a later phase adds. Costs 2.460 ns against `juce::Random`'s 2.346 | 2026-09-08 | Active |
| Stochastic behaviour is tested as a distribution AND as a seeded exact render, with tolerances measured or computed from the binomial standard error | Asserting seeded values alone pins the draw order and breaks on any refactor while letting a wrong distribution through; a guessed tolerance is either flaky or blind. Per-bucket floors cannot tell triangular from uniform — outer/inner bucket mass can | 2026-09-08 | Active |
| The output stage is a `MixBus` SIBLING of the engine, not part of it | Overrides the original Phase 3 design input. The engine is voices — a pool, per-voice state, per-lane keys; the bus is one global stage with no per-voice anything. `getLatencySamples` and `getTailLengthSeconds` are chain sums, and each stage declares its own contribution even when it is zero | 2026-09-08 | Active |
| Audio claims are proved by offline render and measurement; listening is a separate human checkpoint | No audio device is guaranteed on WSL2, and "is it silent" passes for a wrong-but-audible voice. Twelve of my own measurements were wrong before the code was, so every instrument in `TestHarness.h` is self-tested against a synthetic signal with a known answer | 2026-09-08 | Active |
| Every writer publishes automatically, via the state handle's destructor | "Every writer must remember" is the invariant that fails. Both production state methods had bypassed it | 2026-09-08 | Active |
| The chassis is a fixed-size child under one scale transform; no component does its own scaling arithmetic | Every layout number in every UI file is then a DESIGN pixel, and 1×/1.5×/2× are one code path. Renders are produced THROUGH the transform rather than by upscaling a bitmap, which keeps text and hairlines crisp | 2026-09-11 | Active |
| Every UI claim is a rendered pixel measurement, headless, and every instrument is self-tested including a case it must reject | Twelve of Phase 3's audio measurements were wrong before the code was; the UI repeated it. Two instruments in Phase 4 could not fail as first written — a glow check that counted "equal" as a descent, and a pixel comparison whose `getbbox()` read an all-zero alpha channel as an empty image | 2026-09-11 | Active |
| A flex row is as tall as its TALLEST child, written once as `flexRow`/`textBox`/`centredInRow` | Seven content-sized boxes each restated one child's size and two shipped short. Patching each as it surfaced was the wrong altitude | 2026-09-12 | Active |
| Three interaction laws, deliberately NOT unified: knob `dy/160` anchored, fader absolute from the track, BPM 0.5/px anchored | Three CSS/JS sources give three laws. A shared seam would need a `setProportion` the button cannot answer and an `onDragTo` it can never fire | 2026-09-13 | Active |
| Each region bar owns its own layout, and a control's position is compared in its OWNER's coordinate space | `Component::getBounds` is parent-relative. The header worked only because it sits at the chassis origin; the footer at y=724 made two tests silently read the wrong control. Any region added below the header hits this | 2026-09-13 | Active |
| A read-only control still needs the display half | Found twice in one plan: OUTPUT read its parameter once at build time, and STYLE did the same with `activeProfile`. Read-only is about INPUT | 2026-09-13 | Active |
| `forrobox::atomicMax` is the one law for publishing a running max from the audio thread | A load followed by a store is not a read-modify-write. MixBus's gain reduction resurrected peaks a reader had consumed; VoiceEngine's peak voice count had the same bug, uncommented | 2026-09-13 | Active |
| Pattern tiling on a STEPS change is owned by the PROCESSOR, not the UI | Host automation can change the step window with no editor open. A UI-owned tiling would make the same automation produce a different groove depending on whether a window happened to be open | 2026-09-15 | Active |
| The row isolate is visual only, and lives in `SequencerGrid` — not in `State`, not a parameter, not persisted | `PLANNING.md:592` calls it a focus aid that does not affect audio. Keeping it in the grid means there is no path by which it could reach the engine, and a reopened project restoring which row someone was squinting at would be surprising. A test renders eight blocks before and after and compares them sample for sample | 2026-09-16 | Active |
| The kit overlay has no backdrop blur, and its scrim does not fade with the panel | `css:556`'s `blur(3px)` has no JUCE equivalent short of capturing and blurring the region behind, and the `--bg` 78% scrim in the same rule does the separation alone (decided with the user). `css:557` declares no transition on `.subview`, so the dimmed chassis appears at once and only `.subview-panel` slides and fades over it | 2026-09-16 | Active |
| A component that animates owns its own tick, and is told its elapsed time | Four components already own a `PollTimer`; the overlay's living in its parent cost `Chassis` a member meaning "when another component's animation last ticked" and a call placed above its own early return. The driver reads the clock and the animation never does, which is what lets a test drive it to any point — 04-04, where three checks failed on MSVC's clock rather than on the code | 2026-09-16 | Active |
| A constant in an enrolled geometry header must be compared by an expectation, or excused by name | Enrolling a header only lets the reader FIND a name; the loop iterates the expectations, so an unlisted constant is a check that cannot fail. That shipped three times — `kTrailGap` as 0 under a comment saying 3 px, `kPadHeight` unpoliced, and four easing points invisible because they were `double`. `check_enrolment_coverage` now fails on any new one | 2026-09-16 | Active |
| An invariant that fails on ONE compiler belongs in the TYPE, not a comment | The chassis holds a `KnobAttachment` per knob; each deregisters on destruction, so the processor must outlive it. Re-established by hand at ~38 sites, written down at one. Linux tolerates the wrong order; MSVC crashes the suite — proved by mutation. A base class is destroyed after every member, so no reorder can break it | 2026-09-20 | Active |
| The source and execution charset are pinned once in CMake; accented text is written as characters | Four local fixes of one greedy-escape bug, and the fourth still shipped broken to two compilers. Pinning makes the class unrepresentable rather than detectable. `/utf-8` on MSVC is the load-bearing flag; the GNU pair is probed, not assumed | 2026-09-20 | Active |
| A test message carrying non-ASCII must be a DIRECT argument to `check`/`checkEqual`/`section` | `juce::String (const char*)` reads Latin-1 while `operator+=` reads UTF-8, so `"… — " + mode` mangles before any overload can see it. Overloads alone left 20 mojibake lines in a PASSING run; the rule is a gate in `verify-charset.py`, which then found three more | 2026-09-20 | Active |
| A hoist that moves a constant between headers must be checked against the enrolment gate | Moving three poll rates into `Surface.h` took them OUT of `verify-geometry.py`'s coverage and the gate kept passing. The gate's dependencies are now scraped from its own list, with `CONFIGURE_DEPENDS` and a floor | 2026-09-20 | Active |
| Multi-out stems are pre-character, pre-limiter and pre-master; the main bus keeps the full mix in both modes | Conventional for a drum machine and keeps `processBlock` allocation-free. The five stems summed therefore do NOT equal the main mix — `tanh` is not distributive and the limiter acts on the sum, and a check asserts that with the reason in its message. Main stays full so a host that never enables the aux buses cannot go silent | 2026-09-14 | Active |

## Success Metrics

| Metric | Target | Current | Status |
|--------|--------|---------|--------|
| Grooves judged authentic against the prototype (A/B listening) | All 4 profiles | All 4 approved at the 03-03 checkpoint | Achieved |
| Full-chain headroom | No sample above 1.0 | 0.571 / 0.806 / 0.890 / 0.669 across the four grooves | Achieved |
| Timing accuracy of triggers | Sample-accurate; step 0 locked to host bar when synced | Achieved (4/4). Step sequence independent of buffer size; positions within one sample across partitions | Achieved |
| State round-trip (profile, dirty flag, full grid, step count, all params) | Lossless save/reload | Lossless — 1092 checks, 3 compilers | Achieved |
| Time from plugin open to a usable groove | Under 30 s, zero config | - | Not started |
| Audio-thread safety | No allocation or locks in the audio callback | Zero allocations measured by counter; the only lock is a try-lock the audio thread never waits on | On track |
| DAW validation | Passes VST3 validator; loads in Reaper, Live, Bitwig | Loads in Ableton Live 12; validator deferred | On track |
| UI fidelity vs. prototype | Both themes match closely at 1×, 1.5×, 2× | Header, strips and footer approved at four visual checkpoints; 18 reference renders | On track |

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
| Samples | Four `ZAB_LOW` one-shots embedded via an inlined `juce_add_binary_data` | 48 kHz / 24-bit stereo, resampled once at `prepareToPlay`; ~450 KB. Three are velocity layers; the fourth measures as a *pá* and is not played |
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
*Last updated: 2026-09-16 after Phase 5 — the sequencer is playable and legible, and every lane is reachable*
