#!/usr/bin/env python3
"""Build a fully self-contained Forró Box HTML from the modular sources.

Inlines forrobox.css and the four vanilla scripts into one file. The React
tweaks island is dropped on purpose: it only renders when a Claude Design host
posts __activate_edit_mode, so in a standalone page it would pull three CDN
scripts and show nothing. Its defaults are applied inline instead.
"""
import json, pathlib, re

HERE = pathlib.Path(__file__).parent
OUT = HERE / "Forró Box (standalone).html"

CSS = (HERE / "forrobox.css").read_text(encoding="utf-8")
SCRIPTS = [(n, (HERE / n).read_text(encoding="utf-8"))
           for n in ("data.js", "audio.js", "controls.js", "app.js")]

# tweak defaults come from the EDITMODE block in tweaks-bridge.jsx so the two
# entry points can't drift apart
bridge = (HERE / "tweaks-bridge.jsx").read_text(encoding="utf-8")
TWEAKS = json.loads(re.search(r"/\*EDITMODE-BEGIN\*/(.*?)/\*EDITMODE-END\*/",
                              bridge, re.S).group(1))

def guard(js):
    """Keep a </script> inside a string literal from closing the tag."""
    return js.replace("</script>", "<\\/script>")

parts = [f"""<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8" />
<meta name="viewport" content="width=device-width, initial-scale=1.0" />
<title>Forró Box</title>

<link rel="preconnect" href="https://fonts.googleapis.com" />
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin />
<link href="https://fonts.googleapis.com/css2?family=Space+Grotesk:wght@400;500;600;700&family=IBM+Plex+Mono:wght@400;500;600&family=JetBrains+Mono:wght@400;500;600&family=Space+Mono:wght@400;700&display=swap" rel="stylesheet" />

<style>
{CSS}</style>
</head>
<body>
  <div class="fb-stage">
    <div class="fb-window" id="fb-window"></div>
  </div>
"""]

for name, src in SCRIPTS:
    parts.append(f"\n  <!-- {name} -->\n  <script>\n{guard(src)}  </script>\n")

parts.append(f"""
  <!-- tweak defaults (from tweaks-bridge.jsx; the React panel needs a design host) -->
  <script>
    (function () {{
      var t = {json.dumps(TWEAKS, ensure_ascii=False, indent=2)};
      window.FB_TWEAKS = t;
      // app.js boots on DOMContentLoaded, so window.FB may not exist yet
      var apply = function () {{ if (window.FB && window.FB.applyTweaks) window.FB.applyTweaks(t); }};
      if (window.FB) apply();
      else document.addEventListener("DOMContentLoaded", apply);
    }})();
  </script>
</body>
</html>
""")

OUT.write_text("".join(parts), encoding="utf-8")
print(f"{OUT.name}: {OUT.stat().st_size} bytes")
