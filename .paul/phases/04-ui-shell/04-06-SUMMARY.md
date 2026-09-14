---
phase: 04-ui-shell
plan: 06
status: complete
closed: 2026-09-14
commits: ec2d724..HEAD
---

# 04-06 — Multi-out for real

## What was built

Six output buses: the main stereo out plus one stereo bus per channel, named from `ids::channelInfos`.
In `MULTI-OUT` each channel's voices go to their own bus while the main bus keeps the full mix, and
the `OUTPUT` toggle drives it. `ids::outputMode` has been declared, host-visible, automatable and
persisted since Phase 1 while doing nothing; it now does something.

Verified in Ableton Live 12 at the checkpoint: the aux buses appear by name in an audio track's
source chooser, and a stem meters while the main bus plays.

## Acceptance criteria

| AC | Result |
|----|--------|
| AC-1 six buses, stereo path untouched | PASS — all four grooves bit-identical, re-checked after every task |
| AC-2 each stem carries one channel, pre-everything | PASS |
| AC-3 main bus carries the full mix in both modes | PASS — sample-identical, and only tested after `/code-review` |
| AC-4 STEREO leaves the aux buses silent | PASS — and had **no test at all** until `/code-review` |
| AC-5 nothing indexes a raw channel number | PASS |
| AC-6 the OUTPUT toggle drives the parameter | PASS |
| AC-7 no regression on three compilers | PASS — 2640/2640 on GCC, Clang and MSVC, zero warnings |

## Task commits

| Task | Commit | What |
|------|--------|------|
| 1 declare the buses | `3a13031` | five aux stereo buses, layouts accepted and refused by name |
| 2+3 render and route | `b683b19` | `RenderTargets`, per-voice stem writes, routing from the parameter |
| 4 the toggle | `a238ff8` | `ChoiceAttachment`'s write path; `setReadOnly` removed |
| `/code-review` | `ab9bbc3` | nine findings answered |
| controls | `0d7e694` | two holes closed |

## Decisions taken with the user at planning

- **A stem carries that channel's voices and nothing else** — pre-character, pre-limiter,
  pre-master. Conventional for a drum machine, cheapest, and it keeps `processBlock`
  allocation-free. **The five stems summed therefore do NOT equal the main mix**, because
  `tanh(a+b) != tanh(a)+tanh(b)` and the limiter acts on the sum. A check asserts the inequality with
  the reason in its message, so the first person to measure it finds the answer rather than files a
  bug.
- **The main bus keeps the full mix in MULTI-OUT.** The alternative makes a host that instantiated
  the plugin with its aux buses disabled produce silence with no indication why. The cost is that
  routing both double-counts — audible and obvious rather than silent, and visible in the
  checkpoint's own screenshot.

## What the verification found that writing the code did not

- **A compiler warning** — "variable `stem` set but not used" — exposed that my edit had put the stem
  write into the synth loop TWICE and into the sample loop not at all. Every synthesised voice would
  have been doubled in its own stem while every sampled voice was missing from its.
- **A segfault in an existing test** exposed a real hazard: `getBusBuffer` derives its channel offset
  and count from the DECLARED layout, so handed a buffer narrower than the layout it returns a view
  that reads past the end. `testMonoOutputFoldsDown` drives the fold-down with a mono buffer
  deliberately. Bus views are clamped to the caller's channel count now.
- **`/code-review`, nine findings**, every premise verified against JUCE's own source. Two were
  checks that could not fail, both on ACs I wrote:
  - **AC-4 had no test.** The helper took a `multiOut` flag and I passed `true` at all four sites, so
    the STEREO branch never ran — the `if (isMultiOut())` guard could be deleted with the suite green
    while every host in STEREO with aux buses enabled received stems it never asked for.
  - **The allocation contract never measured the new code.** The existing test runs on the default
    layout, where every aux bus is disabled, so the only new audio-thread code sat outside every
    measured window.
  - Plus a **latent trap**: the main bus's width was the literal `2` while every aux width was
    derived. Allow a mono main and aux bus 1 starts at host channel 1 while `mainBus` still claims
    channels 0 and 1 — writing the full mix, limiter and master into ZABUMBA's stem.
  - Plus two comments describing code that was not there, an uncalled overload with a false doc, and
    a `MixBus::kLatencySamples` reminder that did not mention the stems it would misalign.
- **Two negative controls came back NOT DETECTED**, and each was invisible for a structural reason:
  - `c118` — every test enabled all six buses, where the summed offset and `bus * 2` agree exactly.
    The summation exists for layouts with HOLES, which `isBusesLayoutSupported` accepts and nothing
    exercised.
  - `c120` — `output_mode` has two choices, so its range is 0..1 and the normalised value IS the
    denormalised index. The trap `ChoiceAttachment`'s comment warns about is untestable on that
    parameter. Now tested against `timbre`, which has three, and the test asserts the range has more
    than two choices first so it cannot quietly become untestable again.

## Deviations

| Deviation | Why |
|---|---|
| `VoiceEngine::render`'s single-argument overload was added, then deleted | `/code-review` found it had no caller and its doc was false on both halves |
| A `Stem` mono-aliasing fallback was written, then removed | Unreachable by construction, and if reached it would be a +3 dB error rather than a fold-down |
| The checkpoint's own instructions were wrong | I wrote "Live's I/O panel"; there is no I/O panel on a plugin. Routing lives on the receiving audio track. Corrected when the user could not find it |

## Deferred, with reasons

- **Automating OUTPUT mid-groove clicks the stems.** Reading once per block quantises the
  discontinuity to the block boundary rather than removing it. The main bus is unaffected. A
  stem-side fade needs a time constant that is not in any spec, and inventing one is worse than
  saying plainly it is not done — stated in `isMultiOut()`'s doc.
- **`MixBus::kLatencySamples` is 0 today.** The first non-zero value makes all five stems arrive that
  many samples early relative to the main bus and the host grid, because stems bypass the bus while
  the host applies PDC to every output alike. The structural reminder now names the stem path.
- **No per-channel routing matrix.** `output_mode` is one global switch, which is what the parameter
  declares and what the prototype's toggle shows.

## Verification

| | |
|---|---|
| GCC / Clang / MSVC | 2640 / 2640 each, `DISPLAY` unset, zero warnings |
| Cross-checks | geometry 130 lengths + 44 type values · theme · profiles |
| Negative controls | 11 for this plan, all detected |
| Stereo path | four grooves bit-identical to the committed tree, after every task |
| Allocations | zero across 2000 blocks on BOTH the stereo and multi-out paths |
| Host | Ableton Live 12 — aux buses listed by name, stem metering beside the main mix |

**Skill audit: all required skills invoked.** `/graphify` consumed from the existing graph with the
reason recorded; `/code-review` after Task 3; `/simplify` at UNIFY.
