---
phase: 15-portable-builds
plan: 01
subsystem: build
tags: [msvc, static-runtime, windows, packaging, vcruntime]

requires:
  - phase: 10-build-tooling
    provides: "scripts/build-windows.sh judged by exit code, with the exit probe"
provides:
  - "the static MSVC runtime for every target (CMAKE_MSVC_RUNTIME_LIBRARY)"
  - "build-windows.sh: a PE-import check that fails if a shipped binary imports a runtime DLL"
affects: [16 (CI builds this configuration), the v0.3 release notes and INSTALL.txt]

key-files:
  modified: [CMakeLists.txt, scripts/build-windows.sh, README.md]

key-decisions:
  - "Checked from the PE import table of the bytes that ship, not from the build setting"
  - "README states both cases until 0.3 ships: 0.3+ needs nothing, the 0.2 packages need the redistributable"

duration: ~1 session
completed: 2026-10-03
description: "The Windows build links the MSVC runtime statically — the VST3 and the standalone need no VC++ redistributable — and the build refuses to finish if a shipped binary still imports a runtime DLL"
type: Summary
about: "Forró Box"
---

# Phase 15 Plan 01: No runtime to install on Windows

**The Windows VST3 and standalone no longer need the VC++ 2015–2022 Redistributable.**
`CMAKE_MSVC_RUNTIME_LIBRARY` puts every target on `/MT`, with `/MTd` in Debug. The build reads the
PE import table of what it ships, and fails if a runtime DLL is still there.

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: no runtime imports | **Pass** | The VST3 module, `ForroBox.exe` and `ForroBoxTests.exe` are clean. The standalone imports only OS DLLs (KERNEL32, USER32, d2d1 and so on); `MSVCP140.dll` is gone. The generated vcxprojs read `MultiThreaded` / `MultiThreadedDebug` |
| AC-2: the check can fail | **Pass** | With the setting removed, the build FAILS, naming `MSVCP140.dll`, `VCRUNTIME140.dll` and `VCRUNTIME140_1.dll` in both the VST3 and the standalone |
| AC-3: nothing else changes | **Pass** | Windows 5143/5143, the exit probe clean, Windows pluginval PASS. Linux gate 3/3; Clang 5143/5143 |

## Deviations

| Type | Count | Notes |
|------|-------|-------|
| README wording | 1 | Not a plain deletion. The download section still links the v0.2 packages, which DO need the redistributable, so the text states both cases until 0.3 ships |
| Cache | 0 | The setting applied to the existing Windows build dir; no cache clear was needed |

## Skill audit
`/code-review` was not required: build configuration only.

## Next
**15-02:** the Linux portable build in an Ubuntu 22.04 container (glibc 2.35), GCC 13 with
libstdc++ and libgcc linked statically, proven inside a clean 22.04 container.

---
*Phase: 15-portable-builds, Plan: 01 · Completed: 2026-10-03*
