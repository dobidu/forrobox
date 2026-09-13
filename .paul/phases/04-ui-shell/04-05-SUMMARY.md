---
phase: 04-ui-shell
plan: 05
status: complete
closed: 2026-09-13
commits: 5087923..HEAD
---

# 04-05 — The footer, and the split that preceded it

## What was built

The 56 px footer: `MASTER` and its 120 px fader, `LIMITER` with a gain-reduction meter reading the
real limiter, the `DRAG MIDI` call to action, and the `OUTPUT` toggle. And, first, the header moved
out of `Chassis` into its own `HeaderBar`.

Eight new files — `HeaderBar`, `FooterBar`, `GainReductionMeter`, `DragMidiButton`,
`ChoiceAttachment`, `Surface`, `Atomics` — and `Chassis.cpp` shrank from 1354 lines to 704.

## Acceptance criteria

| AC | Result |
|----|--------|
| AC-1 the split | **PASS, amended twice** — see below |
| AC-2 the GR meter reads the real limiter | PASS |
| AC-3 MASTER and LIMITER drive parameters | PASS |
| AC-4 DRAG MIDI honest, OUTPUT honest | PASS |
| AC-5 every new constant cross-checked | PASS — 106→130 lengths, 34→44 type values |
| AC-6 instruments prove themselves, nothing waits on a clock | PASS, after two instruments were found unable to fail |
| AC-7 no regression on three compilers | PASS — 2592/2592 on GCC, Clang and MSVC, zero warnings |

**AC-1 was amended twice at APPLY, both times because I had written it wrong.**

1. It named `ui-renders/*.png` as "the committed reference images". They are gitignored build
   artefacts and have never been committed. The before is produced by building the COMMITTED TREE in
   a throwaway directory and running its own suite — this project's negative-control pattern, and
   strictly stronger than trusting an artefact of unknown age.
2. Absolute pixel-identity is not achievable with a real child component. Making `HeaderBar` a
   `juce::Component` changes two things at once: its paint is clipped to its bounds, and a child
   paints AFTER its parent rather than between two of the parent's regions. Measured cost: **22
   pixels, dark theme only, max channel delta 5** — rows 72-82 of columns 552 and 736, the two 1 px
   inter-strip gutters, where the global knob group's dark-only 18 px glow used to bleed under
   `paintMatrix`'s translucent `--line` fill. **Agreed with the user rather than accepted silently.**
   It also closed a latent bug: gated by `paintIfVisible`, a header-only repaint drew that glow onto
   matrix rows it was not repainting.

## Deviations from the plan

| Deviation | Why |
|---|---|
| The header's constants stayed on `ChassisLayout` | They are `headerInteriorOf`'s inputs and `verify-geometry.py` reads them from `Chassis.h`. The plan's action text said they would move. |
| Task 3's `Segmented` variant landed in Task 2 | The footer layout cannot reserve the OUTPUT box without it. Geometry precedes content. |
| `src/MixBus.*` was edited | An explicit DO-NOT-CHANGE boundary. The plan said a wrong measurement was "a finding to report"; it was reported, and the user's call was to fix it here. |
| `ChassisLayout::headerLayout` was deleted after all | The plan said it would stay. `/simplify` reached the opposite conclusion from three angles at once — see below. |

## What the verification found that the work did not

This is the part worth keeping. Every item below was found by a check, not by writing the code.

### Two of my own measurement instruments could not fail

- **The pixel comparison.** Built on `ImageChops.difference(a, b).getbbox()`. The difference of two
  RGBA images has alpha 0 everywhere, and `getbbox()` reads a fully transparent image as empty — so a
  pixel deliberately moved by one unit reported "identical". Rebuilt on raw channel tuples. **The
  first "all identical" result was therefore unproven**, and rebuilding it is what found the 22
  pixels.
- **The "all three runs draw" check.** Referenced the FOOTER's ground, which every column of a tinted
  gradient differs from — it found one cluster and could not have found three. It references the
  button's own padding column now, row by row, because that ground is a vertical gradient.

### Seven assertions that could not fail

Three found by negative controls during APPLY, four by `/simplify` at UNIFY. All mine, all from this
plan:

- a negative reduction fed to an EMPTY meter, where the decay branch cannot move and the clamp is
  unreachable
- the OUTPUT toggle's box model, which `setBounds` forces onto whatever it is handed
- its segment labels, which the count and the lit index both survive
- `kRangeDb` compared against its own definition
- the auto-margin 2:1 ratio compared against the subtraction that produced it, with a tolerance for a
  rounding that cannot occur
- two "clicking changes nothing" checks that pass with or without `setReadOnly`
- "it draws something at rest", subsumed by the cluster check

### Three real bugs

- **`/code-review`: the OUTPUT toggle read `output_mode` once at build time and never again**,
  contradicting a comment of mine claiming the opposite. **Read-only is about INPUT; the display half
  still needs a listener.** `ChoiceAttachment` binds it, one direction — 04-06 adds the write path
  when it makes the control live.
- **`/simplify`: STYLE had the identical bug**, one control over, in the place Phase 6 would least
  expect it — its headline deliverable is the profile reload, and the control meant to show it would
  have kept lighting whatever was active when the editor opened. `activeProfile` is state rather than
  a parameter, so the header's poll is its home.
- **`/code-review`: `MixBus` published its peak-hold with a load followed by a store.** The reader
  `exchange`s the same cell, so a poll landing between them resurrects a consumed peak and pins the
  meter lit for another frame. Latent until this plan made it reachable. **`/simplify` then found the
  second instance** in `VoiceEngine`, uncommented. The law is now named once in `Atomics.h`.

### A clip gate that never fired

From 04-04, re-blessed by this plan. The header's text gate unions four boxes into one 978 px-wide
rectangle that intersects essentially any repaint — 44.70 µs of glyph layout on every header repaint,
knob-drag frames included, under a comment claiming the text was being skipped. The footer had no gate
at all: 12.11 µs of its 13.36 µs paint, 30 times a second while the limiter works. Both are per-run
now, and measurement showed the header's union approach must NOT be copied to the footer, because
MASTER and OUTPUT union to a box that contains the meter and would never reject.

### The coordinate-space bug the footer surfaced

`juce::Component::getBounds` is PARENT-relative. The header's bar sits at the chassis origin, so its
children's local coordinates happened to equal the chassis's; the footer's sits at y=724, so its
children's small-y rectangles alias straight into the HEADER's boxes. **Two STYLE tests picked up the
OUTPUT toggle the moment it existed** and reported that STYLE had two segments.

`/simplify` judged the first fix — a `boundsInChassis` helper at every call site — to be one level too
shallow, since the helper's own signature reproduces the hazard. The deeper fix is the one `FooterBar`
already had: **the owner of a layout exposes it, and comparisons happen in that owner's space.**
`HeaderBar::getLayout()` now does the same and `ChassisLayout::headerLayout` is gone.

## Decisions worth carrying forward

- **`HeaderBar` owns its layout.** Three `/simplify` agents reached this independently: the copy on
  `ChassisLayout` had no painter, was recomputed every resize for tests alone (~36 µs, most of the
  whole chassis layout), and its correctness rested on the coincidence that broke four tests. The
  plan said it would stay; the plan was holding the worse design. Constants remain in `Chassis.h`,
  where `verify-geometry.py` reads them.
- **DRAG MIDI has no idle pulse and no bobbing arrow.** Deferred to Phase 7 with the export. An
  animated call to action for a control that does nothing is the loudest possible lie.
- **`OUTPUT` is read-only until 04-06**, and still follows its parameter.
- **The footer's auto margins are the prototype's, not the spec's sentence.** Three CSS auto margins
  split the free space equally, so DRAG MIDI is NOT centred — `PLANNING.md:334` calls it centred, and
  the running prototype wins.
- **No shared base class for the two bars.** `/simplify` judged that only the poll timer is genuinely
  identical; their layouts, controls, paints and refresh signatures all differ. `PollTimer` was
  hoisted; the triad was not. Phase 5's sequencer wants 60 Hz and Phase 6's side panel may want no
  poll at all.

## Deferred, with measurements

- **DRAG MIDI's hover glow is clipped.** It reserves 30 px; the 56 px footer around a 37 px button
  allows **9 above and 10 below**. JUCE clips a child to its parent's bounds; the browser lets a
  box-shadow spill over the sequencer. Not fixed — widening `FooterBar` past its own region would put
  the footer over the sequencer. The figure is asserted so it cannot drift.
- **`ChassisLayout` is still three jobs.** Recorded at 04-04, still Phase 6's forcing function.
- **A shared pressable protocol** for `Button`, `StepPad` and now `DragMidiButton` — three instances
  of the reserve-margin / reduce-inverse / `hitTest` triad, each carrying its own copy of the
  reasoning. `/simplify` re-judged `Fader` and confirmed it is still NOT an instance.
- **`atomicMax` will have a third caller** in Phase 5's hit visualisers.

## Traps recorded

- **The MSVC build writes into the same `ui-renders/`** over the WSL path. A render comparison must
  regenerate locally first, or it compares two different rasterisers — which it did once here, and
  reported all 16 renders as differing.
- **`constexpr` on a function calling `juce::Rectangle::withY`** is rejected by Clang
  (`-Winvalid-constexpr`) and accepted by GCC. Third compiler, third catch.
- **A comment naming a symbol can break a verify script.** `verify-profiles.py` anchored its brace
  matcher on `src.index("profileInfos")` — the first MENTION — so a doc comment citing it started the
  match inside prose. Now anchored on the declaration.

## The one gap, stated exactly

**MSVC's last completed run was on `e60f307`, at 2592/2592.** The three commits after it could not be
built under MSVC: the WSL-interop build was killed by the host's memory watchdog on four consecutive
attempts, at full, three-job and two-job parallelism. The Windows toolchain's memory does not appear
in the Linux view, so capping the jobs did not help.

The delta MSVC has not compiled is **three lines**, and they are the restored `checkEqual` on
`kRangeDb`:

```cpp
checkEqual (grmeter::kRangeDb, -forrobox::kLimiterThresholdDb,
            "full scale is asked of MixBus rather than picked, so the meter is full exactly "
            "when the loudest sample was pushed from 0 dBFS to the threshold");
```

Everything else in those three commits is comments and this document — verified by diffing with
comment and blank lines stripped. GCC and Clang both run that line green at 2593/2593. **Re-run
`scripts/build-windows.sh --install` at the start of the next session** to close it; the script now
honours `FORROBOX_MSVC_JOBS` to cap MSBuild's width, which was added while chasing this.

## Verification

| | |
|---|---|
| GCC / Clang | 2593 / 2593 each, `DISPLAY` unset, zero warnings |
| MSVC | **2592 / 2592 at `e60f307`**, VST3 installed, hashes matched, moduleinfo clean — see the gap below |
| Cross-checks | geometry 130 lengths + 44 type values · theme · profiles |
| Negative controls | 34 from committed trees, all detected; 2 recorded structurally inert |
| VST3 | built and installed, hashes match, moduleinfo clean, 0 non-ASCII bytes |
| Renders | compared against builds of two earlier commits, differences enumerated |

**A control caught `/simplify` being wrong.** It advised deleting
`checkEqual (kRangeDb, -kLimiterThresholdDb)` as a tautology — the two sides are one constant, since
`kRangeDb` is DEFINED as that expression. True of the values, false of the check: they are equal only
while the DEFINITION still reads from the limiter's threshold, and that check was the only thing
policing it. Deleting it turned `c88` from detected to NOT DETECTED, one batch later. Restored, with
the reasoning written down so it is not deleted again for looking like a tautology. A `static_assert`
would be stronger but would make `c88` fail to BUILD, and a build failure is not a detection.

**Two controls are recorded INERT rather than counted as passes.** `DragMidiButton` holds a
look-and-feel and two bools, so no mutation makes it touch state without ADDING a member — which is a
feature, not a mutation. And `Segmented`'s `mouseUp` read-only gate is unobservable for a control with
no `onSegmentClicked`. Probing the second found a real hole anyway: nothing checked that a read-only
control stops HOVER-highlighting, which is the gate that actually lies to a user.
