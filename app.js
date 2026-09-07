/* ============================================================================
   FORRÓ BOX — application
   State, DOM, sequencer, transport clock, humanisation, wiring.
============================================================================ */
(function () {
  const { INSTRUMENTS, CHANNEL_DEFAULTS, PROFILES, PROFILE_ORDER, TIMBRES,
          PRESETS, buildGroove } = window.FB_DATA;
  const { Engine, exportMIDI } = window.FB_AUDIO;
  const { Knob, Fader } = window.FB_CONTROLS;

  const engine = new Engine();
  const el = (html) => { const t = document.createElement("template"); t.innerHTML = html.trim(); return t.content.firstElementChild; };
  const $ = (s, r = document) => r.querySelector(s);

  // ── state ───────────────────────────────────────────────────────────────────
  const state = {
    bpm: 132, playing: false, sync: false, steps: 16, currentStep: -1,
    swing: 38, cachaca: 22, presetIdx: 0,
    activeProfile: "campina", dirty: false,
    timbre: "hifi", charMix: 40,
    limiterOn: true, outputMode: "stereo", master: 82,
    isolated: null, bateriaOpen: false,
    channels: {},
    grid: null,
  };
  INSTRUMENTS.forEach((i) => {
    const d = CHANNEL_DEFAULTS[i.id];
    state.channels[i.id] = { ...d, mute: false, solo: false, pattern: 1 };
  });

  const refs = {};        // holds knob instances etc.

  // ── build window ─────────────────────────────────────────────────────────────
  function build() {
    const win = $("#fb-window");
    win.innerHTML = "";
    win.append(buildHeader(), buildMain(), buildSeq(), buildFooter(), buildSubview());
  }

  // ---- HEADER ----
  function buildHeader() {
    const h = el(`<div class="header"></div>`);

    const left = el(`<div style="display:flex;align-items:center;gap:12px"></div>`);
    const logo = el(`<div class="logo-lockup"></div>`);
    logo.innerHTML = `
      <svg class="logo-mark" viewBox="0 0 46 34" aria-hidden="true">
        <g class="lm-sanfona">
          <path d="M3 10 L13 7.5 L13 26.5 L3 29 Z"/>
          <path d="M6 9.2 V27.8 M9 8.4 V27 M12 7.6 V26.2"/>
        </g>
        <ellipse class="lm-zabumba" cx="24.5" cy="18.5" rx="8.8" ry="8.8"/>
        <path class="lm-zabumba-rods" d="M24.5 9.7 V27.3 M15.7 18.5 H33.3"/>
        <g class="lm-triangulo">
          <path d="M31 6 L41.5 24 L23.5 24"/>
          <line x1="35.5" y1="13.5" x2="41" y2="10.5"/>
        </g>
      </svg>
      <div class="wordmark">FORRÓ<span class="dot">·</span>BOX</div>`;
    left.append(logo);

    const bpmCluster = el(`<div class="bpm-cluster"></div>`);
    const bpm = el(`<div class="bpm mono" title="Arraste ou clique para editar">${state.bpm}<small> BPM</small></div>`);
    refs.bpm = bpm;
    const sync = el(`<button class="btn" id="sync">SYNC</button>`);
    const mini = el(`<div class="mini-btns"><button class="mini-btn" id="half">÷2</button><button class="mini-btn" id="dbl">×2</button></div>`);
    bpmCluster.append(bpm, sync, mini);
    left.append(bpmCluster);

    const transport = el(`<div class="transport"></div>`);
    const play = el(`<button class="tp-btn play" id="play" title="Tocar/Parar"><svg viewBox="0 0 24 24"><path d="M7 5v14l12-7z" fill="currentColor"/></svg></button>`);
    const stop = el(`<button class="tp-btn" id="stop" title="Parar"><svg viewBox="0 0 24 24"><rect x="6" y="6" width="12" height="12" fill="currentColor"/></svg></button>`);
    transport.append(play, stop);
    refs.play = play;

    const sp1 = el(`<div style="flex:1"></div>`);
    const sp2 = el(`<div style="flex:1"></div>`);

    const gk = el(`<div class="global-knobs"></div>`);
    const swingWrap = el(`<div class="gk"></div>`);
    const swingKnob = new Knob({ min: 0, max: 100, value: state.swing, size: 54, color: "var(--fg)", onChange: (v) => { state.swing = v; refs.swingRead.textContent = Math.round(v); markCustom(); } });
    refs.swingKnob = swingKnob;
    refs.swingRead = el(`<div class="gk-read mono">${state.swing}</div>`);
    const swingMeta = el(`<div class="gk-meta"></div>`);
    swingMeta.append(el(`<div class="gk-name">SWING</div>`), refs.swingRead);
    swingWrap.append(swingKnob.el, swingMeta);

    const cWrap = el(`<div class="gk"></div>`);
    const cKnob = new Knob({ min: 0, max: 100, value: state.cachaca, size: 54, color: "var(--c-zabumba)", onChange: (v) => { state.cachaca = v; refs.cachacaRead.textContent = Math.round(v); updateDrunk(); markCustom(); } });
    refs.cachacaKnob = cKnob;
    refs.cachacaRead = el(`<div class="gk-read mono">${state.cachaca}</div>`);
    refs.cachacaName = el(`<div class="gk-name">CACHAÇA</div>`);
    const cMeta = el(`<div class="gk-meta"></div>`);
    cMeta.append(refs.cachacaName, refs.cachacaRead);
    cWrap.append(cKnob.el, cMeta);
    gk.append(swingWrap, el(`<div class="gk-divider"></div>`), cWrap);

    const right = el(`<div style="display:flex;align-items:center;gap:10px"></div>`);
    const preset = el(`<div class="preset"></div>`);
    const pprev = el(`<button class="arrow-btn">‹</button>`);
    const pscreen = el(`<div class="pscreen mono">${PRESETS[state.presetIdx]}</div>`);
    const pnext = el(`<button class="arrow-btn">›</button>`);
    refs.pscreen = pscreen;
    pprev.onclick = () => cyclePreset(-1);
    pnext.onclick = () => cyclePreset(1);
    preset.append(pprev, pscreen, pnext);

    const qsGroup = el(`<div class="style-switch"></div>`);
    qsGroup.append(el(`<div class="style-label">STYLE</div>`));
    const qs = el(`<div class="quick-switch" role="tablist"></div>`);
    PROFILE_ORDER.forEach((id) => {
      const b = el(`<button class="qs-btn" data-pid="${id}" title="${PROFILES[id].name}">${PROFILES[id].code}</button>`);
      b.onclick = () => loadProfile(id, true);
      qs.append(b);
    });
    refs.qs = qs;
    qsGroup.append(qs);
    right.append(preset, qsGroup);

    h.append(left, transport, sp1, gk, sp2, right);

    // wiring
    sync.onclick = () => { state.sync = !state.sync; sync.classList.toggle("on", state.sync); };
    $("#half", mini) || null;
    mini.querySelector("#half").onclick = () => setBPM(Math.max(40, Math.round(state.bpm / 2)));
    mini.querySelector("#dbl").onclick = () => setBPM(Math.min(300, state.bpm * 2));
    play.onclick = () => togglePlay();
    stop.onclick = () => stopPlay();
    // bpm drag + click-type
    wireBPM(bpm);

    return h;
  }

  function wireBPM(node) {
    let drag = false, sy = 0, sv = 0;
    node.addEventListener("mousedown", (e) => { drag = true; sy = e.clientY; sv = state.bpm; e.preventDefault();
      const mv = (ev) => { if (!drag) return; setBPM(Math.max(40, Math.min(300, Math.round(sv + (sy - ev.clientY) * 0.5)))); };
      const up = () => { drag = false; window.removeEventListener("mousemove", mv); window.removeEventListener("mouseup", up); };
      window.addEventListener("mousemove", mv); window.addEventListener("mouseup", up);
    });
    node.addEventListener("dblclick", () => { const v = prompt("BPM:", state.bpm); if (v != null) { const n = parseInt(v, 10); if (!isNaN(n)) setBPM(Math.max(40, Math.min(300, n))); } });
    node.addEventListener("wheel", (e) => { e.preventDefault(); setBPM(Math.max(40, Math.min(300, state.bpm - Math.sign(e.deltaY)))); }, { passive: false });
  }
  function setBPM(v) { state.bpm = v; refs.bpm.innerHTML = `${v}<small> BPM</small>`; }

  // ---- MATRIX ----
  function buildMain() {
    const main = el(`<div class="main"></div>`);
    const matrix = el(`<div class="matrix"></div>`);
    INSTRUMENTS.forEach((inst) => matrix.append(buildStrip(inst)));
    main.append(matrix, buildSide());
    refs.main = main;
    return main;
  }

  function buildStrip(inst) {
    const ch = state.channels[inst.id];
    const strip = el(`<div class="strip${inst.anchor ? " anchor" : ""}" style="--c:${inst.color}"></div>`);
    const idx = INSTRUMENTS.indexOf(inst) + 1;
    const head = el(`<div class="strip-head"><div class="strip-name">${inst.name}</div><div class="strip-head-r"><div class="trig-led"></div><div class="strip-idx mono">0${idx}</div></div></div>`);
    strip.append(head);
    strip.append(el(`<div class="accent-bar"></div>`));

    // sample slot (prominent) + live hit visualizer
    strip.append(el(`<div class="sample-slot"><div class="sample-name">${sampleName(inst)}</div><button class="load-btn">LOAD</button></div>`));
    const viz = el(`<div class="hitviz"><div class="hv-fill"></div><div class="hv-ticks"></div></div>`);
    strip.append(viz);
    refs[`viz_${inst.id}`] = { fill: viz.querySelector(".hv-fill"), led: head.querySelector(".trig-led"), level: 0 };
    strip.append(el(`<div class="strip-div"></div>`));

    // knobs
    const kg = el(`<div class="knob-grid"></div>`);
    const mk = (label, opts, key) => {
      const k = new Knob({ size: 32, color: inst.color, label, onChange: (v) => { ch[key] = v; pushChannel(inst.id); markCustom(); }, ...opts });
      refs[`k_${inst.id}_${key}`] = k;
      return k.el;
    };
    kg.append(
      mk("VOL", { min: 0, max: 100, value: ch.vol }, "vol"),
      mk("PITCH", { min: -12, max: 12, value: ch.pitch, bipolar: true, fmt: (v) => (v > 0 ? "+" : "") + Math.round(v) }, "pitch"),
      mk("DECAY", { min: 0, max: 100, value: ch.decay }, "decay"),
      mk("PAN", { min: -50, max: 50, value: ch.pan, bipolar: true, fmt: (v) => v === 0 ? "C" : (v < 0 ? "L" : "R") + Math.abs(Math.round(v)) }, "pan"),
    );
    strip.append(kg);
    strip.append(el(`<div class="strip-div"></div>`));

    // pattern
    const pr = el(`<div class="pattern-row"></div>`);
    const pp = el(`<button class="arrow-btn">‹</button>`);
    const ps = el(`<div class="pat-screen mono">PAT ${String(ch.pattern).padStart(2, "0")}</div>`);
    const pn = el(`<button class="arrow-btn">›</button>`);
    pp.onclick = () => { ch.pattern = ((ch.pattern + 6) % 8) + 1; ps.textContent = `PAT ${String(ch.pattern).padStart(2, "0")}`; markCustom(); };
    pn.onclick = () => { ch.pattern = (ch.pattern % 8) + 1; ps.textContent = `PAT ${String(ch.pattern).padStart(2, "0")}`; markCustom(); };
    pr.append(pp, ps, pn);
    strip.append(pr);

    // mute / solo
    const ms = el(`<div class="ms-row"></div>`);
    const mute = el(`<button class="ms-btn mute">M</button>`);
    const solo = el(`<button class="ms-btn solo">S</button>`);
    mute.onclick = () => { ch.mute = !ch.mute; mute.classList.toggle("on", ch.mute); syncSeqRows(); };
    solo.onclick = () => { ch.solo = !ch.solo; solo.classList.toggle("on", ch.solo); syncSeqRows(); };
    refs[`mute_${inst.id}`] = mute; refs[`solo_${inst.id}`] = solo;
    ms.append(mute, solo);
    strip.append(ms);

    // ghost prob
    const gr = el(`<div class="ghost-row"></div>`);
    const gl = el(`<div class="gl"><span>Ghost Prob</span><b class="mono">${ch.ghost}%</b></div>`);
    gr.append(gl);
    const gf = new Fader({ min: 0, max: 100, value: ch.ghost, color: inst.color, onChange: (v) => { ch.ghost = Math.round(v); gl.querySelector("b").textContent = Math.round(v) + "%"; markCustom(); } });
    gr.append(gf.el);
    strip.append(gr);

    // bateria subdots
    if (inst.subs) {
      const sd = el(`<div class="subdots" title="Abrir kit"></div>`);
      inst.subs.forEach((s, i) => sd.append(el(`<div class="subdot" style="--sd:${subColor(i)}"></div>`)));
      sd.append(el(`<div class="subdots-label">BB · CX · HH · TOM ↗</div>`));
      sd.onclick = () => openSubview(true);
      strip.append(sd);
    }
    return strip;
  }

  const SUBCOLORS = ["#E84646", "#F2887A", "#F4B6AE", "#9b2f2f"];
  function subColor(i) { return SUBCOLORS[i]; }
  function sampleName(inst) {
    return { zabumba: "Couro Aberto", triangulo: "Aço Aberto", pandeiro: "Pandeiro Médio", ganza: "Ganzá Seco", bateria: "Kit Minimal" }[inst.id];
  }

  // hit visualizer: pulse a strip's LED + activity meter on trigger, decay each frame
  function vizHit(id, vel) {
    const v = refs[`viz_${id}`];
    if (!v) return;
    v.level = Math.max(v.level, vel);
  }
  // JS-driven pad flash (robust against CSS-animation throttling)
  const flashReg = new Map();
  function flashPad(pad, strength = 1.4, dur = 240) {
    if (pad) flashReg.set(pad, { start: performance.now(), dur, strength });
  }
  function vizLoop() {
    INSTRUMENTS.forEach((inst) => {
      const v = refs[`viz_${inst.id}`];
      if (!v) return;
      if (v.level > 0.001) {
        v.fill.style.transform = `scaleX(${v.level.toFixed(3)})`;
        v.fill.style.opacity = (0.35 + v.level * 0.65).toFixed(3);
        v.led.style.opacity = (0.25 + v.level * 0.75).toFixed(3);
        v.led.style.boxShadow = `0 0 ${(2 + v.level * 7).toFixed(1)}px var(--c)`;
        v.level *= 0.82;
      } else if (v.level !== 0) {
        v.level = 0;
        v.fill.style.transform = "scaleX(0)";
        v.fill.style.opacity = "0";
        v.led.style.opacity = "";
        v.led.style.boxShadow = "";
      }
    });
    const now = performance.now();
    flashReg.forEach((f, pad) => {
      const t = (now - f.start) / f.dur;
      if (t >= 1) { pad.style.filter = ""; flashReg.delete(pad); }
      else pad.style.filter = `brightness(${(1 + f.strength * (1 - t)).toFixed(3)})`;
    });
    requestAnimationFrame(vizLoop);
  }

  // ---- SIDE PANEL ----
  function buildSide() {
    const side = el(`<div class="side"></div>`);

    // profiles
    const ps = el(`<div class="side-sect"></div>`);
    ps.append(el(`<div style="display:flex;justify-content:space-between;align-items:center"><div class="sect-label">REGIONAL PROFILES</div><div class="custom-tag mono" id="custom-tag">CUSTOM</div></div>`));
    const list = el(`<div class="profiles"></div>`);
    PROFILE_ORDER.forEach((id) => {
      const p = PROFILES[id];
      const b = el(`<button class="profile" data-pid="${id}"><div class="pf-name">${p.name}</div><div class="pf-desc">${p.desc.join(" ")}</div></button>`);
      b.onclick = () => loadProfile(id, true);
      list.append(b);
    });
    refs.profileList = list;
    ps.append(list);
    side.append(ps);

    // timbre
    const ts = el(`<div class="side-sect"></div>`);
    ts.append(el(`<div class="sect-label">TIMBRE / CONVOLUTION</div>`));
    const opts = el(`<div class="timbre-opts"></div>`);
    Object.values(TIMBRES).forEach((t) => {
      const row = el(`<div class="timbre${t.id === "ciclo" ? " ciclo" : ""}" data-tid="${t.id}"><div><div class="tb-name">${t.name}</div><div class="tb-sub">${t.sub}</div></div><div class="tb-led"></div></div>`);
      row.onclick = () => setTimbre(t.id, true);
      opts.append(row);
    });
    refs.timbreOpts = opts;
    ts.append(opts);

    const mix = el(`<div class="mix-row"></div>`);
    const mk = new Knob({ min: 0, max: 100, value: state.charMix, size: 28, color: "var(--c-triangulo)", label: "MIX", onChange: (v) => { state.charMix = v; engine.setTimbre(state.timbre, v / 100); markCustom(); } });
    const mkWrap = el(`<div class="mr-knob"></div>`); mkWrap.append(mk.el);
    mix.append(mkWrap, el(`<button class="btn ir-btn">LOAD IR…</button>`));
    ts.append(mix);
    side.append(ts);

    // bundle
    side.append(el(`<div class="bundle"><div class="bdot"></div><div class="btxt mono">BUNDLE: <b>MINIMAL</b></div></div>`));
    return side;
  }

  // ---- SEQUENCER ----
  function buildSeq() {
    const seq = el(`<div class="seq"></div>`);
    const head = el(`<div class="seq-head"></div>`);
    const left = el(`<div class="sh-left"></div>`);
    left.append(el(`<div class="sect-label">SEQUENCER</div>`));
    left.append(el(`<div class="seq-len" id="iso-hint">CLIQUE O NOME P/ ISOLAR</div>`));
    const lenctl = el(`<div class="seq-len"></div>`);
    const b16 = el(`<button class="btn steps-btn" data-n="16">16</button>`);
    const b32 = el(`<button class="btn steps-btn" data-n="32">32</button>`);
    b16.onclick = () => setSteps(16); b32.onclick = () => setSteps(32);
    refs.b16 = b16; refs.b32 = b32;
    lenctl.append(el(`<span>STEPS</span>`), b16, b32);
    head.append(left, lenctl);
    seq.append(head);

    const wrap = el(`<div class="seq-grid-wrap"></div>`);
    INSTRUMENTS.forEach((inst) => {
      const row = el(`<div class="seq-row" data-iid="${inst.id}" style="--c:${inst.color}"></div>`);
      const label = el(`<div class="seq-rowlabel"><div class="rl-chip"></div>${inst.name}</div>`);
      label.onclick = () => toggleIsolate(inst.id);
      const pads = el(`<div class="pads"></div>`);
      row.append(label, pads);
      wrap.append(row);
    });
    const playhead = el(`<div class="playhead"></div>`);
    wrap.append(playhead);
    refs.playhead = playhead; refs.seqWrap = wrap;
    seq.append(wrap);
    return seq;
  }

  function renderPads() {
    const wrap = refs.seqWrap;
    INSTRUMENTS.forEach((inst) => {
      const row = wrap.querySelector(`.seq-row[data-iid="${inst.id}"]`);
      const pads = row.querySelector(".pads");
      pads.innerHTML = "";
      pads.style.gridTemplateColumns = `repeat(${state.steps}, 1fr)`;
      for (let i = 0; i < state.steps; i++) {
        const pad = el(`<div class="pad${i % 4 === 0 ? " beat" : ""}" data-step="${i}"></div>`);
        pad.style.setProperty("--c", inst.color);
        applyPadVisual(pad, inst.id, i);
        pad.onclick = () => togglePad(inst.id, i);
        pad.addEventListener("animationend", () => pad.classList.remove("pad-flash", "pressed"));
        pads.append(pad);
      }
    });
    layoutPlayhead();
    syncSeqRows();
  }

  function cellVal(iid, step) {
    if (iid === "bateria") {
      const b = state.grid.bateria;
      return Math.max(b.bb[step], b.cx[step], b.hh[step], b.tom[step]);
    }
    return state.grid[iid][step];
  }
  function applyPadVisual(pad, iid, step) {
    const v = cellVal(iid, step);
    pad.classList.toggle("on", v > 0);
    pad.classList.remove("ghost");
    if (v > 0) {
      const b = 0.32 + (v / 127) * 0.68;
      pad.style.opacity = String(b);
      if (v <= 42) pad.classList.add("ghost");
    } else {
      pad.style.opacity = "1";
    }
  }
  function refreshPad(iid, step) {
    const pad = refs.seqWrap.querySelector(`.seq-row[data-iid="${iid}"] .pad[data-step="${step}"]`);
    if (pad) applyPadVisual(pad, iid, step);
  }

  function togglePad(iid, step) {
    if (iid === "bateria") {
      // collapsed row edits caixa (cx) — the backbeat; deep edits live in the kit view
      const cx = state.grid.bateria.cx;
      cx[step] = cx[step] > 0 ? 0 : 100;
    } else {
      const arr = state.grid[iid];
      arr[step] = arr[step] > 0 ? 0 : 100;
    }
    const pad = refs.seqWrap.querySelector(`.seq-row[data-iid="${iid}"] .pad[data-step="${step}"]`);
    if (pad) { pad.classList.remove("pressed"); void pad.offsetWidth; pad.classList.add("pressed"); }
    refreshPad(iid, step);
    markCustom();
  }

  // ---- FOOTER ----
  function buildFooter() {
    const f = el(`<div class="footer"></div>`);
    const mg = el(`<div class="foot-group"></div>`);
    mg.append(el(`<div class="foot-label">MASTER</div>`));
    const mf = new Fader({ min: 0, max: 100, value: state.master, color: "var(--fg)", onChange: (v) => { state.master = v; engine.setMaster(v / 100); } });
    const mfw = el(`<div class="master-fader"></div>`); mfw.append(mf.el);
    mg.append(mfw);
    f.append(mg);

    const lg = el(`<div class="foot-group"></div>`);
    const lim = el(`<button class="btn on" id="limiter">LIMITER</button>`);
    lim.onclick = () => { state.limiterOn = !state.limiterOn; lim.classList.toggle("on", state.limiterOn); engine.setLimiter(state.limiterOn); };
    const gr = el(`<div class="gr-meter"><div class="gr-fill" id="gr-fill"></div></div>`);
    refs.grFill = $("#gr-fill", gr);
    lg.append(lim, gr);
    f.append(lg);

    const dm = el(`<div class="drag-midi" id="drag-midi" draggable="true"><span class="dm-arrow">↓</span><span class="dm-text">DRAG MIDI</span><span class="dm-sub">.mid</span></div>`);
    wireDragMidi(dm);
    f.append(dm);

    const og = el(`<div class="foot-group" style="margin-left:auto"></div>`);
    og.append(el(`<div class="foot-label">OUTPUT</div>`));
    const ot = el(`<div class="out-toggle"><button class="ot on" data-m="stereo">STEREO</button><button class="ot" data-m="multi">MULTI-OUT</button></div>`);
    ot.querySelectorAll(".ot").forEach((b) => b.onclick = () => {
      state.outputMode = b.dataset.m;
      ot.querySelectorAll(".ot").forEach((x) => x.classList.toggle("on", x === b));
    });
    og.append(ot);
    f.append(og);
    return f;
  }

  function wireDragMidi(node) {
    const makeBlob = () => exportMIDI(state);
    node.addEventListener("dragstart", (e) => {
      const blob = makeBlob();
      const url = URL.createObjectURL(blob);
      const name = `forrobox_${state.activeProfile || "custom"}_${state.bpm}bpm.mid`;
      try { e.dataTransfer.setData("DownloadURL", `audio/midi:${name}:${url}`); } catch (_) {}
      e.dataTransfer.setData("text/plain", name);
      e.dataTransfer.effectAllowed = "copy";
      node.classList.add("hot");
    });
    node.addEventListener("dragend", () => node.classList.remove("hot"));
    node.addEventListener("click", () => {
      const blob = makeBlob();
      const url = URL.createObjectURL(blob);
      const a = document.createElement("a");
      a.href = url; a.download = `forrobox_${state.activeProfile || "custom"}_${state.bpm}bpm.mid`;
      a.click(); setTimeout(() => URL.revokeObjectURL(url), 1000);
    });
  }

  // ---- SUBVIEW (bateria kit) ----
  function buildSubview() {
    const ov = el(`<div class="subview" id="subview"></div>`);
    const panel = el(`<div class="subview-panel"></div>`);
    const head = el(`<div class="subview-head"><h3><span class="sh-accent">BATERIA</span> · KIT</h3><button class="subclose">✕</button></div>`);
    head.querySelector(".subclose").onclick = () => openSubview(false);
    panel.append(head);
    panel.append(el(`<div class="subview-sub">Sequencie cada peça do kit. As batidas aparecem somadas na linha BATERIA do sequenciador principal.</div>`));
    const rows = el(`<div class="sub-rows"></div>`);
    INSTRUMENTS[4].subs.forEach((s, i) => {
      const row = el(`<div class="sub-row" data-sid="${s.id}" style="--c:${subColor(i)}"></div>`);
      row.append(el(`<div class="sub-rowlabel"><div class="srl-name">${s.name}</div><div class="srl-full">${s.full}</div></div>`));
      const pads = el(`<div class="pads"></div>`);
      row.append(pads);
      rows.append(row);
    });
    panel.append(rows);
    refs.subRows = rows;
    ov.append(panel);
    ov.addEventListener("click", (e) => { if (e.target === ov) openSubview(false); });
    return ov;
  }

  function renderSubPads() {
    const rows = refs.subRows;
    INSTRUMENTS[4].subs.forEach((s) => {
      const row = rows.querySelector(`.sub-row[data-sid="${s.id}"]`);
      const pads = row.querySelector(".pads");
      pads.innerHTML = "";
      pads.style.gridTemplateColumns = `repeat(${state.steps}, 1fr)`;
      for (let i = 0; i < state.steps; i++) {
        const arr = state.grid.bateria[s.id];
        const pad = el(`<div class="pad${i % 4 === 0 ? " beat" : ""}${arr[i] > 0 ? " on" : ""}" data-step="${i}"></div>`);
        pad.style.setProperty("--c", "var(--c-bateria)");
        if (arr[i] > 0) pad.style.opacity = String(0.32 + arr[i] / 127 * 0.68);
        pad.onclick = () => { arr[i] = arr[i] > 0 ? 0 : 100; pad.classList.toggle("on", arr[i] > 0); pad.style.opacity = arr[i] > 0 ? "1" : "1"; refreshPad("bateria", i); markCustom(); };
        pad.addEventListener("animationend", () => pad.classList.remove("pad-flash", "pressed"));
        pads.append(pad);
      }
    });
  }

  function openSubview(on) {
    state.bateriaOpen = on;
    $("#subview").classList.toggle("on", on);
    if (on) renderSubPads();
  }

  // ── isolate / solo / mute visuals ────────────────────────────────────────────
  function toggleIsolate(iid) {
    state.isolated = state.isolated === iid ? null : iid;
    syncSeqRows();
  }
  function syncSeqRows() {
    INSTRUMENTS.forEach((inst) => {
      const row = refs.seqWrap.querySelector(`.seq-row[data-iid="${inst.id}"]`);
      const ch = state.channels[inst.id];
      const iso = state.isolated;
      row.classList.toggle("isolated", iso === inst.id);
      row.classList.toggle("dimmed", (iso && iso !== inst.id) || ch.mute);
    });
  }

  // ── profiles / presets / timbre ──────────────────────────────────────────────
  function loadProfile(id, flash) {
    const p = PROFILES[id];
    state.activeProfile = id; state.dirty = false;
    setBPM(p.bpm);
    state.swing = p.swing; refs.swingKnob.set(p.swing, false); refs.swingRead.textContent = p.swing;
    state.cachaca = p.cachaca; refs.cachacaKnob.set(p.cachaca, false); refs.cachacaRead.textContent = p.cachaca;
    state.grid = buildGroove(id, state.steps);
    // mutes
    INSTRUMENTS.forEach((inst) => {
      const ch = state.channels[inst.id];
      ch.mute = !!(p.muted && p.muted[inst.id]); ch.solo = false;
      refs[`mute_${inst.id}`].classList.toggle("on", ch.mute);
      refs[`solo_${inst.id}`].classList.remove("on");
    });
    setTimbre(p.timbre, false);
    renderPads();
    if (state.bateriaOpen) renderSubPads();
    updateProfileUI();
    updateDrunk();
    if (flash) flashPads();
  }

  function flashPads() {
    refs.seqWrap.querySelectorAll(".pad.on").forEach((p) => flashPad(p, 1.6, 340));
  }

  function markCustom() {
    if (!state.activeProfile) return;
    if (!state.dirty) { state.dirty = true; updateProfileUI(); }
  }
  function updateProfileUI() {
    refs.profileList.querySelectorAll(".profile").forEach((b) =>
      b.classList.toggle("active", b.dataset.pid === state.activeProfile && !state.dirty));
    refs.qs.querySelectorAll(".qs-btn").forEach((b) =>
      b.classList.toggle("on", b.dataset.pid === state.activeProfile && !state.dirty));
    $("#custom-tag").classList.toggle("on", state.dirty);
  }

  function cyclePreset(d) {
    state.presetIdx = (state.presetIdx + d + PRESETS.length) % PRESETS.length;
    refs.pscreen.textContent = PRESETS[state.presetIdx];
    markCustom();
  }

  function setTimbre(id, custom) {
    state.timbre = id;
    refs.timbreOpts.querySelectorAll(".timbre").forEach((r) => r.classList.toggle("active", r.dataset.tid === id));
    engine.setTimbre(id, state.charMix / 100);
    $("#fb-window").classList.toggle("ciclo-on", id === "ciclo");
    if (custom) markCustom();
  }

  function pushChannel(id) {
    const ch = state.channels[id];
    engine.setChannel(id, { vol: ch.vol / 100, pitch: ch.pitch, decay: ch.decay / 100, pan: ch.pan / 50 });
  }
  function pushAllChannels() { INSTRUMENTS.forEach((i) => pushChannel(i.id)); }

  function setSteps(n) {
    if (state.steps === n) return;
    const old = state.steps; state.steps = n;
    const tile = (arr) => { const out = new Array(n); for (let i = 0; i < n; i++) out[i] = arr[i % old]; return out; };
    ["zabumba", "triangulo", "pandeiro", "ganza"].forEach((k) => state.grid[k] = tile(state.grid[k]));
    ["bb", "cx", "hh", "tom"].forEach((k) => state.grid.bateria[k] = tile(state.grid.bateria[k]));
    refs.b16.classList.toggle("on", n === 16); refs.b32.classList.toggle("on", n === 32);
    renderPads();
    if (state.bateriaOpen) renderSubPads();
  }

  // ── drunk easter egg ─────────────────────────────────────────────────────────
  function updateDrunk() {
    const win = $("#fb-window");
    const c = state.cachaca;
    const d = Math.max(0, Math.min(1, (c - 65) / 35));
    win.style.setProperty("--drunk", d.toFixed(3));
    const tipsy = c >= 88;
    win.classList.toggle("tipsy", tipsy);
    if (refs.cachacaName) {
      refs.cachacaName.textContent = tipsy ? "♪ NO PONTO" : "CACHAÇA";
      refs.cachacaName.style.color = tipsy ? "var(--c-zabumba)" : "";
      refs.cachacaName.classList.toggle("drunk-on", tipsy);
    }
    if (refs.cachacaRead) refs.cachacaRead.style.color = tipsy ? "var(--c-zabumba)" : "";
  }

  // ── transport / scheduler ────────────────────────────────────────────────────
  let nextNoteTime = 0, schedStep = 0, timerId = null;
  const LOOKAHEAD = 0.1, INTERVAL = 25;

  function togglePlay() { state.playing ? stopPlay() : startPlay(); }
  function startPlay() {
    engine.init(); engine.resume();
    pushAllChannels();
    engine.setMaster(state.master / 100);
    engine.setLimiter(state.limiterOn);
    engine.setTimbre(state.timbre, state.charMix / 100);
    state.playing = true; refs.play.classList.add("on");
    schedStep = 0; nextNoteTime = engine.now() + 0.06;
    timerId = setInterval(scheduler, INTERVAL);
  }
  function stopPlay() {
    state.playing = false; refs.play.classList.remove("on");
    clearInterval(timerId); timerId = null;
    refs.playhead.classList.remove("on");
    state.currentStep = -1;
    refs.seqWrap.querySelectorAll(".pad.playing").forEach((p) => p.classList.remove("playing"));
  }

  function anySolo() { return INSTRUMENTS.some((i) => state.channels[i.id].solo); }

  function scheduler() {
    const stepDur = 60 / state.bpm / 4;
    while (nextNoteTime < engine.now() + LOOKAHEAD) {
      const step = schedStep % state.steps;
      const swingDelay = (step % 2 === 1) ? (state.swing / 100) * 0.6 * stepDur : 0;
      const jitter = (state.cachaca / 100) * 0.022 * (Math.random() * 2 - 1);
      const t = nextNoteTime + swingDelay + jitter;
      scheduleStep(step, Math.max(engine.now(), t));
      scheduleVisual(step, t, stepDur);
      nextNoteTime += stepDur;
      schedStep++;
    }
  }

  function scheduleStep(step, t) {
    const solo = anySolo();
    const cach = state.cachaca / 100;
    const play = (id, sub, vel) => {
      const ch = state.channels[id];
      if (ch.mute) return;
      if (solo && !ch.solo) return;
      let v = vel / 127;
      v *= (1 - cach * 0.25 * Math.random()); // velocity humanise
      engine.trigger(id, sub, t, v, { pitch: ch.pitch, decay: ch.decay / 100 });
    };
    const ghost = (id, sub) => {
      const ch = state.channels[id];
      if (ch.mute) return; if (solo && !ch.solo) return;
      const chance = (ch.ghost / 100) * (0.22 + cach * 0.6);
      if (Math.random() < chance) {
        const t2 = t + (Math.random() - 0.5) * 0.02;
        engine.trigger(id, sub, Math.max(engine.now(), t2), 0.2 + Math.random() * 0.12, { pitch: ch.pitch, decay: ch.decay / 100 });
      }
    };
    ["zabumba", "triangulo", "pandeiro", "ganza"].forEach((id) => {
      const v = state.grid[id][step];
      if (v > 0) play(id, null, v); else ghost(id, null);
    });
    ["bb", "cx", "hh", "tom"].forEach((s) => {
      const v = state.grid.bateria[s][step];
      if (v > 0) play("bateria", s, v); else if (s === "hh") ghost("bateria", s);
    });
  }

  function scheduleVisual(step, t, stepDur) {
    const delay = Math.max(0, (t - engine.now()) * 1000);
    setTimeout(() => {
      if (!state.playing) return;
      state.currentStep = step;
      movePlayhead(step, stepDur);
      // pad playing highlight + trigger flash
      const wrap = refs.seqWrap;
      wrap.querySelectorAll(".pad.playing").forEach((p) => p.classList.remove("playing"));
      INSTRUMENTS.forEach((inst) => {
        const pad = wrap.querySelector(`.seq-row[data-iid="${inst.id}"] .pad[data-step="${step}"]`);
        if (!pad) return;
        pad.classList.add("playing");
        const cv = cellVal(inst.id, step);
        if (cv > 0) {
          flashPad(pad, 0.9 + (cv / 127) * 0.9, 260);
          const ch = state.channels[inst.id];
          const audible = !ch.mute && (!anySolo() || ch.solo);
          if (audible) vizHit(inst.id, cv / 127);
        }
      });
      if (state.bateriaOpen) {
        refs.subRows.querySelectorAll(".pad.playing").forEach((p) => p.classList.remove("playing"));
        INSTRUMENTS[4].subs.forEach((s) => {
          const pad = refs.subRows.querySelector(`.sub-row[data-sid="${s.id}"] .pad[data-step="${step}"]`);
          if (pad) pad.classList.add("playing");
        });
      }
    }, delay);
  }

  let padGeom = null;
  function layoutPlayhead() {
    requestAnimationFrame(() => {
      const wrap = refs.seqWrap;
      const firstPads = wrap.querySelector(".seq-row .pads");
      if (!firstPads) return;
      const wr = wrap.getBoundingClientRect();
      const pr = firstPads.getBoundingClientRect();
      padGeom = { left: pr.left - wr.left, width: pr.width };
    });
  }
  function movePlayhead(step, stepDur) {
    if (!padGeom) layoutPlayhead();
    if (!padGeom) return;
    const ph = refs.playhead;
    ph.classList.add("on");
    const cellW = padGeom.width / state.steps;
    const x = padGeom.left + step * cellW + cellW / 2 - 1;
    ph.style.transition = `left ${(stepDur * 1000).toFixed(0)}ms linear`;
    ph.style.left = x + "px";
  }

  // ── scaling ──────────────────────────────────────────────────────────────────
  function scale() {
    const win = $("#fb-window");
    const s = Math.min(window.innerWidth / 1200, window.innerHeight / 780);
    win.style.setProperty("--scale-tf", `scale(${s})`);
    win.style.transform = `scale(${s})`;
    layoutPlayhead();
  }

  // ── tweaks bridge ────────────────────────────────────────────────────────────
  function applyTweaks(t) {
    const root = document.documentElement;
    if (t.theme) root.setAttribute("data-theme", t.theme);
    if (t.radius != null) root.style.setProperty("--r", t.radius + "px");
    if (t.accent != null) root.style.setProperty("--accent-i", (t.accent / 100).toFixed(2));
    if (t.mono) {
      const map = { plex: '"IBM Plex Mono", monospace', jetbrains: '"JetBrains Mono", monospace', space: '"Space Mono", monospace' };
      root.style.setProperty("--mono", map[t.mono] || map.plex);
    }
    if (t.defaultSteps && t.defaultSteps !== state.steps) setSteps(t.defaultSteps);
  }
  window.FB = { applyTweaks, state };

  // ── boot ─────────────────────────────────────────────────────────────────────
  function boot() {
    build();
    refs.b16.classList.add("on");
    loadProfile("campina", false);
    pushAllChannels();
    updateProfileUI();
    vizLoop();
    scale();
    window.addEventListener("resize", scale);
    if (window.ResizeObserver) {
      const ro = new ResizeObserver(() => scale());
      ro.observe(document.documentElement);
    }
    requestAnimationFrame(scale);
    setTimeout(scale, 120);
    setTimeout(scale, 400);
    window.addEventListener("load", scale);
    if (document.fonts && document.fonts.ready) document.fonts.ready.then(scale);
    // first interaction unlocks audio
    const unlock = () => { engine.init(); engine.resume(); window.removeEventListener("pointerdown", unlock); };
    window.addEventListener("pointerdown", unlock);
    // spacebar play/stop
    window.addEventListener("keydown", (e) => {
      if (e.code === "Space" && e.target === document.body) { e.preventDefault(); togglePlay(); }
    });
    document.body.tabIndex = -1;
  }

  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", boot);
  else boot();
})();
