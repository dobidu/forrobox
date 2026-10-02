---
phase: 13-multi-instance
plan: 02
subsystem: settings
tags: [settings, listener, multi-instance, relayout, font, Segmented]

requires:
  - phase: 13-multi-instance
    plan: 01
    provides: "set() -> bool, Snapshot, the store with no Timer"
provides:
  - "Settings::Listener: notified after a write that landed and changed something; message-thread asserted; re-entrant rounds dropped"
  - "Chassis::applySettings: THE entry point (applyTo, a tree re-layout when the family differs, a top-level repaint)"
  - "a seed that moves the process-global font is announced to every other instance"
  - "Segmented re-measures in resized() when the family moved"
affects: [Phase 14 (deferred: one owner for the global font; lookAndFeelChanged as the metric channel)]

key-files:
  modified: [src/Settings.h, src/Settings.cpp, src/SettingsMenu.h, src/SettingsMenu.cpp, src/Chassis.h, src/Chassis.cpp, src/Segmented.h, src/Segmented.cpp, src/PluginEditor.cpp, src/Typography.h, tests/RigStart.h, tests/StateRoundTripTest.cpp, tests/UiTest.cpp]

key-decisions:
  - "Synchronous ListenerList over ChangeBroadcaster (user, Phase 13 planning)"
  - "Following a CHANGE is not SEEDING: Chassis listens, but never seeds from the store at construction"
  - "Re-layout keyed on the family each chassis was laid out under, not on whether its own call changed it"

duration: ~1 session
completed: 2026-10-02
description: "A settings change in one instance reaches every open instance in the process, immediately, and a font switch re-measures what each one draws"
type: Summary
about: "Forró Box"
---

# Phase 13 Plan 02: Every instance follows the store

**Two open instances now agree on every global setting.** `Settings::set` notifies its listeners.
Each `Chassis` is one, and it follows the store through a single entry point: `applySettings()`,
which applies, re-lays out after a font switch, and repaints. The STYLE control's segments move by
4 px on a switch to Space Mono, and the second instance moves with the first.

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: notifies exactly when something changed | **Pass** | 1 call per landed write. 0 on a failed write (a directory as the target). 0 when the value is already in force. 0 after removal. The listener reads the NEW value |
| AC-2: a change reaches the other instance | **Pass** | Theme, corner radius, accent and font each reach rig B through rig A's menu. A light rig is not made dark at construction. A destroyed chassis leaves the list |
| AC-3: a font switch re-lays out | **Pass** | B's STYLE segments end where its preferred width says, and its SidePanel regions equal a fresh layout. With the re-layout removed, both FAIL (137 vs 133 px), so no layout counter was needed |
| AC-4: one entry point | **Pass** | In `src/`, `settings::applyTo` is called only from `Chassis::applySettings`. The editor seeds through it |
| AC-5: green everywhere | **Pass** | Gate 3/3; 5109/5109 on GCC 13, Clang 18 and MSVC 2022 (Debug suite 5111, 0 unexpected, including the new message-thread asserts); Windows pluginval PASS |

**Mutations, all rejected:**
- no notification;
- no re-layout;
- no unregister in the destructor (segfault);
- the seed not announced;
- no re-entrancy guard (51 rounds);
- a failed write reported as `changed`;
- the font gate keyed per call;
- the no-op write check removed;
- the damaged-file exemption removed;
- the `Segmented` family gate disabled.

## /code-review: 10 findings, all handled

1. **The seed moved the global font silently.** An editor opening after another process changed the
   font switched the family for the whole process, but already-open editors kept their old metrics.
   `applyTo` now returns whether the family changed, and the instance that changed it calls
   `notifyListeners()`.
2. **The re-layout ran on every change and walked a live child list.** It now runs only when the
   family differs from the one that chassis was laid out under. It walks a COPY of each child list.
   - Measured in the two-instance test: keying it on "did MY call change the family" re-laid out
     only the first instance in a round. The fix is the per-chassis `laidOutFamily`.
3. **Unchanged values wrote and notified.** Re-picking the ticked item is now a no-op. It is decided
   on the read `set` already does (see the /simplify note below).
4. **The message-thread contract was only a comment.** `JUCE_ASSERT_MESSAGE_THREAD` now guards add,
   remove and notify.
5. **A listener calling `set` recursed.** A nested round is now dropped (`notifying` guard).
6. **A failed write was reported as `changed`.** `SettingsMenu::apply` now returns `dismissed`.
   - This is a deviation inside 12-01's `apply`. Behaviour only; the ids and `Result` are
     unchanged.
7. **The editor's letterbox and the value tooltip missed theme changes.** The repaint now goes to
   the TOP-LEVEL component.
8. **A doc comment was misplaced.** Moved back.
9. **A `std::function` self-recursion.** Now a free `relayoutTree`.
10. **The segment width rule was written twice.** One `segmentWidth`.

## /simplify at Phase 13 close (required): four agents

**Applied:**
- **One read per `set`, not two:**
  - `writeUnderLock` returns `written` / `unchanged` / `failed`, and decides "already in force" on
    the contents it read anyway;
  - a DAMAGED file is exempt, so re-picking the default still repairs it. That case had no test,
    so one was added and mutation-proved.
- **`juce::ScopeGuard`** replaces the hand-written lock exit and the test's family restore.
- **One resolver for the default file** (`realFileLocation`, cached), shared by `target()`.
- **Dead code removed:** `readValues` and `#include <memory>`.
- **`Segmented` rebuilds its spans only when the family moved**, not on every resize of a window
  drag.
- **`repaintAll` is private.** The one test that called it now goes through `applySettings`.
- **Two stale doc comments fixed:** `applyTo`, and `Typography::setMonoFamily`'s return value,
  which is now used.
- **Test helpers in `RigStart.h`:**
  - `ScopedUnwritableSettingsTarget`, with ordered cleanup that runs even on an early return;
  - `referencePropertiesFile`, which replaces 3 copies;
  - `settingOf (info)`, which replaces 8 pointer subtractions;
  - one snapshot per check block.

**Deferred to Phase 14 (in STATE.md):**
- **One owner for the process-global display font.** Each chassis writes it through `applyTo` and
  re-announces it. Typography or the store could own it and notify, which would retire
  `notifyListeners` as public API.
- **`lookAndFeelChanged()` / `sendLookAndFeelChange()`** as the channel for metric caches, instead
  of a hand-written `resized()` walk. It needs care: `TextEditor::lookAndFeelChanged` re-applies
  fonts and colours.
- **A snapshot handed to listeners**, so N instances parse the file once rather than N times.

**Skipped:**
- **`juce::PropertySet::createXml` / `restoreFromXml`** in place of the copied format. `restoreFromXml`
  reads only the `val` attribute, so a foreign key stored as an XML child would be lost on the next
  rewrite, which the 13-01 test rejects.
- **The test-only counters**: already recorded at 13-01.

## Deviations

| Type | Count | Notes |
|------|-------|-------|
| Scope addition | 4 | The seed announcement, the re-entrancy guard, the no-op write, and `apply`'s failed-write result (review) |
| Test gap | 1 | No automated check that the editor's letterbox repaints on another instance's theme change (top-level repaint, by inspection) |

## Next
**Phase 14 — Remaining debt.**

---
*Phase: 13-multi-instance, Plan: 02 · Completed: 2026-10-02*
