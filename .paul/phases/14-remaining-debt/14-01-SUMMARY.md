---
phase: 14-remaining-debt
plan: 01
subsystem: settings
tags: [settings, listener, snapshot, font, repaint, multi-instance]

requires:
  - phase: 13-multi-instance
    provides: "Settings::Listener, Chassis::applySettings, the two-instance tests"
provides:
  - "Settings::notify: the ONE writer of the process-global font, before any listener runs"
  - "settingsChanged (const Snapshot&): listeners get what was written; one read per change"
  - "Settings::published: notify whenever the file differs from what the process shows (set, seedSnapshot)"
  - "a set() inside settingsChanged is refused and asserts"
  - "the editor's whole-window repaint, proven by region"
affects: [14-02..14-04 (no settings work left)]

key-files:
  modified: [src/Settings.h, src/Settings.cpp, src/SettingsMenu.h, src/SettingsMenu.cpp, src/Chassis.h, src/Chassis.cpp, src/PluginEditor.cpp, src/Typography.h, src/Typography.cpp, tests/StateRoundTripTest.cpp, tests/UiTest.cpp]

key-decisions:
  - "The store owns the global font; instances only apply their own LookAndFeel"
  - "The store remembers what it last published, and reconciles against it (/code-review)"
  - "A nested set() is refused, not deferred: a write mid-notification would split file and screen (/code-review)"
  - "Closed as won't-do at Phase 14 planning (user): the pulse timer fold; lookAndFeelChanged as the metric channel"

duration: ~1 session
completed: 2026-10-03
description: "The settings store owns the process-global font and hands every listener the snapshot it wrote; it remembers what the process shows and reconciles against it, so another process's font or theme reaches every open editor"
type: Summary
about: "Forró Box"
---

# Phase 14 Plan 01: The store owns the font

**There is now one writer of the process-global display font: the store.** It sets the font, then
hands every listener the snapshot it just wrote.
- Instances apply only their own LookAndFeel.
- Nobody re-announces.
- One change costs one file read however many editors are open.

The store also remembers what it last PUBLISHED. When the file says something the process is not
showing, a `set` or a seed puts it on every screen. That covers another process's font and also its
theme.

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: the store owns the family | **Pass** | `setMonoFamily` has one `src/` caller, `Settings::notify`. `notifyListeners` is gone. Every 13-02 two-instance check passes |
| AC-2: snapshot, no re-reads | **Pass** | The snapshot handed over equals the disk. +1 read per change with two rigs. `seedSnapshot` +1, and it notifies only when the file differs from what was published |
| AC-3: the editor itself repaints | **Pass** | A letterboxed editor with a recording `CachedComponentImage`: the repainted region covers the whole editor. With `repaint()` on the chassis only, it FAILS |
| AC-4: green everywhere | **Pass** | Gate 3/3; 5120/5120 on GCC 13, Clang 18 and MSVC 2022 (Debug suite 5123, 0 unexpected, 3 expected); Windows pluginval PASS |

**The editor-repaint test could not fail at first.** JUCE forwards a child's repaint to its parent,
so the editor's recorder fired with a chassis-only repaint too. Mutation P4 survived. The test now
measures the REGION on a letterboxed editor; the letterbox lies outside the chassis's bounds. That
also corrected the comment, which claimed the chassis "does not repaint its parent".

**Mutations, 7 rejected:**
- `notify` not setting the family;
- the seed not notifying;
- a listener re-reading (3 reads, not 1);
- a chassis-only repaint;
- an unchanged write not publishing;
- the seed checking only the font;
- a nested `set` allowed.

## /code-review: 9 findings, all handled

1. **A regression.** A `set` from inside a notification wrote the file, but its nested round was
   dropped, so neither the font nor the screen followed. A nested `set` is now REFUSED and asserts,
   so file and screen cannot split. Deferring it was the alternative; a listener that always writes
   would then loop.
2. **The seed during a notification, or a foreign snapshot.** `seedSnapshot` asserts it is not
   inside a notification. `applySettings` is documented as taking the store's snapshots.
3. **The new editor's chassis is applied twice on a notifying seed**, because it is already a
   listener. Kept and documented: the second pass finds the family laid out and only repaints.
4. **The test baseline used `snapshot()`**, so it drew in a font left by an earlier test. It now
   seeds, as the editor does.
5. **Re-picking a ticked font another process wrote did nothing.** `set` now publishes whenever the
   file differs from `published`, even when nothing is written.
6. **A seed told the others only about a FONT difference.** Now any difference, so two editors never
   disagree on the theme.
7. **The listener doc said "after every set".** Rewritten. `seedSnapshot` asserts the message thread
   on every path.
8. **Four stale comments** named `applyTo` as the font's writer. Fixed.
9. **Test hygiene.**
   - The counting listener read the file. It now records the snapshot, and the comparison happens
     after.
   - The test's cleanup restored the font around the store. It now restores it THROUGH the store.

## Closed as won't-do (Phase 14 planning, user)
- **The pulse's 30 Hz timer stays independent of the sway's.** The cost is one timer while
  CACHAÇA ≥ 88%.
- **The `resized()` walk stays, over `lookAndFeelChanged`.** `sendLookAndFeelChange` makes JUCE's
  `TextEditor` re-apply its fonts and colours.

## Deviations

| Type | Count | Notes |
|------|-------|-------|
| Scope addition | 3 | `published` reconciliation, the refused nested `set`, the seed noticing any difference (review) |
| Test strengthened | 1 | The editor repaint is measured by region (a surviving mutation) |

## Next
**14-02:** one owner for the always-on-top overlays' z-order.

---
*Phase: 14-remaining-debt, Plan: 01 · Completed: 2026-10-03*
