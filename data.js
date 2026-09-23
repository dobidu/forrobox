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
        { id: "baiao-seco", name: "BAIÃO SECO",
          bpm: 116, swing: 30, cachaca: 18,
          patterns: {
            zabumba:   "9... ..6. 9... ..5.",
            triangulo: "74.4 74.4 74.4 74.4",
            pandeiro:  "..5. 8... ..5. 8..3",
            ganza:     "5.53 5.53 5.53 5.53",
            bb:        "9... .... 9... ....",
            cx:        ".... 8... .... 8...",
            hh:        ".4.4 .4.4 .4.4 .4.4",
            tom:       ".... .... .... ..3.",
          } },
        { id: "xote-lento", name: "XOTE LENTO",
          bpm: 92, swing: 44, cachaca: 26,
          patterns: {
            zabumba:   "9..4 .5.. 8..4 .5..",
            triangulo: "7.5. 7.5. 7.5. 7.5.",
            pandeiro:  "..6. ...4 ..6. ...5",
            ganza:     "6.4. 6.4. 6.4. 6.4.",
            bb:        "9... .... 8... ....",
            cx:        ".... 7... .... 7...",
            hh:        ".4.. .4.. .4.. .4..",
            tom:       ".... .... .... .3..",
          } },
        { id: "arrasta-pe", name: "ARRASTA-PÉ",
          bpm: 160, swing: 22, cachaca: 30,
          patterns: {
            zabumba:   "9.6. 9.5. 9.6. 9.4.",
            triangulo: "8888 8888 8888 8888",
            pandeiro:  ".7.5 .7.5 .7.5 .7.6",
            ganza:     "7676 7676 7676 7676",
            bb:        "9... 9... 9... 9...",
            cx:        "..8. ..8. ..8. ..8.",
            hh:        "6.6. 6.6. 6.6. 6.6.",
            tom:       ".... ...4 .... ...5",
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
        { id: "xaxado-88", name: "XAXADO 88",
          bpm: 108, swing: 18, cachaca: 20,
          patterns: {
            zabumba:   "9..6 9..5 9..6 9..4",
            triangulo: "7.7. 7.7. 7.7. 7.7.",
            pandeiro:  ".... 8..4 .... 8..5",
            ganza:     "6.6. 6.6. 6.6. 6.6.",
            bb:        "9... 9... 9... 9...",
            cx:        "..7. ..7. ..7. ..7.",
            hh:        "5.5. 5.5. 5.5. 5.5.",
            tom:       ".... .... ..4. ....",
          } },
        { id: "baiao-pesado", name: "BAIÃO PESADO",
          bpm: 132, swing: 50, cachaca: 36,
          patterns: {
            zabumba:   "9..7 .68. 9..7 .58.",
            triangulo: "7676 7676 7676 7676",
            pandeiro:  "..8. 9..6 ..8. 9..7",
            ganza:     "7575 7575 7575 7575",
            bb:        "9..4 ..7. 9..4 ..6.",
            cx:        ".... 9..4 .... 9..5",
            hh:        "7.7. 7.7. 7.7. 7.7.",
            tom:       ".... ...5 .... ..6.",
          } },
        { id: "quadrilha", name: "QUADRILHA",
          bpm: 152, swing: 20, cachaca: 24,
          patterns: {
            zabumba:   "9.5. 9.5. 9.5. 9.6.",
            triangulo: "8.8. 8.8. 8.8. 8.8.",
            pandeiro:  ".6.6 .6.6 .6.6 .6.7",
            ganza:     "8686 8686 8686 8686",
            bb:        "9... 9... 9... 9...",
            cx:        ".... 8... .... 8...",
            hh:        "6666 6666 6666 6666",
            tom:       ".... .... .... .5..",
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
        { id: "pisadinha", name: "PISADINHA",
          bpm: 136, swing: 14, cachaca: 10,
          patterns: {
            zabumba:   "9..4 .9.. 9..4 .9.5",
            triangulo: "5..5 5..5 5..5 5..5",
            pandeiro:  ".... 6..4 .... 6..5",
            ganza:     "8.8. 8.8. 8.8. 8.8.",
            bb:        "9... 9... 9... 9...",
            cx:        ".... 9... .... 9...",
            hh:        "7878 7878 7878 7878",
            tom:       ".... ...4 .... ..5.",
          } },
        { id: "xote-eletrico", name: "XOTE ELÉTRICO",
          bpm: 104, swing: 34, cachaca: 18,
          patterns: {
            zabumba:   "9..5 .6.. 8..5 .6..",
            triangulo: "6.6. 6.6. 6.6. 6.6.",
            pandeiro:  "..5. ...3 ..5. ...4",
            ganza:     "7.5. 7.5. 7.5. 7.5.",
            bb:        "9... .... 8... ....",
            cx:        ".... 8... .... 8...",
            hh:        "5.5. 5.5. 5.5. 5.5.",
            tom:       ".... .... ...4 ....",
          } },
        { id: "vaquejada", name: "VAQUEJADA",
          bpm: 140, swing: 22, cachaca: 14,
          patterns: {
            zabumba:   "9.69 ..5. 9.69 ..4.",
            triangulo: "7.77 7.77 7.77 7.77",
            pandeiro:  ".5.. 8..4 .5.. 8..5",
            ganza:     "8484 8484 8484 8484",
            bb:        "9..4 .... 9..4 ....",
            cx:        ".... 9..3 .... 9..4",
            hh:        "6.66 6.66 6.66 6.66",
            tom:       ".... .... .... ..5.",
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
        { id: "xote-pop", name: "XOTE POP",
          bpm: 100, swing: 24, cachaca: 10,
          patterns: {
            zabumba:   "9..3 7... 9..3 7...",
            triangulo: "8.8. 8.8. 8.8. 8.8.",
            pandeiro:  "..6. ..6. ..6. ..7.",
            ganza:     "6.6. 6.6. 6.6. 6.6.",
            bb:        "9... .... 9... ....",
            cx:        ".... 8... .... 8...",
            hh:        ".6.6 .6.6 .6.6 .6.6",
            tom:       ".... .... .... ....",
          } },
        { id: "forro-pop", name: "FORRÓ POP",
          bpm: 132, swing: 12, cachaca: 8,
          patterns: {
            zabumba:   "9.5. 9.5. 9.5. 9.5.",
            triangulo: "88.8 88.8 88.8 88.8",
            pandeiro:  ".7.5 .7.5 .7.5 .7.5",
            ganza:     "7777 7777 7777 7777",
            bb:        "9... 9... 9... 9...",
            cx:        ".... 9... .... 9...",
            hh:        "7.7. 7.7. 7.7. 7.7.",
            tom:       ".... .... .... ...4",
          } },
        { id: "pe-de-serra-pop", name: "PÉ-DE-SERRA POP",
          bpm: 118, swing: 20, cachaca: 12,
          patterns: {
            zabumba:   "9..4 ..7. 8..4 ..6.",
            triangulo: "7.74 7.74 7.74 7.74",
            pandeiro:  "..6. 8..4 ..6. 8..5",
            ganza:     "6464 6464 6464 6464",
            bb:        "9... .... 9... ....",
            cx:        ".... 8... .... 8...",
            hh:        ".6.6 .6.6 .6.6 .6.6",
            tom:       ".... .... .... ..4.",
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
