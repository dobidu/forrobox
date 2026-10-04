---
phase: 16-ci-release-builds
plan: 03
subsystem: ci
tags: [github-actions, release, draft, packaging, tags]

requires:
  - phase: 16-ci-release-builds
    plan: 01
    provides: "package-release.py"
  - phase: 16-ci-release-builds
    plan: 02
    provides: "the three gate jobs"
provides:
  - "a v* tag packages both platforms and opens a DRAFT release, after all gates pass"
  - "package-release.py --check-tag: the tag rule beside the one version parser"
  - "scripts/fetch-juce.sh: JUCE's tag read from CMakeLists' FORROBOX_JUCE_TAG"
affects: [17 (macOS adds its package to the release job), the v0.3 release itself]

key-files:
  created: [scripts/fetch-juce.sh]
  modified: [.github/workflows/ci.yml, scripts/package-release.py, README.md]

key-decisions:
  - "A tag produces a DRAFT release; publishing is the user's act (user, Phase 16 planning)"
  - "Only the release job holds contents: write, and it needs all three gate jobs"
  - "A mismatched tag fails before any build (early --check-tag in linux-portable)"

duration: ~1 session
completed: 2026-10-04
description: "Cutting a release is push a tag, review the draft, press Publish — the tag is checked against CMakeLists before anything is built, and only after every gate passes does a draft appear; /simplify closes Phase 16"
type: Summary
about: "Forró Box"
---

# Phase 16 Plan 03: A tag makes a draft release

**Releasing is now: bump `project(… VERSION)`, push `vX.Y`, review the draft, press Publish.** A tag
runs the three gate jobs, packages each platform with `package-release.py`, and, only after all
three pass, a `release` job opens a DRAFT GitHub release. That job is the only one with write
access, and it attaches both archives and `SHA256SUMS.txt`.

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: a tag makes a draft | **Pass** | `v0.2-ci-proof` (and `v0.2-ci-proof2` after /simplify): all green; draft pre-release "Forró Box 0.2.0" with three assets. Downloaded: `sha256sum -c` OK, both layouts identical to v0.2. Draft and tag deleted |
| AC-2: a mismatched tag makes nothing | **Pass** | `v9.9-ci-proof`: the release job FATAL "tag … is 9.9 but CMakeLists.txt says 0.2.0 — not releasing". After /simplify, `v9.9-ci-proof2` fails at the EARLY check in 7 s; the release job is skipped. No release either time; tags deleted |
| AC-3: pushes and PRs unchanged | **Pass** | On main: packaging steps and the release job SKIPPED |

Only `v0.1` and `v0.2` remain, as tags and releases.

## Found
- **The first tag regex rejected hyphenated suffixes** (`-ci-proof`). Caught by a local run of the
  check before any tag was pushed, and widened to `[A-Za-z0-9.-]`.
- **GitHub's default Linux shell has no `pipefail`**, so `check | tee $GITHUB_OUTPUT` would have
  left a failing version check green. The output is now captured first.

## /simplify at Phase 16 close (required): two agents, four angles

**Applied:**
- **One version parser.** `package-release.py --check-tag` replaces the workflow's sed copy, which
  could drift from the script's regex. It runs as an early tag-only step, failing in seconds
  instead of after every gate, and again in the release job.
- **One JUCE tag.** `scripts/fetch-juce.sh` reads `FORROBOX_JUCE_TAG` from `CMakeLists.txt` and
  replaces three copied clone steps.
- **Streamed archives.** The tar is written through a gzip with mtime 0, and zip entries are copied
  from the file. Checksums use `hashlib.file_digest`. Binary names come from `PLATFORMS`.
- **Removed:** the dead `SOURCE_DATE_EPOCH` exports (the script falls back to the same commit time)
  and a duplicate `compress_type`.
- **A 30-minute timeout on the Windows suite step**, so a hang does not burn two hours.
- **Re-proved:**
  - two runs at a fixed epoch are byte-identical;
  - the Linux archive's contents and modes equal 16-01's;
  - the Windows archive's entries equal the current build output byte for byte. They differ from
    16-01's only because the Windows build was rerun (MSVC embeds timestamps).

**Skipped:**
- **Build caching (ccache/sccache, docker layer cache):** every job is under 10 minutes on a public
  repo.
- **Dropping the linux-gate configure step:** `validate-plugin.sh` builds that tree, so it must
  exist.
- **Dropping `sha256sum -c` after `--checksums`:** it costs one second and checks the format.
- **Merging the two Package/Upload pairs into a matrix:** they live in jobs with different
  runners.

## Skill audit
`/code-review` was not required: CI configuration.

## Next
**Phase 16 complete.** Next is Phase 17, macOS: its VST3 + AU package joins this release job.

---
*Phase: 16-ci-release-builds, Plan: 03 · Completed: 2026-10-04*
