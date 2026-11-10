---
description: "Forró Box — milestone history"
type: Milestones
about: "Forró Box"
---

# Milestones

| Milestone | Version | Status | Phases | Dates |
|-----------|---------|--------|--------|-------|
| v0.1 Initial Release | 0.1.0 | ✅ Shipped | 1–9 (43 plans) | 2026-09-06 → 2026-09-24 |
| v0.2 Hardening | 0.2.0 | ✅ Complete | 10–14 (18 plans) | 2026-09-25 → 2026-10-03 |
| v0.3 Reach & My Grooves | 0.3.0 | ✅ Complete | 15–19 (11 plans) | 2026-10-03 → 2026-11-10 |

## v0.1 Initial Release — ✅ shipped 2026-09-24

Tagged `v0.1` at `6f72b4c`; released as downloadable packages (Windows x64 zip, Linux x86_64
tarball, `SHA256SUMS.txt`) on the GitHub release 2026-09-25.

- A VST3 instrument and standalone, building on Linux and Windows, loading in Ableton Live 12
- Sample-accurate clock with swing and host sync; lock-free pattern handover
- Eight voices, `CACHAÇA` humanisation, character bus, limiter, six-bus multi-out
- The full chassis in both themes at 1×/1.5×/2×, grid, side panel, kit overlay, gear menu, ABOUT,
  both easter eggs
- MIDI out three ways (file, drag, live) and MIDI in
- Sixteen grooves across four profiles from `assets/profiles.json`; `PAT 01–08`, presets, `LOAD`,
  `LOAD IR…`
- 4974 checks on GCC 13, Clang 18, MSVC 2022; six design cross-check gates

## v0.2 Hardening — ✅ complete 2026-10-03

Tagged `v0.2`; released as downloadable packages (Windows x64 zip, Linux x86_64 tarball,
`SHA256SUMS.txt`) on the GitHub release `v0.2`. No new user-facing feature and, by the milestone's
own rule, no audible change. Archive: `milestones/v0.2.0-ROADMAP.md`.

### Stats

| Metric | Value |
|--------|-------|
| Duration | 9 days (2026-09-25 → 2026-10-03) |
| Phases | 5 (10–14) |
| Plans | 18 |
| Commits since `v0.1` | 37 |
| Files changed (all) | 95 (+11,084 / −1,062) |
| Files changed (src, tests, scripts, CMake) | 50 (+4,467 / −920) |
| Checks | 4974 → 5143, on GCC 13, Clang 18 and MSVC 2022 |

### Key Accomplishments

- **Build & tooling (10):** every gate declares its inputs once and CMake enforces them; the MSVC
  run is judged by its exit code with an exit probe; `verify-geometry` resolves scoped names
- **Validation (11):** pluginval v1.0.4 (hash-pinned) is a gate on Linux Debug, Linux Release and
  Windows Release, judged from its log; the whole suite runs in Debug where every unexpected JUCE
  assertion is a failure — which surfaced 3489 hidden ones, all fixed; the MSVC post-main hang
  (JUCE's drag pool) cured; bypass silent and clean; a restore with no grid tells the truth
- **Settings restructure (12):** `SettingsMenu` is its own unit with a typed result; the step-count
  preference stays a write, its declared default a fixed 16 (reversed by the user on review evidence)
- **Multi-instance (13):** the settings store is plain XML file I/O on `PropertiesFile`'s format —
  no Timer, no thread per access; an unreadable file is never overwritten blind; every open instance
  follows a change through one entry point, a font switch re-measures what it draws
- **Remaining debt (14):** the store owns the process-global font and reconciles against what it
  last published; one owner for the overlays' z-order (ABOUT had been opening under the kit panel);
  `Effects.h` holds design numbers only; `ValueScreen` on the one baseline rule, approved visually

### Key Decisions

- pluginval is the one test-tool exemption to "no new third-party dependencies" (discuss-milestone)
- A validator's own SUCCESS is not a pass: the gate judges pluginval's log; the Debug suite fails
  on any unexpected assertion
- A preference SEEDS a parameter through a bracketed write; it never becomes a declared default (12-02)
- Global settings are file I/O with a synchronous listener; a component FOLLOWS a change but never
  SEEDS itself at construction (13-01/13-02)
- The settings store is the one writer of the process-global font, and the arbiter of what the
  process shows (14-01)
- Closed as won't-do: the pulse-timer fold, `lookAndFeelChanged` as the metric channel, a
  `GEOMETRY_HEADERS` glob (104 more constants)

## v0.3 Reach & My Grooves — ✅ complete 2026-11-10

Tagged `v0.3`; CI built the release (Linux x86_64, Windows x64, macOS universal,
`SHA256SUMS.txt`), published 2026-11-10 as Latest. Archive: `milestones/v0.3.0-ROADMAP.md`.

### Stats

| Metric | Value |
|--------|-------|
| Duration | 38 days (2026-10-03 → 2026-11-10; work paused ~5 weeks, 2026-10-06 → 2026-11-07, on CI billing and the first Mac build) |
| Phases | 5 (15–19) |
| Plans | 11 |
| Commits since `v0.2` | 38 |
| Files changed (all) | 85 (+9,193 / −582) |
| Files changed (src, tests, scripts, CMake) | 49 (+5,027 / −510) |
| Checks | 5143 → 5482, on GCC 13, Clang 18, MSVC 2022 and Apple Clang (CI) |

### Key Accomplishments

- **Portable builds (15):** Windows needs no VC++ redistributable (static runtime, refused if a
  binary imports one); the Linux package needs only glibc 2.35, built in an Ubuntu 22.04 container
- **CI release builds (16):** GitHub Actions runs the local gates on every push and PR; a release is
  push a tag → review the draft → Publish, the tag checked against CMakeLists before anything builds
- **macOS (17):** universal AU, VST3 and standalone for macOS 11+, ad-hoc signed, passing auval,
  pluginval and the suite on CI; a zip with Gatekeeper instructions beside Linux and Windows
- **User groove library (18):** save / rename / overwrite / delete your own grooves as
  `<uuid>.forrogroove` files; REGIONAIS | MEUS tabs; every open instance follows another's changes
- **Groove files (19):** the format documented and versioned; Export groove… / Import grooves… and a
  drop anywhere; identical ignored, different copied as "(2)"; damaged or hostile files refused
  before parsing, with the reason shown

### Key Decisions

- Reach first, then grooves; new sound & content is v0.4 (discuss-milestone)
- macOS is built and validated on CI only; packaged unsigned (ad-hoc), with Gatekeeper instructions
- User grooves are DATA, never parameters — the 47 IDs stay fixed, 0.1/0.2 projects open unchanged
- A release is a draft until the user publishes it; the tag must match CMakeLists
- Export one groove at a time; import many; duplicates never overwrite (Phase 19 planning)
- An async `FileChooser` reads its results before its holder is reset — the rule lives in one
  `launchChooser` (19-02, after the Ableton checkpoint caught a silent no-op export)

### Carried forward

Deferred issues in STATE.md: MIDI export's silent `.mid` overwrite, the IR/MIDI choosers not yet on
`launchChooser`, a flaky seqlock contention check on macOS CI.
