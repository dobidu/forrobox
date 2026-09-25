---
description: "Forró Box — milestone history"
type: Milestones
about: "Forró Box"
---

# Milestones

| Milestone | Version | Status | Phases | Dates |
|-----------|---------|--------|--------|-------|
| v0.1 Initial Release | 0.1.0 | ✅ Shipped | 1–9 (43 plans) | 2026-09-06 → 2026-09-24 |
| v0.2 Hardening | 0.2.0 | 🚧 In progress | 10–14 | 2026-09-25 → |

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

## v0.2 Hardening — in progress

No new user-facing features. The deferred-issues backlog: build & tooling, validation (pluginval),
settings restructure, multi-instance correctness, remaining debt. See `ROADMAP.md`.
