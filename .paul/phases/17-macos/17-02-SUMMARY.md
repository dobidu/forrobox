---
phase: 17-macos
plan: 02
subsystem: release
tags: [macos, packaging, gatekeeper, release, ci, codesign]

requires:
  - phase: 17-macos
    plan: 01
    provides: "the universal, signed AU/VST3/app and build-macos.sh"
  - phase: 16-ci-release-builds
    provides: "package-release.py, the release job"
provides:
  - "package-release.py --platform macos: ForroBox-<v>-macos-universal.zip (AU + VST3 + app, signatures intact)"
  - "packaging/INSTALL-macos.txt.in with install paths and the Gatekeeper quarantine"
  - "the macos job packages on a tag and re-verifies the UNZIPPED bundles; the draft release carries three platforms"
  - "scripts/verify-macos-bundles.sh and scripts/judge-suite.sh, shared by the build scripts and CI"
affects: [the v0.3 release, Phase 18+ (CI and packaging are complete)]

key-files:
  created: [packaging/INSTALL-macos.txt.in, scripts/verify-macos-bundles.sh, scripts/judge-suite.sh]
  modified: [scripts/package-release.py, scripts/build-macos.sh, scripts/build-linux-portable.sh, scripts/validate-plugin.sh, .github/workflows/ci.yml, tests/VoiceTest.cpp, README.md]

key-decisions:
  - "Zip, not dmg; ad-hoc signed, not notarised (no Apple account) — the INSTALL explains the quarantine"
  - "What ships is verified, not only what was built: the macos job unzips its own package and re-runs the bundle checks"

duration: ~1 session
completed: 2026-10-04
description: "The macOS build ships: a reproducible zip of the AU, VST3 and app with Gatekeeper instructions, re-verified after unzipping on CI, attached to the draft release beside Linux and Windows; /simplify closes Phase 17"
type: Summary
about: "Forró Box"
---

# Phase 17 Plan 02: The macOS package

**A tag's draft release now carries Linux, Windows and macOS.** `package-release.py --platform macos`
zips the AU, VST3 and app whole, `_CodeSignature` included, with the docs, licences and an
INSTALL. The INSTALL gives the install paths and how to lift the Gatekeeper quarantine: the builds
are ad-hoc signed, not notarised. On a tag, the macos job unzips what it ships and re-verifies it
before uploading.

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: Linux/Windows unchanged | **Pass** | The generalised script (a per-platform bundle list) produces archives byte-identical to the pre-change script at a fixed epoch. Re-checked after /simplify |
| AC-2: the macOS zip, verified after unzip | **Pass** | v0.2-ci-proof3: the unzipped AU, VST3 and app pass `codesign --verify --deep --strict`, with x86_64 + arm64. After /simplify, `verify-macos-bundles.sh` also checks minos on the unzipped copy |
| AC-3: the draft carries three platforms | **Pass** | Draft pre-release with the linux tar, the windows zip, the macos zip and SHA256SUMS; `sha256sum -c` OK on all three. Draft and tag deleted |
| AC-4: refuses a wrong macOS package | **Pass** | Synthetic artefacts: a missing AU, and an Info.plist at 0.1.9, are each FATAL with nothing written |

Only `v0.1` and `v0.2` remain, as tags and releases.

## /simplify at Phase 17 close (required): two agents, four angles

**Applied:**
- **`scripts/verify-macos-bundles.sh`:** universal, minos and codesign per bundle. The expected
  architectures and floor are READ from CMakeLists' `CMAKE_OSX_*` defaults, so there is one source.
  `build-macos.sh` and CI's unzip step both call it. CI's inline copy had skipped the minos check,
  so the shipped zip is now held to the 11.0 floor too.
- **`scripts/judge-suite.sh`:** one rule for a passing suite, used by `build-macos.sh`,
  `build-linux-portable.sh` and the Windows CI step. The three copies had differed in their FAIL
  pattern and limits.
- **auval** polls until the component has registered, instead of a fixed `sleep 2`.
- **`validate-plugin.sh`:** one `case` per platform sets both the zip and its hash.
- **`package-release.py`:** a `bundle_binaries` flag replaces the `"MacOS/*"` magic string.
- **Tests:** the step-publisher's start barrier is a `std::latch`.
- **Re-proved:**
  - `judge-suite` accepts a good log and rejects a failing one;
  - the packager is byte-identical, and the macOS binaries keep 0755;
  - portable build green;
  - local gate 3/3; WSL Windows green;
  - CI main and a fresh tag proof (recorded in STATE).

**Skipped:**
- **Rendering both clip-test images through `SoftwareImageType`:** that would test JUCE's software
  renderer, not the CoreGraphics one users see. The 1-level macOS tolerance stays, measured and
  recorded.
- **A requested-format member in `EffectOverlay`:** "rebuild on size, or missing alpha" is general
  and needs no state.
- **An arm64-only test build on macOS** (minutes saved for a second build dir).
- **Limiting the macos job to main/tags:** Actions minutes are free on a public repo.
- **Caching:** jobs stay within ~10 minutes.

## Deviations

| Type | Count | Notes |
|------|-------|-------|
| Structure (simplify) | 2 | Two shared scripts replace inline copies |

## Next
**Phase 17 complete.** Next is Phase 18, the user groove library.

---
*Phase: 17-macos, Plan: 02 · Completed: 2026-10-04*
