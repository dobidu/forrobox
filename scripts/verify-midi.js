/* ============================================================================
   FORRÓ BOX — the prototype's own exportMIDI, run as the reference

   This is NOT a port of `exportMIDI`. It loads `audio.js` and calls the function
   the design source itself defines, so the expected bytes come from the
   reference implementation rather than from someone's reading of it. A port
   would be a second transcription, which is the thing the cross-check exists to
   make unnecessary.

   Reads states as NDJSON on stdin — one JSON document per line — and writes one
   line of hex per state on stdout. The same interface `ForroBoxTests
   --emit-midi` presents, so `verify-midi.py` can hand both sides the identical
   documents.

   BATCHED, one process for the whole matrix rather than one per state. Measured:
   a Node spawn costs 20.3 ms here (14.9 ms of bare interpreter start, 5.4 ms to
   re-read and re-evaluate `audio.js`), so 24 states cost ~487 ms of process
   startup to do ~26 ms of work. One process runs all 24 in 26.2 ms.

   `audio.js` is an IIFE that touches `window` in exactly two places
   (`audio.js:29` and `:311`), and the first is inside the engine's `init()`, so
   a bare object is enough of a browser to load it. No DOM, no AudioContext.
============================================================================ */
"use strict";

const fs = require("fs");
const path = require("path");
const vm = require("vm");

const AUDIO_JS = path.join(__dirname, "..", "audio.js");

globalThis.window = {};
vm.runInThisContext(fs.readFileSync(AUDIO_JS, "utf8"), { filename: AUDIO_JS });

if (!globalThis.window.FB_AUDIO || typeof globalThis.window.FB_AUDIO.exportMIDI !== "function") {
  console.error("verify-midi.js: audio.js did not publish window.FB_AUDIO.exportMIDI");
  process.exit(2);
}

async function main() {
  const chunks = [];
  for await (const chunk of process.stdin) chunks.push(chunk);

  const lines = Buffer.concat(chunks).toString("utf8").split("\n").filter((l) => l.trim());
  const out = [];

  for (const line of lines) {
    const blob = globalThis.window.FB_AUDIO.exportMIDI(JSON.parse(line));
    out.push(Buffer.from(await blob.arrayBuffer()).toString("hex"));
  }

  process.stdout.write(out.join("\n") + "\n");
}

main().catch((e) => {
  console.error("verify-midi.js: " + e.stack);
  process.exit(2);
});
