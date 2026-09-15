# Forró Box

Imported from the Claude Design project `2711c990-e0c0-4599-954d-27093aec88f5`.

## Files

| File | Role |
| --- | --- |
| `Forró Box (standalone).html` | **Generated.** Single self-contained page — all CSS/JS inlined. Open directly in a browser. |
| `Forró Box.html` | Modular entry point; loads the sources below plus the React tweaks island from a CDN. |
| `forrobox.css` | Stylesheet (dark + `[data-theme="light"]` palettes, tweakable `--r` / `--accent-i`). |
| `data.js` | Instruments, channel defaults, regional profiles, groove patterns, presets. |
| `audio.js` | Web Audio synth voices, master chain (timbre shaper → limiter), SMF export. |
| `controls.js` | Knob and Fader widgets. |
| `app.js` | State, DOM build, sequencer, transport clock, humanisation. |
| `tweaks-panel.jsx` / `tweaks-bridge.jsx` | Claude Design tweaks panel (host-driven; not used standalone). |
| `build_standalone.py` | Regenerates the standalone file from the sources. |

## Build

```bash
python3 build_standalone.py
```

The tweak defaults in the standalone are read out of the `EDITMODE` block in
`tweaks-bridge.jsx`, so the two entry points cannot drift apart.

## Notes

- The standalone drops the React tweaks island deliberately: it only renders
  after a Claude Design host posts `__activate_edit_mode`, so offline it would
  pull React + ReactDOM + Babel and display nothing. Its defaults are applied
  inline instead.
- Only remaining network dependency is the Google Fonts stylesheet; the CSS
  declares system fallbacks, so the page works offline.
- Audio unlocks on the first pointer press (browser autoplay policy). Space bar
  toggles play/stop.
