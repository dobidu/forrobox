---
phase: 01-plugin-foundation
plan: 03
subsystem: infra
tags: [windows, msvc, vst3, wsl-interop, ableton, git, cmake, cross-platform]

requires:
  - phase: 01-plugin-foundation (01-01)
    provides: CMake project and plugin targets
  - phase: 01-plugin-foundation (01-02)
    provides: 161-check parameter and state suite, re-run here under MSVC
provides:
  - Windows x64 VST3 built with MSVC 2022, driven from WSL
  - ASCII display name, ending the VST3 metadata corruption
  - Install target discovered from the host's own scanner record
  - Parameter and state layer proven under three compilers
  - Git repository with Phase 1 history
  - Verified load in Ableton Live 12 Suite
affects: [02-sequencer-clock, 03-voices-mix-bus, 04-ui-shell, packaging]

tech-stack:
  added: []
  patterns:
    - "Never assume where a host looks; read it from the host"
    - "Match compiler diagnostics by code, not by localised severity word"
    - "Derive paths from the environment; hardcoded usernames and folder names rot"
    - "Refuse before destroying: probe for locks ahead of a non-atomic replace"

key-files:
  created: [scripts/build-windows.sh]
  modified: [CMakeLists.txt, .gitignore]

key-decisions:
  - "ASCII Forro Box for the display name; accented parameter names kept"
  - "Install target discovered from Ableton's PluginScanner.txt, override via FORROBOX_VST3_DIR"
  - "UNC source + local Windows build dir; no source duplication"
  - "git init with per-group commits; nothing pushed to any remote"

patterns-established:
  - "A green automated check that never asked the right question is not evidence"
  - "Negative-control every assertion that gates a claim"

duration: ~140min
started: 2026-09-07T03:10:00Z
completed: 2026-09-07T05:30:00Z
description: "Windows x64 VST3 built with MSVC 2022 from WSL, ASCII display name, 161 checks passing under a third compiler, and a verified load in Ableton Live 12."
type: Summary
about: "Forró Box"
---

# Phase 1 Plan 03: Windows Build and Host Verification Summary

**Windows x64 VST3 built with MSVC 2022 from WSL, ASCII display name, 161 checks passing under a
third compiler, and a verified load in Ableton Live 12.**

## Performance

| Metric | Value |
|--------|-------|
| Duration | ~140 min |
| Tasks | 3 auto + 1 blocking checkpoint, all complete |
| Files | 1 created, 2 modified |
| Compilers | GCC 13.3, Clang 18.1, MSVC 14.44 — all clean |
| Commits | 7 on `main` |

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: ASCII display name everywhere the metadata carries it | Pass | All six `Name`/`Vendor` entries read `Forro Box`; **0** non-ASCII bytes in `moduleinfo.json`; corruption sequence absent; `CACHAÇA`/`TRIÂNGULO`/`GANZÁ` still valid UTF-8 in the binary |
| AC-2: Windows x64 VST3 builds with MSVC | Pass | UNC source, no fallback needed. PE32+ x86-64 DLL exporting `GetPluginFactory`/`InitDll`/`ExitDll`; 0 diagnostics from our sources |
| AC-3: 01-02 suite passes under MSVC | Pass | 161/161, identical to GCC |
| AC-4: Installed where the host actually looks | Pass — **AC revised mid-flight** | Original wording named `%LOCALAPPDATA%\Programs\Common\VST3` on an unverified assumption. Now installed to `D:\VST3`, discovered from Live's scanner record; stale copy swept |
| AC-5: Live 12 scans, loads and opens it | Pass | Browser shows `Forro Box`; loads on a MIDI track; editor shows the dark chassis; measured frame ratio 1.530 vs design 1.538; Live auto-tagged it `Synthesizer` |
| AC-6: Git repository with Phase 1 history | Pass | 7 commits on `main`, clean tree, no `.wav`/build tree/scratchpad tracked |
| AC-7: Linux build unaffected | Pass | Clean under GCC and Clang, 161/161 |

## Accomplishments

- **Phase 1's goal reached in full**: the plugin loads in the DAW this project targets
- The parameter and state layer proven under three compilers rather than one
- The naming defect closed at the only layer that was ours, with the accented parameter names kept
- A repeatable one-command Windows build for every later phase
- Real git history before the DSP phases, where bisecting matters

## Task Commits

| Task | Commit | Type |
|------|--------|------|
| Prototype and spec import | `a9db7fa` | chore |
| Plugin source (01-01 + 01-02 work) | `3b49133` | feat |
| PAUL framework config | `82709b7` | chore |
| Phase 1 plans and summaries | `8c4161a` | docs |
| Windows build script | `c18fccb` | build |
| 12 code-review fixes | `fbbf1c7` | fix |
| Install-location fix | `90a7c49` | fix |
| Simplify quality pass | `021720b` | refactor |

## Decisions Made

| Decision | Rationale | Impact |
|----------|-----------|--------|
| ASCII `Forro Box` display name | JUCE's `infoW.fromAscii()` sign-extends bytes, corrupting the accent in VST3 class metadata | Accent lost in the product name only; parameter names unaffected |
| Install target read from the host's scanner record | Assuming a convention-permitted folder put the plugin somewhere nothing reads | Works for Ableton; `FORROBOX_VST3_DIR` covers other hosts |
| Diagnostics matched by code, not severity word | Toolchain is pt-BR; `cl.exe` says `aviso C4996` | Locale-independent |
| First-party anchor derived from the configured source path | A literal `forrobox` only works while the checkout keeps that folder name | Survives forks, worktrees, CI checkouts |
| One `cmd.exe` spawn, not three | Measured 619 ms → 75 ms per build | Faster every build |

## Deviations from Plan

### Summary

| Type | Count | Impact |
|------|-------|--------|
| Plan spec errors corrected | 2 | One was the checkpoint failure |
| Auto-fixed during qualify | 2 | |
| Review fixes applied | 12 from `/code-review`, 9 from `/simplify` | Substantial |
| Found while re-running | 1 | A destructive step that destroyed before checking |

### The checkpoint failure — a spec error, not a code error

The plugin built, installed, hash-matched, exported the right symbols, resolved every dependency,
and carried clean metadata — and was **invisible in Ableton**. Live's own `PluginScanner.txt` records
that it scans exactly `C:\Program Files\Common Files\VST3` (global) and `D:\VST3` (custom). The plan
asserted "the per-user folder is writable and Live 12 scans it"; only the first half had been checked.

Classified as a **spec** issue and routed accordingly: AC-4, AC-5 and Task 3 were revised **before**
any code changed. The fix is not "use `D:\VST3`" — hardcoding that repeats the same mistake one
directory over — but reading the host's scan folders from the host.

### `/code-review` — 12 findings

Several reproduced by the reviewer. The sharpest:
- The **mirror fallback could never work**: it reconfigured the same build dir with a different
  source, which CMake refuses. The fallback failed in exactly the case it existed for.
- The **warning filter reported "no warnings" while 17 existed** — English-only token against a
  pt-BR toolchain, and `src/` also matched JUCE's vendored LV2_SDK and oboe trees.
- `find | head -1` **exits 141** under `pipefail` once a second match exists.
- Artifact lookups were unscoped, so a **stale Debug binary could be tested and installed** while the
  hash check compared it to itself and passed.
- The **Windows username was hardcoded**, so `--install` on another account would copy into a
  directory no DAW scans and still exit 0.
- The **moduleinfo regression check printed but never asserted**.

### `/simplify` — 9 findings

- `to_win()` reimplemented `wslpath -w`; tested byte-identical on every path shape used
- First-party anchor and diagnostic matching both fixed at the right depth (see Decisions)
- One `cmd.exe` spawn instead of three (measured)
- Fallback VST3 path computed once; `run()` forks one `tee`; shared cmake flags hoisted

### Found while re-running

**The install destroyed before it checked.** Windows locks a loaded DLL, so with the plugin open in
Live the `rm -rf` failed partway and left the installed bundle holding a binary but **no**
`moduleinfo.json` — broken, and broken silently. It now probes for the lock first, names the holding
process, and modifies nothing. The half-removed bundle was repaired.

## Issues Encountered

| Issue | Resolution |
|-------|------------|
| An early UNC probe reported FAIL on every form | Two causes: the distro registers as `Ubuntu-24.04`, not `Ubuntu`, and `cmd.exe /c dir` is unreliable here. PowerShell then read a 30 KB file over the same path in 22 ms |
| `PluginScanner.txt` not found by discovery | `maxdepth 2`; the file sits at depth 3 |
| `set -u` aborted the script on a `DISTRO` reference | Left behind when the variable was removed. The guard did its job — it failed loudly rather than substituting an empty path |
| Built and installed hashes differ despite no source change | MSVC embeds a build timestamp: identical 6,874,112-byte size and image size, differing only in the PE `time date stamp`. The installed binary is functionally current |

## Next Phase Readiness

**Ready:**
- Phase 2's clock has `bpm`/`swing`/`steps`/`sync` parameters and a persisted grid
- Phase 3's voices have all 35 per-channel parameters, and a Standalone target for auditioning
- Phase 4 can attach to fixed, centralised parameter IDs
- Both platforms build from one command; the test suite runs under three compilers
- Git history exists for bisecting once DSP lands

**Concerns:**
- Install discovery is **Ableton-specific**. Another host needs `FORROBOX_VST3_DIR` or a generalised
  strategy
- 18 `MSB8064` warnings: MSBuild records dependency paths lowercased against a case-sensitive
  filesystem and warns incremental builds may misbehave. A touch-and-rebuild did reconfigure, so the
  risk has not materialised
- Hash comparison can validate a copy but can never answer "is the installed build current", because
  MSVC output is not reproducible
- The vendor folder in Live reads `Forro Box` containing a plugin `Forro Box` — display-only, safe to
  change

**Blockers:** None for Phase 2.

## Skill Audit

| Expected | Invoked | Notes |
|----------|---------|-------|
| `/code-review` | ✓ | After Task 2; 12 findings, all fixed |
| `/simplify` | ✓ | During UNIFY; 4 agents, 9 findings, all applied |
| `/graphify` | ○ | Waived — spec authored this session and in context |
| `/impeccable` | — | N/A per plan; no UI work |

---
*Built with PAUL Framework v1.4 · https://chrisai.cv/skool · https://youtube.com/@chris-ai-systems*
*Phase: 01-plugin-foundation, Plan: 03*
*Completed: 2026-09-07*
