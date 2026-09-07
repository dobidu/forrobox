---
description: "Forró Box — current position and accumulated context"
type: ProjectState
about: "Forró Box"
---

# Project State

## Project Reference

See: .paul/PROJECT.md (updated 2026-09-06)

**Core value:** Producers get authentic, human-feeling Brazilian forró percussion grooves inside
their DAW without hiring a percussionist or programming every hit by hand.
**Current focus:** v0.1 Initial Release — Phase 1, Plugin foundation

## Current Position

Milestone: v0.1 Initial Release
Phase: 1 of 8 (Plugin foundation) — Applying
Plan: 01-03 executed, 3 auto tasks + checkpoint approved
Status: APPLY complete, ready for UNIFY (phase transition due)
Last activity: 2026-09-07 — Executed 01-03: ASCII rename, git history, Windows MSVC VST3, loaded in Ableton Live 12

Progress:
- Milestone: [█░░░░░░░░░] 8%
- Phase 1: [███████░░░] 67% (2 of 3 plans complete)

## Loop Position

Current loop state:
```
PLAN ──▶ APPLY ──▶ UNIFY
  ✓        ✓        ○     [Apply complete — checkpoint approved in Live 12]
```

## Accumulated Context

### Decisions

| Decision | Phase | Impact |
|----------|-------|--------|
| JUCE 8 + VST3 instrument target | Init | Sets the whole build and parameter architecture |
| Native JUCE GUI, not WebView | Init | UI work is a rebuild; JUCE_WEB_BROWSER=0 in the build |
| Synthesised voices first, samples optional later | Init | Phase 3 implements the handoff voice specs |
| Continue in PAUL rather than re-incubating in SEED | Init | PLANNING.md serves as the incubated spec |
| JUCE via FetchContent pinned to 8.0.12, with a `JUCE_PATH` local override | 1 | Reproducible on a fresh machine, instant against ~/JUCE, nothing vendored |
| Windows VST3 built natively with MSVC 2022 through WSL interop, not cross-compiled with mingw-w64 | 1 | VS 2022 Build Tools + cmake.exe found on the Windows host; MSVC is JUCE's first-class path and mingw VST3 is the fiddly one. Needs no sudo and no new packages |
| Linux VST3 **and** Standalone targets | 1 | Standalone lets Phase 3 voices be auditioned without a DAW |
| ~~`PRODUCT_NAME` ASCII, `PLUGIN_NAME` accented~~ — **superseded 2026-09-07** | 1 | The accented display name proved unusable in VST3 metadata. `PRODUCT_NAME` stays `ForroBox`; `PLUGIN_NAME` is now ASCII too. See the ASCII decision below |
| Explicit `BUNDLE_ID "com.forrobox.ForroBox"` | 1 | JUCE derives the bundle ID from the accented `COMPANY_NAME` otherwise |
| LTO gated behind `FORROBOX_LTO` (default OFF), not tied to the Release default | 1 | Separates "never accidentally Debug" from "optimise the distributable". Clean rebuild 69 s → 39 s |
| Editor stores no processor reference; phases cast `getAudioProcessor()` at point of use | 1 | `AudioProcessorEditor` already holds one; a second reference was dead code needing `[[maybe_unused]]` |
| Invalid (non-empty) `JUCE_PATH` is `FATAL_ERROR`, never a silent network clone | 1 | A typo fails in ~1 s with an actionable message instead of minutes of cloning |
| 45 automatable parameters in 6 `ParameterGroup`s; grid + profile as a `ValueTree` child, never automation | 1 | Fixed IDs Phase 4 attaches to; a host's saved state stays valid |
| One `ids::channelInfos` array of structs replaces three parallel channel arrays | 1 | Divergence impossible rather than guarded by 4 `static_assert`s |
| Bounds live with the data (clamping setters), serialisation clamps are defence in depth | 1 | An out-of-range `presetIdx`/pattern slot cannot exist in memory |
| `LockedState` RAII handle is the only way to reach the non-automatable state | 1 | No unguarded path; a partial lock had advertised safety the main access path lacked |
| Manual base64 kept over JUCE's `var(MemoryBlock)` path | 1 | `MemoryBlock::fromBase64Encoding` trusts an attacker-controlled length prefix before validating payload — a ~1 GB allocation from one project-file string |
| Grid lanes fixed at 32 slots; `steps` selects the active window | 1 | Persistence is never lossy; UI truncation on step change is a separate concern |
| Display name becomes ASCII `Forro Box`; accented parameter/group names stay | 1 | Resolves the VST3 metadata corruption at the only layer that is ours. `CACHAÇA`/`TRIÂNGULO`/`GANZÁ` travel JUCE's correct UTF-16 path and were verified intact |
| ~~Per-user `%LOCALAPPDATA%\Programs\Common\VST3` is the install target~~ — **wrong, corrected 2026-09-07** | 1 | I verified it was writable and asserted Live scans it without checking. Live's `PluginScanner.txt` lists only `C:\Program Files\Common Files\VST3` (global) and `D:\VST3` (custom) — the install worked into a folder nothing reads |
| Install target is **discovered from the host's scanner record**, never assumed | 1 | The script reads `PluginScanner.txt`, prefers a writable `(custom)` entry over the elevation-gated global one, sweeps stale copies from unscanned folders, and honours a `FORROBOX_VST3_DIR` override. Resolved to `D:\VST3` here |
| Ableton Live 12 scan + instantiate is the load proof; no `pluginval` | 1 | Proves the real thing without crossing the no-new-dependencies boundary. Automated edge-case validation revisited in a later phase |
| `git init` during 01-03, one commit per logical group | 1 | Gives phase transition its mandated commit and real history before the DSP phases where bisecting matters |
| Windows build: UNC source + local Windows build dir | 1 | UNC reads measured fast (30 KB in 22 ms); MSVC's many small artifact writes stay off the share. Mirror to `/mnt/c` only as a documented fallback |

### Deferred Issues

| Issue | Origin | Effort | Revisit |
|-------|--------|--------|---------|
| Prototype GR meter never updates (`refs.grFill` captured but never written in `app.js`) | Init | S | Phase 8 wires the real GR meter; fix the prototype only if used for A/B checks |
| Stubbed controls: `LOAD`, `LOAD IR…`, preset cycler, `PAT 01–08`, `MULTI-OUT` | Init | L | Post-v0.1 milestone; specs in PLANNING.md |
| ~~MSVC building from a UNC source path may be slow or fragile~~ | 1 | — | Measured during 01-03 planning: UNC reads are fast (30 KB in 22 ms, 303 files in 216 ms). Design settled — UNC source, local Windows build dir; mirror only as a fallback. An early probe failed only because the distro is `Ubuntu-24.04`, not `Ubuntu` |
| No `pluginval` or `wine` installed | 1 | S | Decided: Ableton Live 12 scan + instantiate is 01-03's load proof. Automated edge-case validation (state fuzzing, bus permutations) revisited in a later phase |
| `sync` made an automatable parameter although PLANNING.md's parameter-mapping list omits it (its global state table includes it) | 1 | S | Deliberate: user-facing toggle that must persist. Recorded as a spec deviation in 01-02 |
| ~~`getPatternState()` hands out a mutable reference~~ | 1 | — | Resolved during 01-02 UNIFY: replaced with the `LockedState` RAII handle. `/simplify`'s altitude agent judged the partial fix actively misleading rather than merely incomplete, which was the right call |
| Test harness duplicates `juce::UnitTest`/`UnitTestRunner`, including `expectWithinAbsoluteError` | 1 | M | Phase 2, when clock tests are added — a mechanical rewrite of 620 lines now risks silently dropping an assertion for no behavioural gain |

### Blockers/Concerns

| Blocker | Impact | Resolution Path |
|---------|--------|-----------------|
| ~~No JUCE toolchain in the project~~ | Resolved 2026-09-06 | JUCE 8.0.12 at ~/JUCE (commit 501c076); cmake 3.28.3, g++ 13.3, clang++ 18.1, ninja, and all Linux dev libs verified present |
| No audio device guaranteed on WSL2 | Phase 3 voice auditioning | Confirmed: standalone logs an ALSA `/dev/snd/seq` warning and runs on. A/B listening likely happens on the Windows side after 01-03 |
| ~~Accented `PLUGIN_NAME` corrupted in VST3 class metadata~~ | Decided 2026-09-07 | ASCII `Forro Box` for the display name only; accented parameter and group names verified safe and kept. Implemented in 01-03 Task 1 |
| ~~LTO makes a full rebuild ~69 s~~ | Resolved 2026-09-06 | Gated behind `FORROBOX_LTO` during UNIFY; default rebuild now 39 s, `-DFORROBOX_LTO=ON` for packaging |
| ~~No git repository~~ | Decided 2026-09-07 | `git init` plus Phase 1 history during 01-03 Task 1. Nothing is pushed to any remote |

### Sample library received 2026-09-07 — architectural decision needed

User supplied `D:\temp\forrobox\FORRO BOX SAMPLES.zip` (9.9 MB, 8 files, all 24-bit stereo).
Inspected read-only; extracted only to the session scratchpad, **not** into the project.

| File | Rate | Length | Kind |
|------|------|--------|------|
| `ZAB_LOW_01..04.wav` | 48 kHz | 0.265–0.498 s | 4 one-shot variants, single articulation |
| `TRIANGULO_BPM_90.wav` | 48 kHz | 2.000 bars @ 90 BPM | tempo-locked loop |
| `ZABUMBA_BPM_90_4_BARS_01.wav` | 48 kHz | 4.000 bars @ 90 BPM | tempo-locked loop |
| `GANZA 02 104.wav` | 44.1 kHz | 4.000 bars @ 104 BPM | tempo-locked loop |
| `PANDEIRO 01 104 DRY.wav` | 44.1 kHz | 4.000 bars @ 104 BPM | tempo-locked loop |

Contradicts the recorded decision "Synthesised voices first, samples optional later — no sample
library to ship yet". Four collisions with the designed architecture:

1. **Loops are not one-shots.** The product is a 16/32-step sequencer with per-step velocity, ghost
   notes, swing and `CACHAÇA` timing jitter. A fixed 4-bar performance cannot carry per-step
   velocity or ghost notes, and applying jitter to a loop is meaningless without slicing.
2. **Two source tempi (90, 104) and two sample rates (48k, 44.1k).** Combining them at a project
   tempo of 132 needs time-stretching; naive rate-shifting transposes, and a triângulo pushed +47%
   is a different instrument.
3. **Coverage is partial.** One-shots exist only for zabumba, and only a "LOW" articulation. Nothing
   for bateria (BB/CX/HH/TOM), no triângulo open/closed pair — and PLANNING.md calls the triângulo's
   velocity-split articulation "the instrument's defining behaviour".
4. Would activate two currently-stubbed controls: per-strip `LOAD` and the `BUNDLE: MINIMAL`
   indicator.

**Does not block 01-02.** PLANNING.md's sample path keeps the same 7 params per channel (`PITCH`
becomes playback rate or pitch shift, `DECAY` an envelope release), so the parameter surface is
identical either way and no rework follows from deciding this later. It does need settling before
Phase 3 is planned.

Samples currently live only in the session scratchpad, which is temporary. If they are to be kept
they need a durable home (`assets/samples/`) — deferred pending the decision.

## Boundaries (Active)

Loop closed; 01-01 boundaries retired. Carried forward as project-wide constraints:

- `PLANNING.md`, `uploads/UIUX.md` — specification, read-only
- The HTML/CSS/JS prototype and `build_standalone.py` — design reference, must keep working
- `~/JUCE` — consumed read-only (note: the open naming decision may argue for a local patch)
- No new third-party dependencies beyond JUCE without an explicit decision

## Session Continuity

Last session: 2026-09-06
Stopped at: Plan 01-03 created; naming, install target, validation approach and git all decided
Next action: Review and approve plan, then run /paul:apply .paul/phases/01-plugin-foundation/01-03-PLAN.md
Resume file: .paul/phases/01-plugin-foundation/01-03-PLAN.md
Open items: (1) samples vs synthesised voices — the only one left; settle before Phase 3 is planned.
01-03 is Phase 1's last plan, so its UNIFY runs the phase transition.

---
*STATE.md — Updated after every significant action*
