---
phase: 17-macos
plan: 01
subsystem: build
tags: [macos, au, universal, auval, pluginval, ci, coregraphics]

requires:
  - phase: 16-ci-release-builds
    provides: "the CI workflow, fetch-juce.sh, validate-plugin.sh"
provides:
  - "AU + VST3 + Standalone, universal (arm64 + x86_64), macOS 11+, ad-hoc signed"
  - "scripts/build-macos.sh: lipo, minos, codesign verify, auval, pluginval, the suite"
  - "validate-plugin.sh --macos (pinned macOS pluginval); bash-3.2 portable"
  - "a macos CI job, green"
  - "a real macOS fix: the wash scratch buffer no longer rebuilt every frame"
affects: [17-02 (packages these bundles), the v0.3 release]

key-files:
  created: [scripts/build-macos.sh]
  modified: [CMakeLists.txt, scripts/validate-plugin.sh, .github/workflows/ci.yml, src/EffectOverlay.cpp, tests/UiTest.cpp, tests/StateRoundTripTest.cpp, tests/VoiceTest.cpp]

key-decisions:
  - "macOS 11 floor, universal (user, Phase 17 planning)"
  - "The FULL suite must pass on macOS; tolerances per-platform, measured, recorded (user)"
  - "One macOS tolerance only: 1 of 255 for clipped vs full paint (CoreGraphics rounding)"

duration: ~1 session (five CI iterations)
completed: 2026-10-04
description: "Forró Box builds for macOS as a universal AU, VST3 and standalone for macOS 11+, ad-hoc signed, and passes auval, pluginval and the whole suite on CI — after fixing a real macOS performance bug and four test-environment assumptions"
type: Summary
about: "Forró Box"
---

# Phase 17 Plan 01: macOS, proven on CI

**The macOS job is green** (run 37208391133, alongside the other three jobs):

| Check | Result |
|-------|--------|
| Binaries | AU, VST3 and app: universal (x86_64 + arm64), LC_BUILD_VERSION minos 11.0 on both slices |
| Signing | Ad-hoc signed; `codesign --verify --deep --strict` passes on all three bundles |
| `auval -v aumu Frbx Frbx` | AU VALIDATION SUCCEEDED |
| pluginval | strictness 10, GUI tests on: PASS |
| The suite | 5143 / 5143 |

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: universal, signed, three formats | **Pass** | lipo, otool minos and codesign checks, all in `build-macos.sh` |
| AC-2: auval + pluginval | **Pass** | Both green from the first complete run |
| AC-3: the full suite | **Pass** | 5143/5143. One macOS-only tolerance (below), measured and recorded. Nothing skipped |
| AC-4: nothing else changes | **Pass** | Linux gate, portable, Windows (CI + local WSL) all green. Linux checks are as strict as before |

## Five CI iterations, five findings

1. **Bash 3.2.** macOS's `/bin/bash` aborts on an empty-array expansion under `set -u`. In
   `validate-plugin.sh` that was `GUI_ARGS`, which is empty when GUI tests run. Fixed with
   `${A[@]+"${A[@]}"}`. The bash-4 associative arrays and `${1^}` had already become `case`
   lookups, and hashing falls back to `shasum`.
2. **A REAL macOS bug.**
   - **The bug:** the wash's scratch buffer (3.7 MB at 1x, 15 MB at 2x) was rebuilt on every
     frame while the wash ran. The chassis is opaque, so the code asks for RGB, but CoreGraphics
     has no 24-bit format and returns ARGB, so `getFormat() != RGB` was always true. The
     allocation check caught it: 1 build expected, 9 seen.
   - **First fix, wrong:** a `SoftwareImageType` scratch. Drawn onto CoreGraphics and Direct2D,
     it composited wrongly, and five wash "darkens nothing" checks failed by ~0.79 on macOS AND
     Windows CI.
   - **Final fix:** rebuild on a size change, or when alpha is needed and missing. An image with
     alpha serves an opaque request, and the native type is kept.
3. **Lock-free stress tests without their second thread.**
   - On Apple Silicon, the saturated-writer test's 200,000 reads finished before the writer was
     scheduled: one publication, no contention.
   - The step-publisher's 400,000 writes finished before either reader ran: zero observations.
   - Both now use start barriers. The checks are unchanged.
4. **A detector below quantisation.** The meter's tick finder counted any 0.002-brightness dip,
   which is less than one 8-bit level (0.0039), so CoreGraphics' gradient banding read as ticks
   (17 found, 15 real). The threshold is now 0.02 on every platform. Measured on Linux, real ticks
   are 0.075–0.216 deep, at least 3.7× the threshold. The message now lists each dip.
5. **CoreGraphics clip rounding.**
   - Clipped and full paints differ by exactly 1 of 255 at clip edges on macOS, and are
     bit-identical elsewhere.
   - `kClipRoundingLevels` is 1 on macOS and 0 elsewhere. A culled piece, which these checks
     guard against, differs by tens to hundreds of levels.
   - The comparison is in levels with a float epsilon. `maxPixelDifference` is float, so one level
     reads 0.00392158, and the first version failed at the boundary.

## Skill audit
`/code-review` was not required: the only `src/` change (`EffectOverlay`'s scratch) is UI, not
processor or audio code.

## Deviations

| Type | Count | Notes |
|------|-------|-------|
| src fix | 1 | The wash scratch rebuild (finding 2), a real performance bug on macOS |
| Test-environment fixes | 3 | Start barriers; tick threshold; message ASCII (an em dash had become mojibake once joined into a juce::String) |
| Platform tolerance | 1 | Clip rounding, macOS only, 1/255 |
| A wrong fix, reverted | 1 | SoftwareImageType scratch (finding 2) |

## Next
**17-02:** the macOS package (`package-release.py --platform macos`), the release job attaching it,
and Gatekeeper INSTALL notes.

---
*Phase: 17-macos, Plan: 01 · Completed: 2026-10-04*
