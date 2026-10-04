---
phase: 16-ci-release-builds
plan: 01
subsystem: release
tags: [packaging, reproducible, sha256, install-txt, release]

requires:
  - phase: 15-portable-builds
    provides: "build-portable/ (Linux) and the static-runtime Windows artefacts"
provides:
  - "scripts/package-release.py: one platform's archive in the v0.1/v0.2 layout; --checksums"
  - "packaging/INSTALL-{linux,windows}.txt.in with the 0.3 requirements"
affects: [16-02, 16-03 (CI calls it on each runner)]

key-files:
  created: [scripts/package-release.py, packaging/INSTALL-linux.txt.in, packaging/INSTALL-windows.txt.in]
  modified: [README.md]

key-decisions:
  - "The version comes from CMakeLists.txt, and the VST3's moduleinfo must agree with it"
  - "Reproducible archives: sorted entries, SOURCE_DATE_EPOCH (else the last commit), owner 0, normalised modes"

duration: ~1 session
completed: 2026-10-04
description: "Release packaging is one command per platform: the v0.2 layout byte-for-byte reproducible, INSTALL.txt from committed templates, and a stale build refused"
type: Summary
about: "Forró Box"
---

# Phase 16 Plan 01: Packaging as a script

**v0.1 and v0.2 were assembled by hand** (v0.2 needed a re-zip for a missing directory entry). Now
one call per platform writes the archive in the same layout, reproducibly, and refuses a build whose
version does not match `CMakeLists.txt`.

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: the v0.2 layout | **Pass** | Both archives' entry lists are identical to the v0.2 release's (17 entries each). INSTALL.txt is filled, with no placeholder left |
| AC-2: reproducible | **Pass** | Two runs give byte-identical archives (equal SHA256) |
| AC-3: refuses a wrong package | **Pass** | A missing standalone, a missing bundle binary, and a moduleinfo version ≠ CMake each FAIL, with nothing written |
| AC-4: checksums | **Pass** | `--checksums` writes `SHA256SUMS.txt` in the v0.2 format; `sha256sum -c` OK |

The unpacked Linux package was also checked on the 22.04 runtime image: `ldd` is clean on the
standalone and the `.so`.

## Found
- **The VST3 SDK's `moduleinfo.json` is JSON5-flavoured** (trailing commas), so `json.loads` rejects
  it. Every `"Version"` in it (the module's and each class's) is read by regex, and all must equal
  the CMake version.
- **The missing-licence refusal** became a missing-standalone refusal. The font licences come from
  the repo's `assets/fonts`, not from the build artefacts. The script still requires exactly four.

## Skill audit
`/code-review` was not required: release tooling, no plugin code.

## Next
**16-02:** the CI workflow's build-and-test jobs on push to main and pull requests: the Linux
portable build, and Windows.

---
*Phase: 16-ci-release-builds, Plan: 01 · Completed: 2026-10-04*
