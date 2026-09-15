# Forró Box

A VST3 instrument for Brazilian **forró** percussion — a step-sequencer drum machine with five
instrument channels (zabumba, triângulo, pandeiro, ganzá, bateria), regional groove profiles, and a
humanisation engine.

**JUCE 8 · C++20 · VST3 · Linux + Windows**

> **Status: in development.** The engine and the interface are real and tested; several controls are
> still deliberate stubs. See [What works today](#what-works-today).

---

## What works today

| Area | State |
|------|-------|
| Eight percussion voices | Seven synthesised, zabumba from three measured velocity layers |
| `CACHAÇA` humanisation | Per-step timing jitter, velocity variation, ghost notes — every value a keyed hash, not a draw |
| Sequencer clock | Sample-accurate from the audio block's position; host sync with step 0 locked to the bar |
| Character bus | HI-FI / LO-FI / CICLOTRON™, limiter, squared-taper master |
| Multi-out | Six VST3 buses — the full mix plus one stereo stem per channel |
| Interface | Full chassis at 1×/1.5×/2×, dark and light, every control a custom Component |
| Sequencer grid | Five rows, 16/32 steps, click to edit, continuous playhead, per-channel LEDs and meters |
| State | Lossless save/reload, hardened against malformed project data |

**Not yet:** profile loading (the `STYLE` buttons and profile field are drawn but inert), MIDI
export and drag-out, the timbre side panel, and the `CACHAÇA` easter egg. A fresh instance starts
with an empty grid — click pads in to hear it.

## Building

Requires CMake 3.22+, a C++20 compiler, and JUCE 8.

```bash
cmake -B build-linux -DCMAKE_BUILD_TYPE=Release -DJUCE_PATH=/path/to/JUCE
cmake --build build-linux -j
```

Without `JUCE_PATH`, CMake fetches JUCE itself. The VST3 lands in
`build-linux/ForroBox_artefacts/Release/VST3/`.

### Tests

```bash
cmake --build build-linux --target ForroBoxTests
./build-linux/ForroBoxTests
```

3313 checks, and they run headless — the UI tests render into an image with `DISPLAY` unset, so
there is no display dependency.

Three design cross-check scripts run against the prototype sources and fail the build on divergence:

```bash
python3 scripts/verify-theme.py      # colour tokens, both themes
python3 scripts/verify-geometry.py   # 152 lengths + 50 type-scale values
python3 scripts/verify-profiles.py   # the groove tables, against data.js
```

### Windows

`scripts/build-windows.sh` builds the VST3 natively with MSVC through WSL interop and installs it to
the host's VST3 folder. `FORROBOX_MSVC_JOBS` caps MSBuild's width if the build is memory-constrained.

## The prototype

`Forró Box (standalone).html` is a working HTML/CSS/JS prototype and the project's **design source of
truth** — the plugin is a native reimplementation, not a port. Colours, geometry, type and the
groove tables are cross-checked against it on every build by the scripts above, which is why
`forrobox.css`, `data.js`, `app.js` and `controls.js` are in the repository. Open the standalone file
in a browser to compare behaviour side by side.

Its own notes are in [`docs/README-prototype.md`](docs/README-prototype.md).

## Licence

GPLv3 — see [`LICENSE`](LICENSE). This matches JUCE's own open-source terms; a permissive licence
here would not change what a binary built against GPL JUCE inherits.

[`NOTICE.md`](NOTICE.md) records everything the project licence does not cover: JUCE itself, the two
OFL fonts, and the samples.

## Credits

Zabumba samples recorded and provided by **Chico Corrêa** —
[soundcloud.com/chicocorrea](https://soundcloud.com/chicocorrea).

Fonts: Space Grotesk and IBM Plex Mono, both under the SIL Open Font License.
