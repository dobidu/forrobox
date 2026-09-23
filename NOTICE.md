# Licensing

## This project

Forró Box is licensed under the **GNU General Public License v3.0** — see [`LICENSE`](LICENSE).

GPLv3 rather than something permissive because the plugin links **JUCE 8**, which is itself
GPLv3-or-commercial. A permissive licence on this source would not have changed that: anyone
building a binary against GPL JUCE inherits GPL obligations for that binary regardless of what this
repository says. Matching JUCE's terms makes the whole chain consistent instead of only looking
permissive.

## What is NOT covered by that licence

| Component | Licence | Where |
|-----------|---------|-------|
| JUCE 8 | GPLv3 or commercial, from Raw Material Software | Not vendored here. Acquired by CMake via `FetchContent`, or from a local tree via `JUCE_PATH` |
| Space Grotesk | SIL Open Font License 1.1 | [`assets/fonts/SpaceGrotesk-OFL.txt`](assets/fonts/SpaceGrotesk-OFL.txt) |
| IBM Plex Mono | SIL Open Font License 1.1 | [`assets/fonts/IBMPlexMono-OFL.txt`](assets/fonts/IBMPlexMono-OFL.txt) |
| JetBrains Mono | SIL Open Font License 1.1 | [`assets/fonts/JetBrainsMono-OFL.txt`](assets/fonts/JetBrainsMono-OFL.txt) |
| Space Mono | SIL Open Font License 1.1 | [`assets/fonts/SpaceMono-OFL.txt`](assets/fonts/SpaceMono-OFL.txt) |
| The four `ZAB_LOW` zabumba one-shots | Recorded and provided by **Esmeraldo Filho**, who records as Chico Corrêa, cleared for redistribution | [`assets/samples/`](assets/samples/) |

The OFL is compatible with the GPL and imposes its own conditions on the font files themselves —
principally that they keep their reserved names and ship with their licence text, which they do.

**All four are embedded in the binary** (`CMakeLists.txt:204-212`, nine `.ttf` files across the four
families), so all four belong in this table. JetBrains Mono and Space Mono arrived at 08-03 as
selectable display fonts and were not added here at the time — the licence texts shipped with them,
so the OFL's own condition was met, but the file that exists to record which licences apply had
stopped listing them.

## Attribution

The zabumba samples were recorded and provided by **Esmeraldo Filho**, who records as **Chico
Corrêa** (https://soundcloud.com/chicocorrea) and is one of Forró Box's two authors. Three of the four play as measured velocity layers; the
fourth is a *pá* (stick) articulation held aside for a future velocity split. See
[`assets/samples/README.md`](assets/samples/README.md) for the measurements and the reasoning.
