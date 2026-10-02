---
phase: 13-multi-instance
plan: 01
subsystem: settings
tags: [settings, PropertiesFile, Timer, XmlDocument, InterProcessLock, persistence]

requires:
  - phase: 08-polish
    provides: "08-02's global Settings store and its file format"
  - phase: 12-settings-restructure
    provides: "SettingsMenu::build and settings::applyTo, the two multi-value readers"
provides:
  - "Settings as plain XML file I/O on PropertiesFile's format: no Timer, no thread"
  - "Settings::Snapshot, the only typed-reader surface; build and applyTo read once"
  - "set() -> bool, under an InterProcessLock; an unreadable file is copied aside, never overwritten blind"
affects: [13-02 (the broadcast hooks into set(), and its listeners re-read one snapshot)]

key-files:
  modified: [src/Settings.h, src/Settings.cpp, src/SettingsMenu.cpp, src/PluginProcessor.cpp, tests/StateRoundTripTest.cpp, tests/UiTest.cpp]

key-decisions:
  - "XmlDocument file I/O over a SharedResourcePointer store (user, Phase 13 planning): keeps re-read-before-write"
  - "A damaged-but-existing file is set aside as <name>.damaged before a write; the write is refused if that copy fails (/code-review)"
  - "Writes serialised across processes by a bounded InterProcessLock (/code-review)"

duration: ~1 session
completed: 2026-10-02
description: "The settings store reads and writes PropertiesFile's own XML directly, so no access creates a Timer or a thread; one read per menu and per apply; an unreadable file is never overwritten blind"
type: Summary
about: "Forró Box"
---

# Phase 13 Plan 01: The store without PropertiesFile

**Settings access no longer starts or joins the timer thread.** That used to happen about 14 times
per gear click, and also in the processor's constructor during a headless scan.

The store now parses the file with `juce::XmlDocument` and writes it atomically through
`XmlElement::writeTo`, on the same `<PROPERTIES><VALUE name val/>` format. Every existing user's
file still loads. `Settings::Snapshot` gives the menu and `applyTo` one read each, down from 5 and 4.

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: no PropertiesFile, no Timer | **Pass** | Only `PropertiesFile::Options` remains, used to resolve the path. The header comment is rewritten |
| AC-2: JUCE's format, both ways | **Pass** | A file a real `PropertiesFile` wrote reads back correctly, and a real `PropertiesFile` reads what the store writes. Unknown keys survive a `set`, including an XML-valued one |
| AC-3: damage reads as defaults | **Pass** | Empty, cut off inside the tag, truncated mid-body, wrong root, non-integer value, missing: all defaults; `set` repairs. The existing hostile-value checks pass unchanged |
| AC-4: one read per menu / apply | **Pass** | Counted with `readCountForTest`: 1 for `build`, 1 for `applyTo`. Ticks and applied values are unchanged |
| AC-5: green everywhere | **Pass** | Gate 3/3; 5081/5081 on GCC 13, Clang 18 and MSVC 2022 (Debug suite 5083, 0 unexpected); Windows pluginval PASS |

**Mutations, all rejected:**
- the root-tag check removed;
- unknown keys dropped on write;
- `applyTo` reading per field (expected 1 read, saw 2);
- the attribute `val` → `value`;
- no set-aside of a damaged file.

## /code-review: 10 findings, 9 fixed, 1 kept

1. **A data-loss bug.** A file that EXISTS but does not parse (truncated, a wrong root tag, or
   locked while being read) read as empty. The next click then rewrote it holding one key, which
   destroyed every other preference.
   - Now `read()` reports `damaged`, and `set` copies the file aside as `<name>.damaged` before
     replacing it. If the copy also fails, `set` refuses to write.
   - **Measured:** JUCE's parser accepts `<PROPERTIES` cut off inside its opening tag as an empty
     element. That loses nothing, so it is not treated as damaged. A file truncated mid-body is an
     "unmatched tags" failure, and is treated as damaged.
2. **Two processes writing at once** could still revert each other's change. The read-modify-write
   now runs under `juce::InterProcessLock ("ForroBoxSettings")`, bounded at 1 s because it runs on
   the message thread. Its lock file goes in `/var/tmp` on Linux; Windows uses a named mutex.
3. **A failed write was silent.** `set` returns `bool`. The menu needs no special case, because it
   re-reads and so shows the value actually in force.
4. `Snapshot` storage is sized from `settings::infos.size()`.
5. `Snapshot`'s constructor is private, so no snapshot can hold zeros (below the accent floor).
6. The six typed readers that only forwarded to `snapshot()` are gone; `Snapshot` is the one
   surface. The processor and the tests call `snapshot().…`.
7. **Kept:** the test-only read counter. It costs one relaxed atomic increment per read, and the
   suite is single-threaded.
8. The default path is resolved once (it is a shell query on Windows).
9. `PropertiesFile::save`'s guards are restored: no file, or a directory where the file should be,
   fails up front.
10. The test locals named `juce` are renamed `reference`.

## Deviations

| Type | Count | Notes |
|------|-------|-------|
| Scope addition | 3 | The set-aside, the cross-process lock and `set -> bool` (review findings 1–3) |
| API narrowed | 1 | The typed readers live on `Snapshot` only; one processor line changed to `snapshot().defaultStepChoiceIndex()` |
| Test file | 1 | The read-count checks live in `UiTest`, because `applyTo` needs a LookAndFeel |

## Deferred
- **On Windows, a write against a file another process holds open** can stall the message thread
  for about 0.5 s inside JUCE's `TemporaryFile` retry before failing. That is rare, it is JUCE's
  retry, and it is now reported through `set`'s result.

## Next
**13-02:** a synchronous listener list on `set`, plus one `Chassis` entry point (apply, re-layout,
repaint) that every open instance runs.

---
*Phase: 13-multi-instance, Plan: 01 · Completed: 2026-10-02*
