---
phase: 10-build-tooling
plan: 03
subsystem: testing
tags: [python, verify-geometry, cross-check, gate, name-resolution]

requires:
  - phase: 10-build-tooling
    provides: "10-02's gate_inputs.declare()/run() — the new shared module is declared through it"
provides:
  - "verify-geometry: one declaration index (scope chain, name, expression, header, line) and one resolver behind both cpp_constant and the coverage check"
  - "coverage judged per declaration: unresolved or ambiguous excuses, compared-AND-excused, and uncovered declarations all fail"
  - "self_test() on every run, with must-reject cases"
  - "scripts/cpp_text.py — the C++ comment/literal regex shared by verify-charset and verify-geometry"
affects: [phase-14 (glob GEOMETRY_HEADERS becomes a list swap), any gate reading C++ constants]

tech-stack:
  added: []
  patterns:
    - "Resolve, don't count: a key names exactly one declaration or it fails"
    - "A bare key only for a unique name; otherwise one qualified key per declaration"

key-files:
  created: [scripts/cpp_text.py]
  modified: [scripts/verify-geometry.py, scripts/verify-charset.py, scripts/gate_inputs.py, tests/ExitProbe.cpp, scripts/build-windows.sh, CMakeLists.txt]

key-decisions:
  - "cpp_constant and coverage share one index, so value lookup and coverage cannot disagree about which declaration a key means"
  - "/simplify at phase close: the tokenizer regex moved to a shared cpp_text.py rather than being copied into a second gate"

duration: ~1 session
completed: 2026-09-30
description: "verify-geometry resolves every key to exactly one scoped declaration; an excuse naming the wrong namespace now fails where it passed"
type: Summary
about: "Forró Box"
---

# Phase 10 Plan 03: verify-geometry resolves `scope::name`

**Before the change, an excuse naming a namespace that does not exist (`nosuch::kGap`) could hide an unchecked `footer::kGap`, and the gate exited 0. It now exits 1 and names both problems.** Every enrolled constant is indexed once with its full scope. Value lookup and coverage go through the same resolver.

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: every declaration indexed once, with scope | **Pass** | 332 declarations, 20 scopes. Comments and literals are blanked first, and each header is walked on its own. `namespace a::b`, `struct X final : Base`, templates and unnamed blocks are handled. A real-index check also fails any enrolled header that yields no declaration |
| AC-2: keys resolve by scope; ambiguity fails | **Pass** | A qualified key matches the scope-chain suffix. A bare key resolves only when the name is unique. Zero matches fails as "names no declaration"; more than one fails as "ambiguous", listing the candidates |
| AC-3: coverage per declaration | **Pass** | The planning mutation now gives exit 1 with both lines. On the HEAD script, the same mutation gives exit 0. Also rejected: a bare `kBorderWidth` excuse (ambiguous), excusing `pad::kGap` while it has an expectation (compared AND excused), and a new `seq::kGap` (uncovered). The dead-excuse loop is gone, subsumed by the zero-match failure |
| AC-4: one resolver for value lookup and coverage | **Pass** | `cpp_constant` reads from the index. `scope_block` is deleted. A constant in a reopened namespace in a second header resolves (self-test) |
| AC-5: self-tested on every run | **Pass** | `self_test()` joins the failure list. Six regression mutations are each rejected by the self-test alone: the old bare rule, no comment stripping, the both-check removed, an unresolved excuse accepted, the template guard removed, and the line count not advancing |
| AC-6: no regression | **Pass** | The OK line is identical: `235 lengths and 70 type-scale values`. 4974/4974 on GCC 13, Clang 18 and MSVC 2022, and all six gates re-ran in real builds. `--list-inputs` was unchanged after the plan's tasks; `/simplify` then added `scripts/cpp_text.py`, as intended |

## Accomplishments

- **The measured hole is closed.** On the committed script, a wrong-namespace excuse covered a real, unchecked 4 px footer gap. That is now a failure.
- **Two readers became one.** `scope_block`'s first-match lookup and the bare-name `Counter` are both gone.
- **The latent ambiguity surfaced on day one.** `kBorderWidth` and `kRadiusExtra` were bare excuses, each silently covering two declarations. They are now four qualified keys.

## Files Created/Modified

| File | Change | Purpose |
|------|--------|---------|
| `scripts/verify-geometry.py` | Modified | `Declaration`, `index_declarations`, `resolve`/`resolve_one`, `cpp_constant` rebuilt on the index, per-declaration `check_enrolment_coverage`, `self_test`, real-index check, four qualified excuses |
| `scripts/cpp_text.py` | Created (/simplify) | `LITERALS` (moved from verify-charset along with its history) and `blank_non_code` |
| `scripts/verify-charset.py` | Modified (/simplify) | Uses `cpp_text.LITERALS` and declares it as an input |
| `scripts/gate_inputs.py` | Modified (/simplify) | `_record` helper; the flat-glob test is `path.parent != directory`; `run()`'s dead `script` parameter removed |
| `tests/ExitProbe.cpp` | Modified (/simplify) | `writeFormatted` clamps `snprintf`'s return at all four sites. Two sites had no clamp before |
| `scripts/build-windows.sh` | Modified (/simplify) | `run_tests`: `image` is computed once; the tee/rm output path is written once |
| `CMakeLists.txt` | Modified (/simplify) | Deleted a comment that 10-02 made stale |

## Skill audit

| Skill | Invoked | Notes |
|-------|---------|-------|
| `/simplify` (required at phase close) | ✓ | Four agents (reuse, simplification, efficiency, altitude). Findings applied or deferred below |
| `/code-review` | n/a | Required only for audio-thread or processor work. None was done in this plan |
| `/graphify` | n/a | Satisfied at Phase 10 planning. No spec lookup was needed |

## /simplify at Phase 10 close

**Applied:**
- The comment/literal regex is shared through `cpp_text.py`. geometry had its own copy, already drifting: its char arm used `+` where charset's used `*`, and it lacked charset's escaped-quote and digit-separator cases.
- Resolution happens once per key; ambiguity text comes from one `_ambiguous()`.
- The line count is incremental instead of recounting from offset 0.
- A redundant length guard in `resolve` is gone.
- The hand-kept "94 names" count is gone. It said 94 while there were 90.
- The docstrings are trimmed to the invariant.
- The real-index check is new; its mutation of enrolling an empty header is rejected.
- The `gate_inputs` tidies, the ExitProbe clamp helper, the `run_tests` tidy and the stale CMake comment.

**Every applied change was re-proved:**
- Enforcement still holds: dropping `cpp_text` from charset's declaration fails with `UNDECLARED INPUT`.
- The flat-glob rule still holds for direct and nested paths.
- Both Linux trees reconfigured by themselves, and `cpp_text.py` appears in `build.ninja` for both gates.
- MSVC exits 0 with 4974/4974.
- `--exit-probe-selftest` exits 3, the watchdog's code, with 33 frames written through `writeFormatted`.

**Skipped:**
- *A `by_name` dict in `resolve`.* The whole gate takes about 63 ms and `resolve` about 3 ms. After the single-resolution fix, it is not material.
- *Merging the six `--list-inputs` configure calls.* They cost about 0.3 s, at configure time only, and merging them would add coupling.

**Deferred to STATE.md** (each is a change outside this diff):
- verify-midi's `unobservable` exemption. The general fix is for the Node child to report its own reads.
- The ExitProbe watchdog dies with `ExitProcess`, so it cannot capture a DLL-detach hang. The deeper instrument is an out-of-process dump (e.g. `procdump -ma`) taken at the 180 s timeout.
- `verify-profiles.read_constant` and `verify-theme.parse_float_constant` are still unscoped regex readers. The theme one matches inside comments, and its message names `THEME_H` even when reading `EffectOverlay.cpp`. They could move onto a shared resolver.
- `verify-midi` loads `verify-profiles` twice (lines 382 and 485), and `build-profiles` and `verify-midi` each carry their own loader.

## Deviations from Plan

| Type | Count | Impact |
|------|-------|--------|
| Scope additions | 1 | The /simplify changes reached beyond the plan's DO-NOT-CHANGE list (charset, gate_inputs, ExitProbe, build-windows.sh, CMakeLists). This was sanctioned: SPECIAL-FLOWS makes /simplify at phase close required, and it reviews the whole phase. Every change was re-verified on all three compilers |
| Measurement correction | 1 | The plan inherited a comment's "94 names"; the real count was 90 at HEAD. Fixed by deleting the count |

**My own instrument was wrong once.** The first M1 mutation deleted `("footer::kGap",` by substring. That matched a self-test line before the expectation, so it mutated the wrong thing. The report was then missing its second line, which exposed the mistake, and the re-run anchored on the expectation's own text. This is the standing law again: a mutation's report has to be read in full, not just its exit code.

## Next Phase Readiness

**Ready:** Phase 11 (Validation). The MSVC run it rides on is judged by its exit code with the probe armed, and every gate's inputs are declared and enforced.

**Concerns:** Phase 14's globbing of `GEOMETRY_HEADERS` is now a list swap, since the index already handles names shared across headers. Every name it makes duplicate will turn a bare excuse into a loud failure, so expect a batch of qualification.

**Blockers:** None.

---
*Phase: 10-build-tooling, Plan: 03*
*Completed: 2026-09-30*
