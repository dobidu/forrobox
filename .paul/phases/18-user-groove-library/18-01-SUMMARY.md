---
phase: 18-user-groove-library
plan: 01
subsystem: state
tags: [user-grooves, library, file-format, processor, state]

requires:
  - phase: 08-settings (v0.1) / 13-multi-instance (v0.2)
    provides: "Settings' per-platform config directory, ScopedTestFile's nesting rule"
provides:
  - "src/UserGrooves.{h,cpp}: the .forrogroove format (strict read, atomic write) and UserGrooveLibrary (save/rename/overwrite/remove, sorted bank, ScopedTestFolder)"
  - "ids::userProfile = \"user\", static_assert'd against profileInfos"
  - "processor: userGrooves, saveUserGroove, overwriteUserGroove, loadUserGroove, renameUserGroove, deleteUserGroove; activeGrooveName/cycleGroove resolve the USER bank"
  - "recordGrooveLoad: applyGroove's slot-reset + pristine tail, shared with applyUserGroove"
affects: [18-02 (the UI over this API), 19 (export/import of this format)]

key-files:
  created: [src/UserGrooves.h, src/UserGrooves.cpp, tests/UserGroovesTest.cpp]
  modified: [src/ParameterIDs.h, src/Profiles.h, src/Profiles.cpp, src/PluginProcessor.h, src/PluginProcessor.cpp, CMakeLists.txt, tests/TestMain.cpp, tests/TestSuites.h]

key-decisions:
  - "Grid + feel only, like a built-in; one <uuid>.forrogroove per groove beside the settings file; unlimited (user, Phase 18 planning)"
  - "activeGroove's meanings resolved by SCOPE: under activeProfile \"user\" it is a library id; regional meanings unchanged"
  - "A user groove id this machine lacks shows NO name (a UUID tells the reader nothing); its lanes still play"
  - "A save is pristine only when every channel is on PAT 01 and slots 2-8 are empty; saving never wipes a slot"
  - "A capture at a narrow step window tiles what is heard across all 32 slots"

duration: ~1 session
completed: 2026-10-04
description: "The user groove library's model: <uuid>.forrogroove files read strictly and written atomically, a shared sorted library with save/rename/overwrite/delete, and a reserved USER profile the processor plays, names and cycles — green on all four CI jobs, no UI yet"
type: Summary
about: "Forró Box"
---

# Phase 18 Plan 01: The user groove library's model

**The processor can keep, list, load and cycle the user's own grooves.** A groove is a name, the
feel and the eight lanes at the State's own resolution (32 slots × 0–127), stored as
`<uuid>.forrogroove` in `grooves/` beside the settings file. Under the reserved `user` profile the
header's name and the cycler resolve that bank; the regional banks behave exactly as before.

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: round trip; only valid files load | **Pass** | Exact at 8 × 32 × 0–127 with a non-ASCII name. 13 malformed variants each refused with a named reason; in a folder each is reported, a non-XML file too, and the valid neighbour loads. Feel clamps |
| AC-2: save, rename, overwrite, delete | **Pass** | Sorted case-insensitively; rename keeps id and file; overwrite keeps id and name; refusals (empty, blank, 25 chars, newline, unknown id) write nothing; a second library sees every change after rescan |
| AC-3: USER plays, names, cycles | **Pass** | State `user/<id>`, lanes + bpm/swing/cachaça loaded, slots PAT 01, timbre and mutes untouched, cycler clamps both ends in name order; every pre-existing test unchanged |
| AC-4: survives the host; absence costs nothing | **Pass** | get/setStateInformation round-trips identity, pristine and lanes. A missing id: lanes play, no name, an arrow loads the first groove. Deleting the active groove marks dirty; an empty bank leaves the arrows inert |
| AC-5: nothing reaches the audio thread | **Pass** | No new code reachable from processBlock; the allocation/lock checks pass unchanged |

**Verification:** Linux GCC + Clang 5267/5267; Debug 5271/5271 (0 unexpected assertions); local
gate 3/3; WSL Windows 5267/5267 + pluginval; CI run 37220552527 on `2011824`: Linux gate, portable,
Windows, macOS all green. The developer's real `~/.config/Forro Box/` was never created by a run.

**Mutation proofs (8):**
- duplicate-lane check removed → 4 checks fail;
- the sort removed → 2 fail;
- the 0–127 bound widened → 4 fail;
- `applyUserGroove` without `recordGrooveLoad` → 2 fail;
- the dirty-on-delete removed → 1 fails;
- the pristine-only-if-true rule forced false → 1 fails;
- the narrow-window tiling removed → 2 fail;
- the clamp moved before the empty check → 2 unexpected assertions in the Debug tree.

## /code-review (required): four findings

**Fixed:**
1. **Empty bank:** `jlimit (0, -1, …)` asserted before the `empty()` check in `cycleGroove`'s USER branch.
2. **A stale second bar:** at 16 steps, slots 16–31 can hold an older groove (narrowing tiles
   nothing), and a raw capture saved a bar the user never heard. The capture now tiles the window.
3. **Temp files matched the scan:** `XmlElement::writeTo`'s temporary is `<id>_temp<hex>.forrogroove`,
   so a mid-write rescan or a crash left a "refused groove" the user never made. The write now goes
   through an explicitly named `.partial` temporary.

**Deferred:**
4. **Other in-process instances are not told** when one overwrites or deletes a groove they are
   playing: they keep claiming it, pristine. The same class as another process's writes (seen on the
   next rescan). The notification path belongs with 18-02's UI, which must refresh anyway.

## Deviations

| Type | Count | Notes |
|------|-------|-------|
| Semantics | 1 | A save is pristine only if no other slot holds patterns (the plan said "pristine"); wiping slots to make it true would destroy user content |
| Semantics (review) | 1 | Narrow-window capture tiles what is heard |
| Structure | 1 | `UserGrooveLibrary::shared()` is a static instance like `Settings::shared()`, not a `SharedResourcePointer` |

## Next
**18-02:** on screen — a USER entry in the side panel, save / rename / delete in the gear menu, the
name prompt, and the in-process refresh deferred above. /simplify closes Phase 18.

---
*Phase: 18-user-groove-library, Plan: 01 · Completed: 2026-10-04*
