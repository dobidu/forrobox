# The `.forrogroove` file format — version 1

A `.forrogroove` file holds **one groove**: a name, the feel (tempo, swing,
cachaça) and the eight sequencer lanes at 32 steps each. It is what Forró Box
saves in your groove library (the **MEUS** tab) and what **Export groove…** writes
for you to share. It does **not** carry the timbre, mutes, mixer, voices,
impulse response or pattern slots: those belong to whoever plays the groove.

## Example

```xml
<?xml version="1.0" encoding="UTF-8"?>

<ForroBoxGroove version="1" id="3f2b8c1e-7a4d-4e5f-9b6a-1c2d3e4f5a6b" name="Xote da Feira"
                bpm="96" swing="35.0" cachaca="20.0">
  <lane id="zabumba" steps="100 0 0 0 0 0 70 0 0 0 0 0 110 0 0 0 100 0 0 0 0 0 70 0 0 0 0 0 110 0 0 0"/>
  <lane id="triangulo" steps="90 40 60 40 90 40 60 40 90 40 60 40 90 40 60 40 90 40 60 40 90 40 60 40 90 40 60 40 90 40 60 40"/>
  <lane id="pandeiro" steps="0 0 80 0 0 0 80 0 0 0 80 0 0 0 80 0 0 0 80 0 0 0 80 0 0 0 80 0 0 0 80 0"/>
  <lane id="ganza" steps="50 30 50 30 50 30 50 30 50 30 50 30 50 30 50 30 50 30 50 30 50 30 50 30 50 30 50 30 50 30 50 30"/>
  <lane id="bb" steps="0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0"/>
  <lane id="cx" steps="0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0"/>
  <lane id="hh" steps="0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0"/>
  <lane id="tom" steps="0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0"/>
</ForroBoxGroove>
```

## Fields

The file is UTF-8 XML. The root element is `ForroBoxGroove`.

| Attribute | Meaning | Rule |
|-----------|---------|------|
| `version` | The format version | Must be `1`. A reader refuses a version it does not know. |
| `id` | The groove's identity | A UUID, lower case, dashed (`xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx`). |
| `name` | The name shown in the plugin | 1–24 characters after trimming, no control characters. |
| `bpm` | Tempo | An integer; clamped to 40–300 on reading. |
| `swing` | Swing, percent | A number; clamped to 0–100. |
| `cachaca` | Cachaça (humanisation), percent | A number; clamped to 0–100. |

Exactly eight `lane` children follow, one per lane, in any order:

| Attribute | Meaning | Rule |
|-----------|---------|------|
| `id` | The lane | One of `zabumba`, `triangulo`, `pandeiro`, `ganza`, `bb`, `cx`, `hh`, `tom` (the last four are the BATERIA kit: bass drum, snare, hi-hat, tom). Each exactly once. |
| `steps` | The 32 steps | 32 integers separated by spaces, each 0–127: 0 is a rest, otherwise the velocity. |

A groove made at 16 steps is saved with its first 16 steps repeated, so it plays
the same at 32.

## Reading rules

A reader is **strict**. Any of the following refuses the **whole file**, with a reason; nothing is
half-loaded:

- larger than 64 KB, empty, not UTF-8 text (a UTF-8 byte-order mark is fine), containing a
  `<!DOCTYPE` or `<!ENTITY`, or holding more than 32 elements;
- not XML, another root element, or a `version` other than `1`;
- an attribute or element this page does not list, or anything inside a `lane`;
- a missing, duplicate or unknown lane, a lane without exactly 32 steps, or a step outside 0–127;
- an `id` that is not a lower-case UUID, or a `name` that is empty or too long;
- a `bpm`, `swing` or `cachaca` that is not a number.

Only the feel is forgiving: a tempo, swing or cachaça out of range is clamped to the nearest
playable value.

## Versioning

This page describes **version 1**. A later change that a version-1 reader could not play
correctly will use a new version number, and version-1 readers will refuse it rather than
guess. Unknown attributes are refused in version 1 on purpose: loosening that rule later is
safe, tightening it would break files already in circulation.

## Importing: duplicates

When you import a file, Forró Box uses the **`id` inside the file**, not the file's name:

- **Same id, same groove** as one already in your library: nothing changes.
- **Same id, different groove** (someone edited it and sent it again): it is added as a
  **copy** with a new id and the name followed by ` (2)` (or ` (3)`, …). Nothing in your
  library is overwritten.
- **A new id:** it is added.

A groove you export **exactly as it is in your library** keeps its id, so a friend who
imports it twice gets it once. Exporting a regional groove, or one that differs in any way from
your saved copy (edited steps, another tempo, a 16-step view), gives it a new id.
