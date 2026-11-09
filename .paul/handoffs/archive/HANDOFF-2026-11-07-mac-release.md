# PAUL Handoff

**Date:** 2026-11-07
**Status:** paused — side task: build the macOS package of v0.3-beta1 BY HAND on the user's Mac and open the draft pre-release. GitHub Actions is locked (billing) for ~72 h.

---

## READ THIS FIRST — REMIND THE USER IMMEDIATELY ON /paul:resume

You have no prior context. The user is resuming **on their Mac** to make the macOS build.
On resume, lead with the "Mac release steps" section below — the user explicitly asked to be
reminded of it as soon as they run /paul:resume there. Reply in Portuguese (caveman/terse ok).

**Project:** Forró Box — a JUCE 8 VST3/AU/standalone drum-machine plugin for Brazilian forró percussion
**Core value:** Producers get authentic, human-feeling Brazilian forró percussion grooves inside their DAW

---

## Current State

**Version:** v0.3 Reach & My Grooves — CMake now says **0.3.0** (bumped for the beta)
**Phase:** 19 of 19 — Groove files
**Plan:** 19-02 — APPLY Task 1 done (commit 8718d0e), still at the human-verify checkpoint (untouched this session)

**Loop Position:**
```
PLAN ──▶ APPLY ──▶ UNIFY
  ✓        ◐        ○
```

---

## What Was Done (this session, 2026-11-07)

- User wanted a macOS installer released on GitHub before the 19-02 checkpoint.
- Found the release pipeline already ships macOS (Phase 17): tag `v*` → CI builds
  `ForroBox-<ver>-macos-universal.zip` (AU + VST3 + app, ad-hoc signed) → DRAFT release. Never released (v0.2 predates it).
- User decisions: **pre-release `v0.3-beta1`** from current main; **keep the zip** (no .pkg).
- `CMakeLists.txt` VERSION 0.2.0 → 0.3.0, commit `383e5f8`, pushed; annotated tag `v0.3-beta1` pushed
  (`package-release.py --check-tag v0.3-beta1` → version=0.3.0 prerelease=true).
- CI run 42402063212 (tag) and the main run were CANCELLED: "The job was not started because your
  account is locked due to a billing issue." The macos job also said
  "The configuration 'macos-14-arm64-us-default' is not supported" — maybe the lock, maybe macos-14
  retired (then switch `runs-on` to `macos-15` in .github/workflows/ci.yml and re-tag).
- The lock: GitHub Education (faculty) approved 2026-11-06; benefits arrive ~72 h after (≈2026-11-09).
- Cross-building macOS from this Linux/WSL box rejected (needs Apple SDK / Xcode tools).

---

## Mac release steps (DO THIS FIRST on resume)

```bash
xcode-select --install            # if missing
brew install cmake ninja gh
gh auth login

git clone https://github.com/dobidu/forrobox.git && cd forrobox   # or git pull
git checkout v0.3-beta1
scripts/build-macos.sh            # build, ad-hoc sign, verify, auval, pluginval, whole suite
python3 scripts/package-release.py --platform macos \
  --artefacts build-macos/ForroBox_artefacts/Release --out dist

check=$(mktemp -d); ditto -x -k dist/ForroBox-*-macos-universal.zip "$check"
scripts/verify-macos-bundles.sh "$check"/*/ForroBox.component "$check"/*/ForroBox.vst3 "$check"/*/ForroBox.app

gh release create v0.3-beta1 --draft --prerelease --verify-tag \
  --title "Forró Box 0.3.0 beta 1" --generate-notes \
  dist/ForroBox-0.3.0-macos-universal.zip
```

- Judge build-macos.sh by its exit code; on failure read build-macos/auval.log / suite.log and fix.
- The release API is not Actions, so `gh release create` should work despite the lock (unverified).
- The draft asset link can go to the user's friend before publishing.
- Optional, offered and not yet accepted: Linux (`scripts/build-linux-portable.sh`) and Windows
  (`scripts/build-windows.sh`, WSL) packages built locally on the Linux box and attached via
  `gh release upload v0.3-beta1 …` + `package-release.py --checksums dist` for SHA256SUMS.txt.
- When the account unlocks: `gh run rerun 42402063212` (or delete the hand-made draft first so the
  CI's `gh release create` does not collide with an existing release for the tag).

---

## What's Next (after the Mac release)

1. Back to the main loop: the **19-02 human-verify checkpoint** in Ableton — export (EXPORTADO),
   re-import (JÁ EXISTIA), edited variant → "Nome (2)" + MEUS, drag-and-drop, damaged file → dialog,
   WAV onto a strip unchanged. Await "approved" or change requests.
2. 19-02 Task 3: `/code-review`, fix with mutation proofs, commit + push, watch the four CI jobs (once unlocked).
3. UNIFY 19-02 with `/simplify` (Phase 19 close); fix the stale "Loop Position" block in STATE.md.
4. v0.3 milestone close: VERSION is already 0.3.0; MILESTONES.md, tag `v0.3` → draft release, user publishes.

---

## Key Files

| File | Purpose |
|------|---------|
| `.paul/STATE.md` | Live project state |
| `.paul/phases/19-groove-files/19-02-PLAN.md` | The plan being applied |
| `scripts/build-macos.sh` | The proven macOS build (17-01) |
| `scripts/package-release.py` | Release archives, tag rule, checksums |
| `.github/workflows/ci.yml` | CI incl. macos job and draft release |
| `packaging/INSTALL-macos.txt.in` | Install + Gatekeeper notes shipped in the zip |
| `.paul/handoffs/archive/HANDOFF-2026-10-06.md` | Previous handoff (19-02 detail) |

---

## Working conventions (from the user)

- Approve each step ("1", "yes", "approved", "pode seguir"); reply in Portuguese.
- READ-ONLY: PLANNING.md, uploads/UIUX.md, forrobox.css, controls.js, app.js, prototype, ~/JUCE.
- 47 parameter IDs fixed; processBlock allocation/lock-free; no new deps; VST3 install target D:\VST3 on Windows.
  Repo public; pushing authorised.
- /code-review for processor/state/untrusted-input code; /simplify at each phase close; /graphify before planning a phase.
- Always check the build's exit code.

---

## Resume Instructions

1. Read `.paul/STATE.md`
2. Run `/paul:resume` — then remind the user of the Mac release steps above FIRST

---

*Handoff created: 2026-11-07*
