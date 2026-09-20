---
phase: 06-side-panel
plan: 04
subsystem: infra
tags: [utf-8, charset, cmake, msvc, verification, testing]

requires:
  - phase: 06-02
    provides: the accented side-panel strings, and the split-literal treatment this deletes
provides:
  - accented text written as characters, with the source/execution charset pinned in CMake
  - scripts/verify-charset.py — every src/ literal checked against the drawn repertoire
  - a charset-independent test oracle, written as code points
affects: [06-05, 06-06, 06-07, 07-midi-out]

tech-stack:
  added: []
  patterns:
    - "src/ spells the CHARACTER; a test fixture or oracle spells the BYTE"
    - "the repertoire has ONE owner and the other side asserts agreement"

key-files:
  created: [scripts/verify-charset.py]
  modified: [CMakeLists.txt, src/ParameterIDs.h, src/Chassis.cpp, src/Chassis.h, tests/UiTest.cpp, scripts/verify-profiles.py]

key-decisions:
  - "Charset pinned on the ForroBox target PUBLIC, gated on the MSVC VARIABLE so clang-cl is covered"
  - "The GNU flag pair is probed with check_cxx_compiler_flag, not assumed — it is a hard failure before Clang 18"
  - "A test ORACLE is written as code points: the byte is its meaning, so the compiler must not be able to corrupt it"
  - "tests/ keeps its \\xNN fixtures deliberately, and that is now a stated rule rather than an accident"

patterns-established:
  - "A checker that enumerates a rule must assert agreement with any other copy of it"
  - "Prove a check can fail in BOTH directions — that it rejects the bad and accepts the good"

duration: ~110min
started: 2026-09-20T02:52:00-03:00
completed: 2026-09-20T04:42:00-03:00
description: "Accented text is written as characters with the charset pinned in CMake; the escape machinery is deleted and a fourth cross-check covers every literal"
type: Summary
about: "Forró Box"
---

# Phase 6 Plan 4: The charset, pinned — Summary

**The `\xNN` escape is gone from `src/`, the charset is pinned once in CMake, and the three pieces
of machinery that existed to survive the escape are deleted — but the plan's own safety net had a
hole that `/code-review` found and a mutation proved, so a fourth cross-check now covers every
literal in the tree.**

## Performance

| Metric | Value |
|--------|-------|
| Duration | ~110 min |
| Tasks | 3 of 3, all PASS |
| Qualify | 3 PASS / 0 GAP / 0 DRIFT |
| Files modified | 13 + 1 created |
| Checks | 3776 → **3787** (+11) |
| Cross-checks | 3 → **4** |
| Suite runtime | 2.98 s (was 3.00 s) |
| Compilers green | GCC 13, Clang 18, MSVC 2022 — 0 warnings |

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: no `\xNN` survives in a `src/` literal | **Pass** | 36 sites across 10 files converted; grep finds none |
| AC-2: the three compilers agree on the bytes | **Pass** | All green; `/utf-8` confirmed in `ForroBox.vcxproj` AND propagated to `ForroBoxTests.vcxproj`; moduleinfo clean, 0 non-ASCII bytes, hashes match |
| AC-3: the verifier compares text directly | **Pass** | `decode_c_escapes` deleted; 42 fields compared, 0 mismatches; mutation caught with correct accents on both sides |
| AC-4: cannot false-fail on a correct accent | **Pass** | Control asserts `É Ó Ç í ú à õ` are accepted — all seven outside the old twelve-character list |
| AC-5: can still fail, and it is proved | **Pass** | Control asserts U+00DD is rejected; both controls proven to fail when inverted |

## Accomplishments

- **The escape class is unrepresentable, not merely detectable.** Four previous local fixes each
  made the greedy `\xNN` bug visible; pinning `/utf-8` on MSVC and the probed GNU pair elsewhere
  removes it. `decode_c_escapes`, the split literals, the split-literal comment at `Chassis.h:545`
  and the twelve-character allowlist are all gone.
- **A real hole was found in this plan's own work and closed.** See Deviations.
- **The only compiled-bytes check is now charset-independent.** Its oracle is code points.
- **The repertoire has one owner.** `verify-charset.py` parses the C++ array and asserts agreement.
- **Three drawn characters gained glyph coverage** — `—`, `…`, `™` were declared drawn by the new
  allowlist while nothing proved the embedded faces ink them.

## Files Created/Modified

| File | Change | Purpose |
|------|--------|---------|
| `scripts/verify-charset.py` | **Created** (211 lines) | Every `src/` literal against the drawn repertoire; asserts agreement with the C++ oracle |
| `CMakeLists.txt` | Modified | Charset pinned; `verify-charset` wired with `CONFIGURE_DEPENDS` |
| `src/ParameterIDs.h` | Modified | 13 literals; the split-literal doc comment deleted; tables realigned |
| `src/Chassis.cpp` / `.h` | Modified | 11 literals; the discipline comment and its stale `PluginProcessor.cpp:686` citation deleted |
| `src/HeaderBar.cpp`, `KitOverlay.cpp`, `SidePanel.cpp`, `Typography.cpp`, `DragMidiButton.cpp`, `MixBus.h`, `PluginProcessor.cpp` | Modified | The remaining 12 literals |
| `scripts/verify-profiles.py` | Modified | `decode_c_escapes` deleted; `join_literals` kept; stale line offset removed |
| `tests/UiTest.cpp` | Modified | Repertoire rule + 2 negative controls + 3 glyph fixtures; oracle as code points |

Net: **−17 lines** in `src/` + `scripts/` (machinery deleted); **+205** in CMake + tests (the new
coverage and the reasoning behind it).

## Decisions Made

| Decision | Rationale | Impact |
|----------|-----------|--------|
| Pin the charset on `ForroBox` **PUBLIC** | The test target already inherits `juce_recommended_config_flags` the same way; a second copy would drift. Verified: only `ForroBox` compiles `src/`, only `ForroBoxTests` compiles `tests/` and it links `ForroBox` | Every target that needs it has it; JUCE's own modules are not widened |
| Gate on the **`MSVC` variable**, not `CXX_COMPILER_ID` | clang-cl reports its id as `Clang` while taking CL-style args, so an id test hands it `-finput-charset` (ignored, warning only) and never passes `/utf-8` | A Windows `-T ClangCL` build is covered |
| **Probe** the GNU pair rather than assume it | `-fexec-charset=` is rejected outright before Clang 18 — a hard build failure, and the flags buy nothing where UTF-8 is already the default | No version cliff for contributors |
| A test **oracle** is written as code points | Everywhere else the rule is "spell the character". In an oracle the BYTE is the meaning — written as UTF-8 it mangles in lockstep with the subject it judges | The check cannot be neutralised by the failure it detects |
| `tests/` keeps its `\xNN` fixtures | Byte-exact fixtures for a font-glyph check, each a complete standalone sequence, pinned by `checkEqual (text.length(), 1)` | The file now has one rule: `src/` spells characters, fixtures and oracles spell bytes |
| The repertoire is a **rule**, not an inventory | The old list held the twelve characters present the day it was written; the source uses 23 | A correct new accent cannot false-fail |

## Deviations from Plan

### Summary

| Type | Count | Impact |
|------|-------|--------|
| Auto-fixed | 6 | Five `/code-review` findings + one self-caught defect |
| Scope additions | 2 | `verify-charset.py`; 3 glyph fixtures |
| Criterion missed | 1 | "Net lines removed" — stated wrongly by me |
| Deferred | 4 | Logged to PROJECT.md |

### Auto-fixed

**1. The plan's safety net did not cover what the plan converted.** `/code-review`, medium.
The runtime check walks `ids::profileInfos` and `timbreSpecs` only, while this plan converted ~19
further literals. **Proved by mutation: `CACHAÇA` replaced with its mojibake `CACHAÃ‡A` built clean
and ran 3778/3778 GREEN.** Escapes were charset-proof; converting them without adding coverage
traded that away for nothing. Closed by `scripts/verify-charset.py`, wired as a build dependency,
proven to catch that exact mutation.

**2. The oracle could be corrupted by the failure it detects.** `/simplify` altitude, and the most
serious item in this plan. The allowlist was converted from byte escapes to UTF-8 characters, so
under a uniform charset misread the oracle mangles with its subject and **every corrupted string
reports zero offenders** — the mangled list contains `Ã © ¡ ¢ £ ª „ ‚`, the mojibake alphabet
itself. Simulated and confirmed, then fixed by expressing `allowed` as 36 named code points and
both control subjects as byte escapes, and re-simulated: 4, 2 and 3 offenders where there were 0.

**3. The rationale claimed a guarantee it did not provide.** `/code-review`, medium. The comment
said the check was the safety net for a missing `/utf-8`; MSVC without it decodes *and re-encodes*
with the same ANSI page, so on CP1252 the bytes round-trip and there is nothing to catch. Corrected
to state what it actually detects — a lossy or DBCS page.

**4. clang-cl and the Clang <18 flag cliff.** `/code-review`, low ×2. Both fixed above.

**5. My own check message printed `Ã Ã Ã Ã­`.** Caught by reading a mutant's output rather than its
pass/fail: `check` takes a `juce::String`, and `juce::String (const char*)` reads Latin-1 — the trap
`Chassis.h` exists to warn about, reappearing inside the test written to catch mangled text. Now
moot: the message is ASCII code-point names.

**6. Smaller `/simplify` items.** `strip_comments` blanked each character (56.4 ms → 6.4 ms, output
verified byte-identical across all 548 literals); failures went to stdout while the three sibling
verifiers use stderr; `TYPOGRAPHIC` was a dict whose values were never read; `MixBus.h:75` alignment
was missed when the escape shrank; `verify-profiles.py`'s "135 lines above" had already rotted to
130; the rationale narrative was written in four files and is now in one.

### Scope additions

- **`scripts/verify-charset.py`** — not in the plan. Required by finding 1; without it the plan
  shipped a coverage regression.
- **Three glyph fixtures** (`—`, `…`, `™`) — the new allowlist declares them drawn, so nothing
  proving the faces ink them was a claim without a check. All three pass; the fonts do carry them.

### Criterion missed

The plan's success criteria said *"Net lines REMOVED across the diff — this plan deletes machinery,
it does not add any."* `src/` + `scripts/` is **−17** as promised, but CMake + tests are **+205**.
The criterion was mine and it was wrong: a coverage hole cannot be closed by deleting lines. Meeting
it as written would have meant shipping the `CACHAÇA` gap.

### Deferred (logged to PROJECT.md)

- `Chassis.cpp:88` builds the `FORRÓ·BOX` wordmark inline per layout pass rather than from a
  `static const` like its neighbours. Pre-existing; this plan only changed its bytes.
- The Portuguese half of the repertoire is a hand inventory of a set a Unicode rule states exactly
  (`category in {Ll,Lu}` and NFD base is ASCII). The symbol half is correctly enumerated.
- `struct Glyph`'s `U+XXXX` names are prose that nothing verifies against the bytes beside them.
- `verify-charset.py`'s accent floor sits at 40 against a measured 57.

## Issues Encountered

| Issue | Resolution |
|-------|------------|
| A "3778 passed" reading taken from a **stale binary** — `verify-profiles.stamp` had failed and halted ninja | Discarded and re-run. The handoff records this exact trap |
| `PIPESTATUS` outside a subshell measured `grep`, not the suite | Re-measured; confirmed the suite does exit 1 on failure |
| Deleting `CMakeCache.txt` alone lost the generator and broke `build-linux` | Wiped and reconfigured with `-G Ninja`; the clean configure then properly exercised the new CMake logic |

## Skill Audit

| Expected | Invoked | Notes |
|----------|---------|-------|
| `/graphify` | ✓ | At planning — settled the prototype's profile-load path for 06-05 |
| `/code-review` | ✓ | 5 findings, all verified before acting, all fixed |
| `/simplify` | ✓ | 4 agents; 2 material findings, ~8 smaller ones |

All required skills invoked. This closes 06-02's recorded `/code-review` gap pattern.

## Next Phase Readiness

**Ready:** 06-05 (the processor announces a profile load) can add accented strings freely — they are
written as characters, checked at the source by `verify-charset.py` and at runtime for the two
tables. Its own design question is already settled: it needs its **own** counter, because
`getPatternPublicationCount` bumps on every `toggleCell`.

**Concerns:** `verify-charset.py` scans `src/` only. `tests/` literals legitimately use characters
this UI does not draw (a `±` tolerance, deliberate mojibake fixtures), so the drawn repertoire is
the wrong rule there — recorded rather than assumed closed.

**Blockers:** None.

---
*Built with PAUL Framework v1.4*
*Phase: 06-side-panel, Plan: 04*
*Completed: 2026-09-20*
