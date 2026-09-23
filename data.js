/* ============================================================================
   FORRÓ BOX — data layer
   Instruments, accent colors, regional profiles, default grooves.
   ----------------------------------------------------------------------------
   Pattern notation: S("9..5 ..6. 8..4 ..6.")
     '.'   = step off
     '1'-'9' = velocity level (×14 → 14..126), spaces ignored.
   16 chars = one bar of sixteenths.
============================================================================ */
(function () {
  function S(str) {
    return [...str.replace(/\s/g, "")].map((c) =>
      c === "." ? 0 : Math.min(127, parseInt(c, 10) * 14)
    );
  }

  // ── Instruments ───────────────────────────────────────────────────────────
  const INSTRUMENTS = [
    { id: "zabumba",   name: "ZABUMBA",   color: "#E8650A", anchor: true },
    { id: "triangulo", name: "TRIÂNGULO", color: "#00C2C7" },
    { id: "pandeiro",  name: "PANDEIRO",  color: "#F2C200" },
    { id: "ganza",     name: "GANZÁ",     color: "#7ABF6E" },
    { id: "bateria",   name: "BATERIA",   color: "#E84646",
      subs: [
        { id: "bb",  name: "BB",  full: "Bumbo"   },
        { id: "cx",  name: "CX",  full: "Caixa"   },
        { id: "hh",  name: "HH",  full: "Chimbal" },
        { id: "tom", name: "TOM", full: "Surdo"   },
      ] },
  ];

  // Default per-channel control values.
  const CHANNEL_DEFAULTS = {
    zabumba:   { vol: 82, pitch: 0,  decay: 58, pan: 0,   ghost: 12 },
    triangulo: { vol: 68, pitch: 0,  decay: 40, pan: 22,  ghost: 8  },
    pandeiro:  { vol: 72, pitch: 0,  decay: 46, pan: -18, ghost: 14 },
    ganza:     { vol: 64, pitch: 0,  decay: 30, pan: 12,  ghost: 6  },
    bateria:   { vol: 74, pitch: 0,  decay: 50, pan: 0,   ghost: 10 },
  };

  // ── Regional profiles ──────────────────────────────────────────────────────
  // Each profile is a complete starting point: tempo, feel, timbre, mutes, grooves.
  const PROFILES = {
    // GENERATED — this block and PROFILE_ORDER below, from
    // assets/profiles.json, by scripts/build-profiles.py. Edit the JSON.
    // Everything else in this file is hand-written and authoritative.
    campina: {
      id: "campina", name: "CAMPINA GRANDE", short: "CAMPINA", code: "CAM",
      desc: ["Pé-de-serra puro — sanfona, zabumba e triângulo.",
             "Swing médio, balanço solto.",
             "Timbre HI-FI, bateria em silêncio."],
      bpm: 132, swing: 38, cachaca: 22, timbre: "hifi",
      muted: { bateria: true },
      patterns: {
        zabumba:   "9..5 ..6. 8..4 ..6.",
        triangulo: "7474 7474 7474 7474",
        pandeiro:  "..6. 9..4 ..6. 9..5",
        ganza:     "6363 6363 6363 6363",
        bb:        "9... .... 9... ....",
        cx:        ".... 9... .... 9...",
        hh:        ".5.5 .5.5 .5.5 .5.5",
        tom:       ".... .... .... ..4.",
      },
      grooves: [
        { id: "pe-de-serra-01", name: "PÉ-DE-SERRA 01",
          bpm: 132, swing: 38, cachaca: 22,
          patterns: {
            zabumba:   "9..5 ..6. 8..4 ..6.",
            triangulo: "7474 7474 7474 7474",
            pandeiro:  "..6. 9..4 ..6. 9..5",
            ganza:     "6363 6363 6363 6363",
            bb:        "9... .... 9... ....",
            cx:        ".... 9... .... 9...",
            hh:        ".5.5 .5.5 .5.5 .5.5",
            tom:       ".... .... .... ..4.",
          } },
      ],
    },
    caruaru: {
      id: "caruaru", name: "CARUARU", short: "CARUARU", code: "CAR",
      desc: ["Forró tradicional pernambucano.",
             "Peso extra na zabumba, swing alto.",
             "Timbre HI-FI, balanço pesado."],
      bpm: 138, swing: 54, cachaca: 32, timbre: "hifi",
      muted: {},
      patterns: {
        zabumba:   "9..6 .57. 9..6 .47.",
        triangulo: "7575 7575 7575 7575",
        pandeiro:  "..7. 9..5 ..7. 9..6",
        ganza:     "7474 7474 7474 7474",
        bb:        "9... ..6. 9... ..6.",
        cx:        ".... 9..3 .... 9..4",
        hh:        "6.6. 6.6. 6.6. 6.6.",
        tom:       ".... ...4 .... ..5.",
      },
      grooves: [
        { id: "tradicional-01", name: "TRADICIONAL 01",
          bpm: 138, swing: 54, cachaca: 32,
          patterns: {
            zabumba:   "9..6 .57. 9..6 .47.",
            triangulo: "7575 7575 7575 7575",
            pandeiro:  "..7. 9..5 ..7. 9..6",
            ganza:     "7474 7474 7474 7474",
            bb:        "9... ..6. 9... ..6.",
            cx:        ".... 9..3 .... 9..4",
            hh:        "6.6. 6.6. 6.6. 6.6.",
            tom:       ".... ...4 .... ..5.",
          } },
      ],
    },
    petrolina: {
      id: "petrolina", name: "PETROLINA", short: "PETROLINA", code: "PET",
      desc: ["Forró eletrônico do São Francisco.",
             "Bateria presente, groove seco.",
             "Timbre LO-FI, cachaça baixa."],
      bpm: 128, swing: 26, cachaca: 16, timbre: "lofi",
      muted: {},
      patterns: {
        zabumba:   "9... 9..4 9... 9..6",
        triangulo: "5.5. 5.5. 5.5. 5.5.",
        pandeiro:  ".... 7..3 .... 7..4",
        ganza:     "8484 8484 8484 8484",
        bb:        "9... ..5. 9..4 ....",
        cx:        ".... 9... .... 9...",
        hh:        "6868 6868 6868 6868",
        tom:       ".... .... ...5 ..6.",
      },
      grooves: [
        { id: "forro-eletrico", name: "FORRÓ ELÉTRICO",
          bpm: 128, swing: 26, cachaca: 16,
          patterns: {
            zabumba:   "9... 9..4 9... 9..6",
            triangulo: "5.5. 5.5. 5.5. 5.5.",
            pandeiro:  ".... 7..3 .... 7..4",
            ganza:     "8484 8484 8484 8484",
            bb:        "9... ..5. 9..4 ....",
            cx:        ".... 9... .... 9...",
            hh:        "6868 6868 6868 6868",
            tom:       ".... .... ...5 ..6.",
          } },
      ],
    },
    sp: {
      id: "sp", name: "UNIVERSITÁRIO", short: "UNIV", code: "UNI",
      desc: ["Forró universitário, limpo e pop.",
             "Quantizado, cachaça quase zero.",
             "Timbre HI-FI, pulso reto."],
      bpm: 124, swing: 16, cachaca: 6, timbre: "hifi",
      muted: {},
      patterns: {
        zabumba:   "9... 6... 9... 6...",
        triangulo: "8888 8888 8888 8888",
        pandeiro:  "..7. ..7. ..7. ..7.",
        ganza:     "7575 7575 7575 7575",
        bb:        "9... .... 9... ....",
        cx:        ".... 9... .... 9...",
        hh:        ".7.7 .7.7 .7.7 .7.7",
        tom:       ".... .... .... ....",
      },
      grooves: [
        { id: "universitario-01", name: "UNIVERSITÁRIO 01",
          bpm: 124, swing: 16, cachaca: 6,
          patterns: {
            zabumba:   "9... 6... 9... 6...",
            triangulo: "8888 8888 8888 8888",
            pandeiro:  "..7. ..7. ..7. ..7.",
            ganza:     "7575 7575 7575 7575",
            bb:        "9... .... 9... ....",
            cx:        ".... 9... .... 9...",
            hh:        ".7.7 .7.7 .7.7 .7.7",
            tom:       ".... .... .... ....",
          } },
      ],
    },
  };

  const PROFILE_ORDER = ["campina", "caruaru", "petrolina", "sp"];

  const TIMBRES = {
    hifi: { id: "hifi", name: "HI-FI", sub: "Limpo, encorpado" },
    lofi: { id: "lofi", name: "LO-FI", sub: "Fita, 12-bit" },
    ciclo: { id: "ciclo", name: "CICLOTRON™", sub: "TOTAL DISTORTION™" },
  };

  // Build a fully-expanded groove (per-step velocity arrays) from a profile.
  function buildGroove(profileId, steps) {
    const p = PROFILES[profileId];
    const expand = (key) => {
      const base = S(p.patterns[key]); // length 16
      const out = new Array(steps).fill(0);
      for (let i = 0; i < steps; i++) out[i] = base[i % 16];
      return out;
    };
    return {
      zabumba:   expand("zabumba"),
      triangulo: expand("triangulo"),
      pandeiro:  expand("pandeiro"),
      ganza:     expand("ganza"),
      bateria: {
        bb:  expand("bb"),
        cx:  expand("cx"),
        hh:  expand("hh"),
        tom: expand("tom"),
      },
    };
  }

  // ── Presets (header cycler) ────────────────────────────────────────────────
  const PRESETS = [
    "PÉ-DE-SERRA 01", "BAIÃO SECO", "XOTE LENTO", "ARRASTA-PÉ",
    "XAXADO 88", "FORRÓ ELÉTRICO", "QUADRILHA", "PISADINHA",
  ];

  window.FB_DATA = {
    S, INSTRUMENTS, CHANNEL_DEFAULTS, PROFILES, PROFILE_ORDER,
    TIMBRES, PRESETS, buildGroove,
  };
})();
