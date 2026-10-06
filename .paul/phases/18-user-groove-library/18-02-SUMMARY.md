---
phase: 18-user-groove-library
plan: 02
subsystem: ui
tags: [user-grooves, side-panel, tabs, header, inline-prompt, settings-menu, listeners]

requires:
  - phase: 18-user-groove-library
    plan: 01
    provides: "UserGrooveLibrary, the USER profile, the processor's user-groove API"
provides:
  - "Side panel tabs REGIONAIS | MEUS; UserGrooveList: one ProfileButton per user groove, accent stripe, feel as description, scrolling"
  - "Gear menu GROOVES band: Save groove as… / Save over / Rename… / Delete…"
  - "HeaderBar inline prompt over the preset screen: name entry, delete confirm, timed per-action error"
  - "UserGrooveLibrary listeners (locked list, nested writes refused, generation counter); every instance told of another's overwrite/delete"
  - "STYLE control removed; preset cycler spans the side panel's column at 12 px; knob group centred as near as STOP allows"
  - "type::upperCase (accent-aware) for every uppercase style; ValueScreen::fits"
affects: [19 (export/import lives beside these UI entry points), the v0.3 release]

key-files:
  created: [src/UserGrooveList.h, src/UserGrooveList.cpp]
  modified: [src/UserGrooves.*, src/PluginProcessor.*, src/ProfileButton.*, src/SidePanel.*, src/SettingsMenu.*, src/Chassis.*, src/HeaderBar.*, src/Typography.*, src/ValueScreen.*, src/ForroBoxState.*, src/Segmented.*, src/ParameterIDs.h, scripts/verify-geometry.py, tests/UiTest.cpp, tests/UserGroovesTest.cpp, tests/RigStart.h]

key-decisions:
  - "Label MEUS GROOVES → revised at the checkpoint: tabs REGIONAIS | MEUS, each user groove its own button with an accent stripe, the lit one showing BPM · swing · cachaça (user)"
  - "Names typed inline on the preset screen; deletes confirmed inline (user)"
  - "STYLE control (CAM CAR PET UNI) removed; its room went to the cycler (user)"
  - "Cycler spans the side panel's column; preset screen face 12 px; knob group centred (user, at the checkpoint)"
  - "Sanctioned deviations from forrobox.css: STYLE removed, preset screen size and face, header cycler alignment — recorded in place; the screen's type row left the CSS gate with its reason"

duration: ~2 sessions (three checkpoint rounds)
completed: 2026-10-06
description: "The user's grooves on screen: REGIONAIS | MEUS tabs with one marked button per saved groove, a gear-menu GROOVES band, inline naming and delete confirm on a widened preset screen, every instance told of another's changes — STYLE removed at the user's call; green on four CI jobs and tested in Ableton"
type: Summary
about: "Forró Box"
---

# Phase 18 Plan 02: The user groove library on screen

**A user saves, finds, renames and deletes their grooves without leaving the chassis.** The
side panel's profile section is two tabs. Under MEUS each saved groove is a button like
CARUARU's, marked with an accent stripe; the lit one shows its feel. The gear menu carries the
GROOVES band; names are typed on the header's screen and deletes are confirmed there. The user
tested it in Ableton (Windows) before approving.

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: user grooves in the side panel | **Pass (revised)** | Planned as one MEUS GROOVES entry; the checkpoint replaced it with tabs and one button per groove. Tested: name order, stripe in ink, click loads, lit by the regional rule, edit darkens, tab follows scope changes only, scrolls with 11 grooves and keeps the lit one in view (including after a re-sorting rename), fits with natural heights on both tabs |
| AC-2: the gear menu's GROOVES band | **Pass** | Enablement and names; no Settings write |
| AC-3: the inline prompt | **Pass** | Save as, rename (pre-filled), delete confirm (typing ignored; named when it fits, else APAGAR ESTE GROOVE?), Esc / outside click / focus loss cancel, empty name keeps it open, poll and cycler held; a failed write shows a per-action error for ~2 s of polls, ended by an arrow |
| AC-4: every instance told | **Pass** | B dirty when A overwrites or deletes X, pristine on a rename; nested write refused (expected assertion); destroyed processors leave the list |
| AC-5: it looks right | **Pass** | Three checkpoint rounds; "approved" after testing in Ableton |

**Verification:** Linux GCC + Clang 5384/5384; Debug 5389/5389 (0 unexpected assertions); local
gate 3/3; WSL Windows 5384/5384 + pluginval, installed to `D:\VST3` (hashes match); CI on
`d97a55c` (run 37434155992): all four jobs green. After /simplify, `3a7ce5f` (run 37436499507): all four jobs green, 5384/5384 and Debug 5389/5389.

**Mutation proofs (~35):** every new behavioural check was broken on purpose and seen to fail —
listener dirty-marking, deregistration (crash), nested refusal; the tab rules, stripe, rebuild,
scroll-into-view, visibility, box sizing; menu enablement; empty-name acceptance, outside-click,
focus-loss, confirm typing, poll/cycler holds, message hold and its clearing, per-action text;
header alignment and screen width; the fit check (which the first version could not fail —
an overflow squashes boxes to zero height, so it now compares natural heights).

## Checkpoint rounds (the user's decisions)

1. **One MEUS GROOVES entry → tabs with one button per groove.** "Os grooves de meus grooves
   deviam ter botões como Caruaru, Campina Grande" — chosen: REGIONAIS | MEUS tabs, an accent
   stripe on the left edge, the feel as the lit button's description.
2. **STYLE removed.** "Reavaliar a presença dos 4 botões de style" — removed; its room widened the
   groove screen.
3. **Proportions.** The tabs (spacing, underline), the knob group (centred as near as STOP
   allows: ~26 px right of centre where exact centring would overlap STOP), the cycler spanning
   the side panel's column with a 12 px face.

## /code-review (required)

**Fixed (all mutation-proved):**
- Scroll-into-view ran only on a new active id; a re-sorting rename or resize could hide the lit tile.
- A failed write still ran the reload flash.
- The error's hold was spent by any refresh, not by poll ticks, and survived an explicit arrow.
- Every failure said "NÃO SALVO"; now per action (SALVO / RENOMEADO / APAGADO).

## /simplify (required, Phase 18 close): four angles, one commit

**Applied:**
- Library generation moves only when the bank changed (an arrow press no longer rebuilds every
  editor's list); `find` allocates nothing per comparison (30 Hz poll); `changeExisting` shares
  the overwrite/rename/remove guard-rescan-notify shape.
- Processor: `activeIds()` (five copies), `writeFeel()` (three loads), `steppedIndex()` (both
  banks), `State::tileLane` (one tiling law).
- `type::upperCase` for every uppercase style (the accent bug was latent in all of them);
  `ProfileButton` builds its strings once.
- `ValueScreen::fits`; `indexOfProfile` without STYLE's fallback; dead `screenMessage` and the
  STYLE label's type row removed; one prompt-then helper in Chassis; `SettingsMenu::build` default.
- Tests: one `ScopedGrooveFolder` in RigStart.h (deleter first — a hand copy deleted before its
  redirect let go); harness `utf8` and `renderComponentScaled` reused.

**Skipped, with reasons:**
- Ellipsising in `ValueScreen` (would change every screen's overflow behaviour; the confirm's
  fallback covers the one caller).
- A bank-resolver abstraction for regional/user (two banks; worth it at a third consumer).
- A typed failure enum from the library (no duplicate-name rule planned).
- An override slot on the preset screen (one message writer today).
- A shared inline-editor helper across BpmField/Knob/HeaderBar (key handling genuinely differs).
- `ProfileSelection::user` + `userGroove` kept: "user scope with an unknown/empty id" is a real state.
- `Segmented::Variant::quickSwitch` kept as the design's second segmented style; `kPresetScreenMinWidth`
  still feeds `preferredWidth`.

## Deviations

| Type | Count | Notes |
|------|-------|-------|
| Design (user) | 3 | Tabs + per-groove buttons instead of one entry; STYLE removed; cycler/screen/knob-group proportions |
| Design source | 3 | Sanctioned deviations from forrobox.css, recorded in code; the preset screen's type row left the CSS gate |
| Process | 1 | A build failed at the geometry gate and an old (mutant) binary ran; every build's exit code is now checked |

## Next
**Phase 18 complete.** Next is Phase 19, groove files (export/import of the `.forrogroove` format).

---
*Phase: 18-user-groove-library, Plan: 02 · Completed: 2026-10-06*
