---
phase: 01-plugin-foundation
plan: 01
subsystem: infra
tags: [juce, juce8, vst3, cmake, fetchcontent, audio-plugin, standalone, cpp20]

requires: []
provides:
  - CMake project acquiring JUCE 8.0.12 (FetchContent pinned, JUCE_PATH local override)
  - VST3 + Standalone instrument targets for Linux
  - AudioProcessor skeleton with the project's audio-thread contract
  - AudioProcessorEditor holding the 1200×780 / 20:13 design geometry
  - Opt-in LTO switch (FORROBOX_LTO) separating dev builds from distribution
affects: [01-02-parameters-state, 01-03-windows-build, 02-sequencer-clock, 03-voices-mix-bus, 04-ui-shell]

tech-stack:
  added: [JUCE 8.0.12, juce_audio_utils, juce_dsp]
  patterns:
    - "processBlock does no allocation, no locking, no I/O — enforced by comment and reviewed"
    - "Cross-thread scalars are lock-free std::atomic, proven with static_assert"
    - "Non-ASCII strings are display-only; every technical identifier stays ASCII"
    - "Editor geometry expressed in design px against a fixed 1200×780 frame"

key-files:
  created:
    - CMakeLists.txt
    - src/PluginProcessor.h
    - src/PluginProcessor.cpp
    - src/PluginEditor.h
    - src/PluginEditor.cpp
    - .gitignore
  modified: []

key-decisions:
  - "BUNDLE_ID set explicitly — JUCE derives it from the accented COMPANY_NAME otherwise"
  - "LTO gated behind FORROBOX_LTO rather than tied to the Release default"
  - "No shadow processor reference in the editor; cast getAudioProcessor() at point of use"
  - "A non-empty but invalid JUCE_PATH is FATAL_ERROR, never a silent network clone"

patterns-established:
  - "Audio-thread contract: no allocation, no locks, no I/O in processBlock"
  - "prepareToPlay is the only place allocation is permitted"
  - "releaseResources retains cached rate/block size so callers never receive 0.0"

duration: ~50min
started: 2026-09-06T23:15:00Z
completed: 2026-09-06T00:05:00Z
description: "JUCE 8 CMake project building Forró Box as a Linux VST3 + Standalone instrument, with a silent audio-thread-safe processor and an aspect-locked 1200×780 editor."
type: Summary
about: "Forró Box"
---

# Phase 1 Plan 01: Plugin Foundation Summary

**JUCE 8 CMake project building Forró Box as a Linux VST3 + Standalone instrument, with a silent
audio-thread-safe processor and an aspect-locked 1200×780 editor.**

## Performance

| Metric | Value |
|--------|-------|
| Duration | ~50 min |
| Tasks | 3 completed |
| Files created | 6 (307 → 313 lines) |
| Clean rebuild | 39 s (was 69 s before the LTO gate) |
| Artifacts | `ForroBox.vst3` 4.7 MB · `Standalone/ForroBox` 5.6 MB |

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: Configures with JUCE resolved locally | Pass | Logs `JUCE: using local /home/bidu/JUCE`; no `_deps/`, nothing fetched. Unset `JUCE_PATH` logs `JUCE: fetching 8.0.12` |
| AC-2: Correct format and capabilities | Pass | `IsSynth=1`, `WantsMidiInput=1`, `ProducesMidiOutput=1`, `IsMidiEffect=0`, `EditorRequiresKeyboardFocus=1`, codes `0x46726278` = `Frbx` |
| AC-3: Both targets build and load | Pass — AC text corrected | VST3 bundle + Standalone build; `ldd` clean (0 missing) on both. AC named `Contents/moduleinfo.json`; JUCE correctly writes `Contents/Resources/moduleinfo.json` per the VST3 spec |
| AC-4: Processor silent and audio-thread safe | Pass | `processBlock` body is `ScopedNoDenormals`, `buffer.clear()`, `midi.clear()`. No allocation, locks or I/O |
| AC-5: Editor holds design geometry | Pass (one clause unverified) | Window 1208×814 = 1200×780 content + resizable border 4×2 (JUCE calls `setTitleBarHeight(0)`); limits and fixed aspect set in code. Driven-resize not runtime-tested — `xdotool` absent, package installs out of bounds |
| AC-6: Standalone launches without crashing | Pass | Ran under WSLg, stayed alive, single ALSA `/dev/snd/seq` warning — graceful degradation confirmed |

## Accomplishments

- A VST3 instrument that configures, builds and loads on Linux, plus a Standalone target that makes
  Phase 3 voice auditioning possible without a DAW
- The project's audio-thread contract established and reviewed in the file every later phase copies
- Reproducible JUCE acquisition: pinned tag for a fresh clone, instant local override on this machine,
  hard failure on a mistyped path
- Clean under **both** GCC 13.3 and Clang 18.1 under JUCE's recommended warning flags

## Task Commits

Not applicable — the project is intentionally not a git repository (`01-01-PLAN.md` boundaries
forbid `git init`). **This blocks the phase-transition workflow, which mandates a git commit; resolve
before Phase 1 closes.**

## Files Created/Modified

| File | Change | Purpose |
|------|--------|---------|
| `CMakeLists.txt` | Created | JUCE 8.0.12 acquisition, plugin target, opt-in LTO |
| `src/PluginProcessor.h` | Created | Capability reporting, atomics for cross-thread state |
| `src/PluginProcessor.cpp` | Created | Silent `processBlock`, `prepareToPlay`, state stubs seamed for 01-02 |
| `src/PluginEditor.h` | Created | Design geometry constants (1200×780, 840×546, 2400×1560) |
| `src/PluginEditor.cpp` | Created | Aspect-locked construction, placeholder paint |
| `.gitignore` | Created | Build trees, FetchContent cache, JUCE generated sources |

## Decisions Made

| Decision | Rationale | Impact |
|----------|-----------|--------|
| Explicit `BUNDLE_ID "com.forrobox.ForroBox"` | JUCE derived `com.Forró Box.ForroBox` — space and non-ASCII in an identifier with a hard reverse-DNS constraint | Display name stays accented; identifier is valid |
| LTO behind `FORROBOX_LTO` (default OFF) | "Don't accidentally ship Debug" and "optimise the distributable" are separate concerns; conflating them taxed every dev build | Rebuild 69 s → 39 s; packaging adds `-DFORROBOX_LTO=ON` |
| No stored processor reference in the editor | `AudioProcessorEditor` already keeps one; a second reference was dead until Phase 4 and needed `[[maybe_unused]]` to stay quiet | Phase 4 casts `getAudioProcessor()` at point of use |
| Invalid `JUCE_PATH` → `FATAL_ERROR` | A non-empty value asserts a checkout exists; degrading to a clone buries a typo behind minutes of network, or an opaque git error when air-gapped | Fails in ~1 s with an actionable message |
| `using juce::AudioProcessor::processBlock;` over implementing the double overload | `supportsDoublePrecisionProcessing()` is false and the base double overload is `jassertfalse` — a well-behaved host never calls it | Warning resolved without an unused conversion path |

## Deviations from Plan

### Summary

| Type | Count | Impact |
|------|-------|--------|
| Plan spec errors corrected | 2 | Code was right, plan text wrong |
| Auto-fixed during qualify | 3 | Essential; no scope creep |
| Review fixes applied | 6 | 4 from `/code-review`, 2 from `/simplify` |
| Verification gaps | 1 | Environment limit, documented |

**Total impact:** No scope creep. Every deviation either corrected a plan inaccuracy or fixed a
defect the plan's own criteria required fixing.

### Plan spec errors

1. **AC-3 asserted the wrong moduleinfo path.** JUCE writes `Contents/Resources/moduleinfo.json`,
   which is correct per the VST3 spec. Confirmed independently by `/code-review`; the AC line would
   have failed as written.
2. **Task 1 required `JUCE_DISPLAY_SPLASH_SCREEN=0`.** Obsolete in JUCE 8 — defining it only emits a
   `#pragma message` on every build, so following the plan would have *added* a warning.

### Auto-fixed during qualify

1. **Bundle ID contamination** — found qualifying Task 1; JUCE warned about `com.Forró Box.ForroBox`.
   Fixed with an explicit ASCII `BUNDLE_ID`.
2. **Obsolete define** — found on first build; removed.
3. **Two warnings from own code** — `-Woverloaded-virtual` and `-Wshadow`, hidden by an earlier
   `tail -30`. Fixed with a `using` declaration and a rename.

### Review fixes

From `/code-review` (4 of 5 findings; the 5th is an open decision below):

1. **Cross-thread data race** — `currentSampleRate`/`currentBlockSize` were plain `double`/`int`
   while documented as written on the message thread and read on the audio thread. Now `std::atomic`
   with `static_assert(is_always_lock_free)`.
2. **`-Wunused-private-field`** — invisible under GCC, would have broken 01-03's MSVC build.
3. **`releaseResources()` zeroed the sample rate**, contradicting the getter's contract and setting
   Phase 2 up to divide by zero when a host closes its device with the editor open.
4. **Invalid `JUCE_PATH` cloned silently** — now `FATAL_ERROR`.

From `/simplify` (2 findings; reuse and simplification agents returned clean):

5. **Unconditional LTO** — gated behind `FORROBOX_LTO`.
6. **`[[maybe_unused]]` was a bandaid** — the member was dead code, so it was removed rather than
   silenced.

### Verification gap

AC-5's driven-resize clause is not runtime-verified: `xdotool` is absent and the plan's boundaries
forbid installing packages. Verified instead by construction plus exact chrome arithmetic.

## Issues Encountered

| Issue | Resolution |
|-------|------------|
| `JucePluginDefines.h` is generated at build time, not configure time | AC-2 checked after the build, via the target's own compile flags |
| First clang check reported "CLEAN" while actually aborting on a missing generated `JuceHeader.h` | Caught by inspecting the non-zero exit; redone with the header's include dir. Genuinely clean |
| Two AC-4 "violations" were false positives — my own comment text, and `lock` matching inside `processB`**lock** | Re-checked with comments stripped and word-boundary patterns |

## Skill Audit

| Expected | Invoked | Notes |
|----------|---------|-------|
| `/code-review` | ✓ | After Task 2 per plan; 5 findings, 4 fixed |
| `/simplify` | ✓ | During UNIFY; 4 agents, 2 findings, both fixed |
| `/graphify` | ○ | Waived — `PLANNING.md` was authored this session and already in context |
| `/impeccable` | — | Marked N/A in the plan; the editor is intentionally empty |

## Next Phase Readiness

**Ready:**
- Build system, plugin target and processor/editor scaffold in place for 01-02 to attach APVTS to
- `getStateInformation`/`setStateInformation` carry explicit `01-02` seam markers
- Audio-thread contract documented and reviewed; Phase 2's clock inherits it
- Design geometry constants in place for Phase 4's scale transform

**Concerns:**
- The accented plugin name is corrupted in VST3 class metadata (open decision below). Should be
  settled before 01-03 produces a Windows binary
- No git repository, so the phase-transition workflow's mandated commit cannot run
- AC-5's resize behaviour rests on JUCE's constrainer rather than a driven test

**Blockers:** None for 01-02.

**Open decision — plugin display name.** JUCE's `infoW.fromAscii()`
(`juce_VST3ModuleInfo.h:296`) sign-extends each `char`, so UTF-8 `ó` (`c3 b3`) becomes
`U+FFC3 U+FFB3` and hosts reading `moduleinfo.json` or `getClassInfoUnicode` show `Forrￃﾳ Box`.
Runtime `getName()` and the standalone title are correct. The altitude review judged the layers:
patching the built bundle is most fragile (couples to JUCE's internal binary layout, breaks on
version bumps); changing `PLUGIN_NAME` to ASCII is a permanent concession to an upstream bug; a
one-call-site patch in the local JUCE checkout, or tracking the upstream fix, is least fragile since
the defect lives entirely in JUCE's serialisation code.

---
*Built with PAUL Framework v1.4 · https://chrisai.cv/skool · https://youtube.com/@chris-ai-systems*
*Phase: 01-plugin-foundation, Plan: 01*
*Completed: 2026-09-06*
