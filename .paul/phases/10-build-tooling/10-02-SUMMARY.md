---
phase: 10-build-tooling
plan: 02
subsystem: infra
tags: [cmake, python, audit-hook, cross-check-gates, configure-depends]

requires:
  - phase: 06-side-panel
    provides: "verify-geometry's scrape + floor — the partial answer this replaces"
provides:
  - "scripts/gate_inputs.py — one declaration per gate, --list-inputs, run-time enforcement"
  - "forrobox_add_verify_target deriving DEPENDS from the script; no input path in CMakeLists.txt"
  - "Three live undeclared inputs closed (verify-theme, verify-midi, verify-charset)"
affects: [10-03 verify-geometry scope resolution — same script; any future gate]

tech-stack:
  added: []
  patterns:
    - "A gate's inputs are declared in the gate and enforced by it; the build reads the declaration"
    - "sys.addaudithook: `open` for reads, `exec` for importlib-run modules"

key-files:
  created: [scripts/gate_inputs.py]
  modified: [CMakeLists.txt, scripts/verify-theme.py, scripts/verify-profiles.py,
             scripts/verify-charset.py, scripts/verify-geometry.py, scripts/verify-midi.py,
             scripts/build-profiles.py]

key-decisions:
  - "Declared + enforced (user, at planning) over the recorded regex scrape"
  - "Node's reads are declared as unobservable and exempt — the one place enforcement cannot reach"

patterns-established:
  - "A new gate calls gate_inputs.declare() and ends with sys.exit(gate_inputs.run(main))"

duration: ~1h45m
started: 2026-09-25T11:15:00-03:00
completed: 2026-09-25T13:10:00-03:00
description: "Each gate declares its inputs once, CMake depends on exactly that, and a gate that reads an undeclared file fails"
type: Summary
about: "Forró Box"
---

# Phase 10 Plan 02: Gate inputs declared once, and enforced — Summary

**Every cross-check gate now lists what it reads in one place, inside its own script. CMake takes
its dependencies from that list, and a gate that opens any repository file it didn't declare
fails, naming the file. The three live undeclared inputs measured at planning are closed by
declaration.**

## Performance

| Metric | Value |
|--------|-------|
| Duration | ~1h45m |
| Tasks | 3 of 3 |
| Files | 1 created, 7 modified; `CMakeLists.txt` net −26 lines across 265 changed |

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: one declaration per gate | **Pass** | All six declare through `gate_inputs.declare`. `--list-inputs` covers every read measured at planning; the only extras are imports the old probe couldn't see, the Node exemption, and `verify-profiles`' full set for its importers. No input path remains in any `forrobox_add_verify_target` call |
| AC-2: CMake depends on exactly the declaration | **Pass** | `ninja -t query`: EffectOverlay.cpp → verify-theme, MixBus.h and Profiles.h → verify-midi, a `.ttf` → verify-charset, all **0 → 1** against a worktree of the previous commit. Two already-declared controls 1 → 1. Real rebuilds after touching each ran exactly its gates. A new `tests/Scratch.h` re-ran CMake unprompted. Editing a declaration re-configured and added the input; reverting removed it |
| AC-3: an undeclared read fails | **Pass** | Mutation: `verify-theme` reading `src/Knob.h` → exit 1, and the build FAILED on `verify-theme.stamp` with `UNDECLARED INPUT: src/Knob.h`. Reverted. Module self-tests: undeclared read → 1, both declared → 0, failing gate keeps code 7, importlib-loaded module undeclared → 1 |
| AC-4: nothing else changes | **Pass** | 4974/4974 on GCC 13, Clang 18, MSVC 2022; zero warnings from our sources; every gate's OK line identical to before. On Windows the MSVC projects carry `Forró Box (standalone).html` intact, plus the three new inputs |

## Accomplishments

- **The class is closed, not the instances.** `gate_inputs.run(main)` fails a passing gate that
  read anything undeclared. Reads are seen by an audit hook installed when the module is imported,
  with `open` events for files and `exec` events for modules run through importlib, plus a
  `sys.modules` walk. A seventh enrolment bug can't pass silently.
- **CMake holds no copy.** `forrobox_add_verify_target` runs `--list-inputs` at configure time:
  `file` lines become DEPENDS, `glob` lines become `CONFIGURE_DEPENDS` globs, and declared `.py`
  files become configure dependencies. The hand lists, `verify-geometry`'s regex scrape and its
  16-header floor are deleted, along with about 150 lines of enrolment history now kept in git.
- **Closed by declaration:** `verify-theme` ← `src/EffectOverlay.cpp`; `verify-midi` ← `MixBus.h`,
  `Profiles.h` via `verify-profiles.INPUTS`; `verify-charset` ← `assets/fonts/*.ttf`.

## Task Commits

| Task | Commit | Type | Description |
|------|--------|------|-------------|
| Tasks 1–3 | `47dae6a` | feat | gate_inputs.py, six declarations, CMake from `--list-inputs` |

## Decisions Made

| Decision | Rationale | Impact |
|----------|-----------|--------|
| Declared + enforced, over the scrape (user, planning) | A scrape of `verify-midi.py` can't see what its import reads, and nothing detects a miss | Enforcement at run time; the build can't disagree with the script |
| Node's reads are `unobservable` | A child process's opens are invisible from Python | `verify-midi.js` and `audio.js` are declared (the build depends on them) and exempt, with the reason at the call site |
| `run(main)` instead of an `atexit` exit | `atexit` can't see the gate's exit code, so it couldn't keep a failure's own code | A failing gate keeps its code; only a PASS turns into 1 |

## Deviations from Plan

| Type | Count | Impact |
|------|-------|--------|
| Found in own work, fixed | 1 | importlib detection |
| Placement changed | 2 | declare above `__main__`; hook at import |
| Method substituted | 2 | exit via `run()`; "before" shown by graph query, not rebuild |
| Over-declaration | 1 | verify-midi now also depends on `data.js` and `Profiles.cpp` |

1. **The plan's importlib mechanism was wrong, and its self-test caught it.** The plan said walk
   `sys.modules`. `module_from_spec` + `exec_module` never registers the module there, so the
   must-reject case passed with exit 0. It's fixed with the `exec` audit event, whose code object
   carries `co_filename`, and the case now returns 1.
2. **`declare()` sits just above each `__main__` guard, not near the top**, and the audit hook is
   installed at import, not in `declare`. `build-profiles` loads `verify-profiles` at module level,
   which reads `MixBus.h` while loading. With the hook in `declare`, those reads would escape. The
   importers also need `_vp.INPUTS` to exist first.
3. **Exit via `sys.exit(gate_inputs.run(main))`**, not `os._exit` from `atexit`, for the reason in
   Decisions.
4. **"Before" was shown with `ninja -t query` in a worktree of the previous commit**, not by
   rebuilding that tree. A dry run (`ninja -n`) can't show re-runs in either tree: with
   `CONFIGURE_DEPENDS` globs, which `verify-charset` already had, Ninja always marks the glob check
   dirty and a dry run can't execute it, so it stops at "Re-running CMake". Real no-op builds do
   no work, so there's no regression. The "after" side was proven with real builds.
5. **`verify-midi` over-declares** `data.js` and `Profiles.cpp` by reusing the whole of
   `verify-profiles.INPUTS`. An extra dependency only over-triggers; copying a subset would be the
   second list this plan exists to remove.

## Issues Encountered

| Issue | Resolution |
|-------|------------|
| Dry-run touch checks printed nothing | Not accepted as a pass; diagnosed as `ninja -n` stopping at the always-dirty glob check; proved with real builds instead |
| Restoring a script re-configured the tree | Expected: declared `.py` files are configure dependencies now. Settled with one build |

## Skill Audit

| Skill | Status | Notes |
|-------|--------|-------|
| /simplify | pending | At Phase 10's close, after 10-03 |
| /code-review | not triggered | No processor, voice, clock or state code |

## Next Phase Readiness

**Ready:**
- 10-03 (`verify-geometry` scope resolution) works in a script whose inputs are now declared and
  enforced. Adding a header to `GEOMETRY_HEADERS` is automatically a dependency
- Any future gate needs two lines to be correct by construction

**Concerns:**
- Node's reads are trusted, not verified. If `verify-midi.js` ever reads a new file, only review
  will notice, and the comment at the declaration says so

**Blockers:** None

---
*Phase: 10-build-tooling, Plan: 02*
*Completed: 2026-09-25*
