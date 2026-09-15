# Zabumba samples

## Provenance and rights

Recorded and provided by **Chico Corrêa** — https://soundcloud.com/chicocorrea — and cleared for
redistribution, confirmed 2026-09-15 before this repository was made public.

Stated here because the paragraph below used to record only where the files came FROM. That is not
the same question as whether they may be shipped, and a public repository makes them downloadable
by anyone; the distinction was worth one line rather than leaving the next reader to guess.

Supplied 2026-09-07 as `FORRO BOX SAMPLES.zip` (9.9 MB, 8 files). These four one-shots are
the only files from that archive that ship. All four are 48 kHz, 24-bit, **true stereo** (left and
right differ — they are not dual-mono), and each contains exactly one attack.

Embedded into the binary via `juce_add_binary_data`, never read from a filesystem path: a runtime
path would break the moment the plugin is installed anywhere but the machine it was built on.

## Measurements

Measured 2026-09-08 during 03-01. The code does not trust this table — `ZabumbaSampler` re-measures
at load and derives the mapping from what it finds, so replacing a file cannot silently reorder
anything. The table is here so a reader can see *why* the code is shaped as it is.

| File | Length | Peak | RMS | Peak/RMS | Centroid | Brightness | Role |
|------|--------|------|-----|----------|----------|------------|------|
| `ZAB_LOW_04.wav` | 0.265 s | −23.1 dBFS | 0.0182 | 3.8 | 125 Hz | 0.235 | velocity layer 1 (soft) |
| `ZAB_LOW_02.wav` | 0.457 s | −2.5 dBFS | 0.1223 | 6.2 | 126 Hz | 0.222 | velocity layer 2 (medium) |
| `ZAB_LOW_01.wav` | 0.498 s | −3.1 dBFS | 0.1958 | 3.6 | 96 Hz | 0.143 | velocity layer 3 (open) |
| `ZAB_LOW_03.wav` | 0.344 s | −2.0 dBFS | 0.0373 | 21.3 | 527 Hz | 0.810 | *pá* / stick — held aside |

`Brightness` is RMS after a two-pole 250 Hz highpass, over total RMS.

## Why `ZAB_LOW_03` is not a velocity layer

The project notes originally recorded all four as "4 one-shot variants, single articulation", and
the hybrid engine decision was taken on that description. Measurement contradicts it.

`01`, `02` and `04` are one strike at three levels: 87–98% of their energy sits below 160 Hz and
their centroids are within 30 Hz of each other. `03` is a different articulation — centroid 527 Hz,
78% of its energy *above* 160 Hz, and a peak-to-RMS ratio of 21 against the others' 3.6–6.2. It is
the stick (*pá*) hit, which shares the `ZAB_LOW` filename prefix but not the sound.

It is loaded and measured but unreachable from the velocity mapping, because the sequencer has one
zabumba lane and nothing to select an articulation with. When that lane gains a velocity split — as
the triângulo already has — this file is what it plays.

## Why levels are normalised by RMS, not peak

Peak-normalising is the obvious choice and it is wrong here. `04`'s peak/RMS is 3.8 while `03`'s is
21.3, so equalising peaks would make the *softest* layer the loudest thing in the set. Layers are
normalised to a common RMS and adjacent layers crossfade, so there is no discontinuity at a
boundary and perceived level rises smoothly with velocity.

One consequence worth knowing: because the layers' peak/RMS ratios differ (3.8, 6.2, 3.6 softest to
loudest), rendered **peak** level is not monotonic in velocity across the whole range even though
**RMS** is. The tests assert on RMS for this lane and on peak for the synthesised lanes, where the
envelope peak is literally `k · v`.

## What does not ship

The archive's other four files are tempo-locked loops and stay out of the repository:

| File | Rate | Length |
|------|------|--------|
| `ZABUMBA_BPM_90_4_BARS_01.wav` | 48 kHz | 4 bars @ 90 BPM |
| `TRIANGULO_BPM_90.wav` | 48 kHz | 2 bars @ 90 BPM |
| `GANZA 02 104.wav` | 44.1 kHz | 4 bars @ 104 BPM |
| `PANDEIRO 01 104 DRY.wav` | 44.1 kHz | 4 bars @ 104 BPM |

A fixed 4-bar performance cannot carry per-step velocity, ghost notes or `CACHAÇA` timing jitter
without being sliced; the two source tempi (90, 104) and two sample rates would need
time-stretching, and a triângulo rate-shifted +47% is a different instrument. They also cover
nothing that is missing — no bateria BB/CX/HH/TOM, and no triângulo open/closed pair, which
`PLANNING.md` calls that instrument's defining behaviour.

The archive itself remains outside the repository.
