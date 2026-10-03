---
phase: 15-portable-builds
plan: 02
subsystem: build
tags: [linux, glibc, docker, static-libstdc++, xvfb, packaging]

requires:
  - phase: 15-portable-builds
    plan: 01
    provides: "the shape of a platform build that checks what it ships"
provides:
  - "docker/linux-portable.Dockerfile: a `build` stage (22.04, GCC 13, pinned Node 20) and a compiler-free `runtime` stage"
  - "FORROBOX_PORTABLE_LINUX: static libstdc++/libgcc, INTERFACE on the shared-code target"
  - "scripts/build-linux-portable.sh: build, floor check, clean-22.04 run"
  - "scripts/check-glibc-floor.sh, scripts/check-pe-runtime.sh, scripts/headless-x.sh: reusable by Phase 16's CI"
affects: [16 (CI calls these scripts), the v0.3 release notes and INSTALL.txt]

key-files:
  created: [docker/linux-portable.Dockerfile, scripts/build-linux-portable.sh, scripts/check-glibc-floor.sh, scripts/check-pe-runtime.sh, scripts/headless-x.sh]
  modified: [CMakeLists.txt, scripts/build-windows.sh, README.md]

key-decisions:
  - "The floor is Ubuntu 22.04 / glibc 2.35 (user, Phase 15 planning)"
  - "Proven from the binaries (objdump -T) and on a clean 22.04, never trusted from the build setting"
  - "A headless X display gets the window-manager atoms JUCE expects, read from JUCE's own source"

duration: ~1 session
completed: 2026-10-03
description: "The Linux package needs only glibc 2.35 — built in an Ubuntu 22.04 container with libstdc++ static, and proven from the binaries' symbols and by the full suite on a clean 22.04; /simplify closes Phase 15"
type: Summary
about: "Forró Box"
---

# Phase 15 Plan 02: Linux on Ubuntu 22.04 and newer

**The Linux binaries now need only glibc 2.35.** They are built in an Ubuntu 22.04 container with
GCC 13, and libstdc++ and libgcc are linked statically. The build script proves the result two
ways:
- the binaries' own symbol versions;
- the whole suite, run on a clean 22.04 that has no compiler.

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: the floor, from the binaries | **Pass** | The VST3 `.so`, the standalone and the test binary are each at GLIBC 2.35, with no `GLIBCXX_`/`CXXABI_` dependency |
| AC-2: runs on a clean 22.04 | **Pass** | The `runtime` stage has no compiler (checked: 0 gcc/g++/cc). `ldd` is clean on the `.so` and the standalone; the suite passes 5143/5143 |
| AC-3: the checks can fail | **Pass** | `FORROBOX_PORTABLE_LINUX=OFF` → `GLIBCXX_3.4.32` FATAL on all three. A host-built `.so` → `GLIBC_2.38` FATAL |
| AC-4: host builds unchanged | **Pass** | GCC/Clang 5143/5143; gate 3/3; MSVC with the runtime-import check and Windows pluginval PASS |

## Found on the way: a headless display is not a desktop
The first clean-container run died mid-suite with an X error: `BadAtom` on `X_ChangeProperty`, atom 0.
- **Cause:** JUCE fetches the window-manager atoms (`WM_PROTOCOLS`, `_NET_WM_*`, `_MOTIF_WM_HINTS`)
  only if they already exist. On a desktop the window manager has created them; on a bare Xvfb a
  test that gives a component a real window passes atom 0.
- **`scripts/headless-x.sh`:** starts Xvfb with `-noreset`, so interned atoms survive the last
  client, and interns every window-manager atom name that JUCE's X11 source mentions. The names are
  read from the source itself: 33 of them, where a hand-written list had 20.

## /simplify at Phase 15 close (required): two agents, four angles

**Applied:**
- **Reusable checks for CI.**
  - `check-glibc-floor.sh` and `check-pe-runtime.sh` are their own scripts now. The second replaced
    15-01's inline loop in `build-windows.sh`.
  - That removes the `FORROBOX_FLOOR_CHECK_ONLY` environment back door.
  - The atom setup is `headless-x.sh`, which CI will call too.
- **A cached, compiler-free `runtime` stage** replaces the per-run apt install in a fresh
  `ubuntu:22.04`. It is still clean, with no compiler or `-dev` package, it costs nothing after the
  first build, and there is now one package list instead of two.
- **The atom list comes from JUCE's source**, so a JUCE upgrade cannot leave it stale. The hand list
  had missed 13 names.
- **One CMake line:** `target_link_options (ForroBox INTERFACE ...)`, next to its option, reaches
  every consumer. The second block, its hand-kept target list and the split of the LTO pair are
  gone.
- **Smaller:** the two plain apt layers are merged, there is a single `PORTABLE` default, and
  `objdump -T` is read once per binary.

**Skipped:**
- **Making the tests avoid real windows.** The display gap is real for any headless runner, and the
  shared helper covers it in one place.

## Deviations

| Type | Count | Notes |
|------|-------|-------|
| Environment fix | 1 | The headless X atoms (not in the plan; found by the clean run) |
| Structure (simplify) | 1 | Three reusable scripts, a two-stage image |
| Leftover | 1 | `build-portable-mutation/` from the OFF mutation (gitignored); the user may delete it |

## Next
**Phase 15 complete.** Next is Phase 16, CI release builds, which calls these scripts.

---
*Phase: 15-portable-builds, Plan: 02 · Completed: 2026-10-03*
