# Milestone Context

**Generated:** 2026-09-25
**Status:** Ready for /paul:milestone

## Features to Build

No new user-facing features. v0.1 shipped the whole design spec; this milestone pays down what the
*Deferred Issues* table in `STATE.md` carries, in four groups the user selected all of:

- **Build & tooling** — the MSVC test binary blocks after `main` returns (0% CPU, state S, waiting on
  a handle), costing ~15 min per MSVC run behind `scripts/build-windows.sh`'s 900 s timeout. Find
  what is still alive and cure it. Also: a `forrobox_scrape_script_inputs()` helper so each gate's
  CMake dependencies and its script's scan scope stop being two hand-maintained lists (third
  instance in three phases); and `verify-geometry` resolving declarations by `scope::name` rather
  than counting bare names.
- **Validation & robustness** — `pluginval` as a repeatable gate on the VST3; the `loadProfile`
  `JUCE_ASSERT_MESSAGE_THREAD` that a host instantiating on a loader thread can trip; deciding what a
  tagged blob with no `<STATE>` child means (today: empty grid still claiming CAMPINA).
- **Settings restructure** — extract `SettingsMenu` from `Chassis` (owning the id enum,
  `kAccentSteps`, `build()`, `apply(int) -> Result`), which retires the ~24 hard-coded menu ids in
  `tests/UiTest.cpp`; split `applyStoredSettings` into `settings::applyTo(LookAndFeel&, const
  Settings&)` + `repaintAll()`; make the step-count seed a declared parameter DEFAULT, not a write
  (interacts with 08-01's inventory check — a design decision with test consequences).
- **Multi-instance correctness** — `juce::ChangeBroadcaster` on the shared `Settings` store so a
  second open instance stops going stale (theme, radius, accent, steps, font — the font visibly
  torn); stop `PropertiesFile`-per-open spawning and joining `TimerThread` (~14 per gear click); and
  re-run layout after a font switch (`Segmented` spans, `SidePanel`, `ValueScreen::preferredWidth`).
- **Remaining small debt** — one owner for the always-on-top overlays' z-order; `src/Effects.h`'s
  unit (hold its rule, or glob `GEOMETRY_HEADERS`); `ValueScreen`'s `kBaselineFromCentre` →
  `type::baselineIn` (pixel change to an approved component — needs re-approval); fold the pulse's
  30 Hz timer into the sway's.

## Scope

**Suggested name:** v0.2 Hardening
**Estimated phases:** 5
**Focus:** Make v0.1 provably robust — validated by a real plugin validator, correct with two
instances open, structurally ready for the next feature — without changing what a user hears.

## Phase Mapping

| Phase | Focus | Features |
|-------|-------|----------|
| 10 | Build & tooling | MSVC post-`main` hang root cause; gate input-list helper; `verify-geometry` scope resolution |
| 11 | Validation | `pluginval` gate; loader-thread `loadProfile`; no-`<STATE>` blob semantics |
| 12 | Settings restructure | `SettingsMenu` extraction; `applyStoredSettings` split; step count as declared default |
| 13 | Multi-instance | `Settings` change broadcast; `PropertiesFile` thread churn; re-layout on font switch |
| 14 | Remaining debt | Overlay z-order owner; `Effects.h` unit; `ValueScreen` baseline; pulse timer |

**Order is deliberate:** tooling first so every later MSVC run is 15 min cheaper; validation early so
pluginval guards every later change; the restructure before the broadcast because the broadcast lands
in the extracted `SettingsMenu` / `Settings` seams.

## Constraints

- All v0.1 standing constraints hold: read-only design references, fixed parameter IDs (47),
  allocation- and lock-free `processBlock`, never write `C:\Program Files\**`, VST3 target `D:\VST3`.
- **`pluginval` is ALLOWED as a test tool — explicit user decision, 2026-09-25.** Fetched by script
  into a build/tools directory, never linked, never shipped, never committed. This is the exemption
  the "no new third-party dependencies" constraint requires; it covers pluginval only.
- No audible change. Any refactor that could move a sample must prove it did not.
- `ValueScreen`'s baseline fix is a pixel change to a component approved at three checkpoints — it
  needs its own visual checkpoint.
- Read the real exit code from the log, never a wrapper's.

## Additional Context

- **Test-harness migration to `juce::UnitTest` stays DEFERRED** (user decision, 2026-09-25): no
  behavioural gain, and a ~620-line rewrite risks silently dropping coverage.
- **Two questions from the v0.1 close are still unanswered** and are not part of this scope: whether
  to move the `v0.1` tag to include `b55135e`; the MSVC wait itself is now Phase 10's to cure rather
  than to poll around.
- Deferred items NOT in this milestone: prototype GR meter (`app.js`, read-only); `about::authors`
  cross-check (out of proportion); `sync` as parameter (deliberate deviation).

---

*This file is temporary. It will be deleted after /paul:milestone creates the milestone.*
