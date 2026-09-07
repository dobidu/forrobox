/* ============================================================================
   FORRÓ BOX — audio engine
   Synthesised percussion (no samples), per-channel routing, global timbre
   character (HI-FI / LO-FI / CICLOTRON), limiter, and Standard MIDI export.
============================================================================ */
(function () {
  const semi = (n) => Math.pow(2, n / 12);

  class Engine {
    constructor() {
      this.ctx = null;
      this.ready = false;
      this.master = null;
      this.limiterNode = null;
      this.limiterOn = true;
      this.channels = {};      // id -> { gain, pan, params }
      this.charDry = null;     // dry/wet for timbre character
      this.charWet = null;
      this.shaper = null;
      this.charFilter = null;
      this.timbre = "hifi";
      this.charMix = 0.4;
      this._grPoll = 0;
      this.grReduction = 0;    // dB, for the meter
    }

    init() {
      if (this.ready) return;
      const AC = window.AudioContext || window.webkitAudioContext;
      const ctx = new AC();
      this.ctx = ctx;

      // master chain: sum -> character -> limiter -> masterGain -> out
      this.charInput = ctx.createGain();
      this.charDry = ctx.createGain();
      this.charWet = ctx.createGain();
      this.shaper = ctx.createWaveShaper();
      this.charFilter = ctx.createBiquadFilter();
      this.charFilter.type = "lowpass";
      this.charFilter.frequency.value = 18000;

      this.charInput.connect(this.charDry);
      this.charInput.connect(this.charFilter);
      this.charFilter.connect(this.shaper);
      this.shaper.connect(this.charWet);

      const charSum = ctx.createGain();
      this.charDry.connect(charSum);
      this.charWet.connect(charSum);

      this.limiterNode = ctx.createDynamicsCompressor();
      this.limiterNode.threshold.value = -6;
      this.limiterNode.knee.value = 0;
      this.limiterNode.ratio.value = 20;
      this.limiterNode.attack.value = 0.002;
      this.limiterNode.release.value = 0.12;

      this.master = ctx.createGain();
      this.master.gain.value = 0.85;

      charSum.connect(this.limiterNode);
      this.limiterNode.connect(this.master);
      this.master.connect(ctx.destination);

      // per-channel buses
      ["zabumba", "triangulo", "pandeiro", "ganza", "bateria"].forEach((id) => {
        const g = ctx.createGain();
        const pan = ctx.createStereoPanner();
        g.connect(pan);
        pan.connect(this.charInput);
        this.channels[id] = { gain: g, pan, vol: 0.8, pitch: 0, decay: 0.5, panV: 0 };
      });

      this.setTimbre(this.timbre, this.charMix);
      this.ready = true;
      this._pollGR();
    }

    resume() { if (this.ctx && this.ctx.state === "suspended") this.ctx.resume(); }

    _pollGR() {
      const tick = () => {
        if (this.limiterNode) {
          const r = this.limiterOn ? -this.limiterNode.reduction : 0;
          this.grReduction = r;
        }
        this._grPoll = requestAnimationFrame(tick);
      };
      tick();
    }

    setMaster(v01) { if (this.master) this.master.gain.value = v01 * v01; }

    setLimiter(on) {
      this.limiterOn = on;
      if (!this.limiterNode) return;
      // soften when "off" by raising threshold out of the way
      this.limiterNode.threshold.value = on ? -6 : 0;
      this.limiterNode.ratio.value = on ? 20 : 1;
    }

    setChannel(id, p) {
      const ch = this.channels[id];
      if (!ch) return;
      if (p.vol != null) { ch.vol = p.vol; ch.gain.gain.value = p.vol; }
      if (p.pitch != null) ch.pitch = p.pitch;
      if (p.decay != null) ch.decay = p.decay;
      if (p.pan != null) { ch.panV = p.pan; ch.pan.pan.value = Math.max(-1, Math.min(1, p.pan)); }
    }

    setTimbre(id, mix) {
      this.timbre = id;
      if (mix != null) this.charMix = mix;
      if (!this.ready) return;
      const m = this.charMix;
      // build waveshaper curve + filter per timbre
      const curve = new Float32Array(1024);
      let cut = 18000, drive = 1;
      if (id === "hifi") { cut = 16000; drive = 1.2; }
      else if (id === "lofi") { cut = 5200; drive = 2.4; }
      else if (id === "ciclo") { cut = 9000; drive = 9; }
      for (let i = 0; i < 1024; i++) {
        const x = (i / 1023) * 2 - 1;
        curve[i] = Math.tanh(x * drive);
      }
      this.shaper.curve = curve;
      this.charFilter.frequency.setTargetAtTime(cut, this.ctx.currentTime, 0.02);
      const wet = id === "hifi" ? m * 0.5 : m;
      this.charWet.gain.setTargetAtTime(wet, this.ctx.currentTime, 0.02);
      this.charDry.gain.setTargetAtTime(1 - wet * 0.5, this.ctx.currentTime, 0.02);
    }

    // ── voices ────────────────────────────────────────────────────────────────
    _noise(dur) {
      const ctx = this.ctx;
      const len = Math.max(1, Math.floor(ctx.sampleRate * dur));
      const buf = ctx.createBuffer(1, len, ctx.sampleRate);
      const d = buf.getChannelData(0);
      for (let i = 0; i < len; i++) d[i] = Math.random() * 2 - 1;
      const src = ctx.createBufferSource();
      src.buffer = buf;
      return src;
    }

    trigger(id, sub, time, vel, params) {
      if (!this.ready) return;
      const ch = this.channels[id];
      if (!ch) return;
      const t = time;
      const v = Math.max(0, Math.min(1, vel));
      const p = params || {};
      const pf = semi(p.pitch || 0);
      const dscale = 0.4 + (p.decay != null ? p.decay : 0.5) * 1.4;
      const out = ch.gain;

      const env = (node, peak, dur, attack = 0.001) => {
        const g = this.ctx.createGain();
        g.gain.setValueAtTime(0, t);
        g.gain.linearRampToValueAtTime(peak, t + attack);
        g.gain.exponentialRampToValueAtTime(0.0001, t + dur);
        node.connect(g);
        g.connect(out);
        return g;
      };

      if (id === "zabumba") {
        const dur = (0.16 + 0.18 * (v)) * dscale;
        const o = this.ctx.createOscillator();
        o.type = "sine";
        const f0 = 96 * pf, f1 = 46 * pf;
        o.frequency.setValueAtTime(f0, t);
        o.frequency.exponentialRampToValueAtTime(f1, t + dur * 0.6);
        env(o, v * 1.0, dur, 0.002);
        o.start(t); o.stop(t + dur + 0.02);
        // attack click
        const n = this._noise(0.012);
        const bp = this.ctx.createBiquadFilter(); bp.type = "bandpass"; bp.frequency.value = 1400;
        n.connect(bp); env(bp, v * 0.25, 0.02, 0.001); n.start(t); n.stop(t + 0.03);
      }
      else if (id === "triangulo") {
        const open = v > 0.55;
        const dur = (open ? 0.45 : 0.06) * dscale;
        const freqs = [5400, 6850, 8120, 9700, 11200];
        const sum = this.ctx.createGain();
        const bp = this.ctx.createBiquadFilter(); bp.type = "bandpass"; bp.frequency.value = 7600; bp.Q.value = 0.7;
        sum.connect(bp);
        freqs.forEach((f, i) => {
          const o = this.ctx.createOscillator();
          o.type = "square";
          o.frequency.value = f * pf * (1 + (Math.random() - 0.5) * 0.01);
          const g = this.ctx.createGain(); g.gain.value = 0.12 / (i + 1);
          o.connect(g); g.connect(sum); o.start(t); o.stop(t + dur + 0.02);
        });
        env(bp, v * 0.5, dur, 0.001);
      }
      else if (id === "pandeiro") {
        const dur = (0.10 + 0.10 * v) * dscale;
        // membrane
        const o = this.ctx.createOscillator();
        o.type = "sine"; o.frequency.setValueAtTime(330 * pf, t);
        o.frequency.exponentialRampToValueAtTime(180 * pf, t + dur * 0.7);
        env(o, v * 0.55, dur, 0.001); o.start(t); o.stop(t + dur + 0.02);
        // jingles
        const n = this._noise(dur + 0.05);
        const hp = this.ctx.createBiquadFilter(); hp.type = "highpass"; hp.frequency.value = 6500;
        n.connect(hp); env(hp, v * 0.4, dur * 0.9, 0.001); n.start(t); n.stop(t + dur + 0.05);
      }
      else if (id === "ganza") {
        const dur = (0.035 + 0.03 * v) * dscale;
        const n = this._noise(dur + 0.02);
        const bp = this.ctx.createBiquadFilter(); bp.type = "bandpass"; bp.frequency.value = 6800 * pf; bp.Q.value = 1.2;
        n.connect(bp); env(bp, v * 0.6, dur, 0.004); n.start(t); n.stop(t + dur + 0.02);
      }
      else if (id === "bateria") {
        const which = sub || "cx";
        if (which === "bb") {
          const dur = 0.14 * dscale;
          const o = this.ctx.createOscillator(); o.type = "sine";
          o.frequency.setValueAtTime(130 * pf, t);
          o.frequency.exponentialRampToValueAtTime(48 * pf, t + dur * 0.6);
          env(o, v * 1.0, dur, 0.001); o.start(t); o.stop(t + dur + 0.02);
        } else if (which === "cx") {
          const dur = 0.14 * dscale;
          const n = this._noise(dur + 0.02);
          const hp = this.ctx.createBiquadFilter(); hp.type = "highpass"; hp.frequency.value = 1700;
          n.connect(hp); env(hp, v * 0.6, dur, 0.001); n.start(t); n.stop(t + dur + 0.02);
          const o = this.ctx.createOscillator(); o.type = "triangle"; o.frequency.value = 190 * pf;
          env(o, v * 0.4, dur * 0.7, 0.001); o.start(t); o.stop(t + dur + 0.02);
        } else if (which === "hh") {
          const dur = (0.03 + 0.04 * v) * dscale;
          const n = this._noise(dur + 0.02);
          const hp = this.ctx.createBiquadFilter(); hp.type = "highpass"; hp.frequency.value = 9000;
          n.connect(hp); env(hp, v * 0.45, dur, 0.001); n.start(t); n.stop(t + dur + 0.02);
        } else if (which === "tom") {
          const dur = 0.2 * dscale;
          const o = this.ctx.createOscillator(); o.type = "sine";
          o.frequency.setValueAtTime(190 * pf, t);
          o.frequency.exponentialRampToValueAtTime(110 * pf, t + dur * 0.7);
          env(o, v * 0.8, dur, 0.001); o.start(t); o.stop(t + dur + 0.02);
        }
      }
    }

    now() { return this.ctx ? this.ctx.currentTime : 0; }
  }

  // ── MIDI export (Standard MIDI File, type 0) ────────────────────────────────
  // General-MIDI percussion mapping on channel 10.
  const GM = {
    zabumba: 36, triangulo: 81, pandeiro: 54, ganza: 82,
    bb: 36, cx: 38, hh: 42, tom: 45,
  };

  function vlq(n) {
    const bytes = [n & 0x7f]; n >>= 7;
    while (n) { bytes.unshift((n & 0x7f) | 0x80); n >>= 7; }
    return bytes;
  }
  function str(s) { return [...s].map((c) => c.charCodeAt(0)); }
  function u32(n) { return [(n >>> 24) & 255, (n >>> 16) & 255, (n >>> 8) & 255, n & 255]; }
  function u16(n) { return [(n >>> 8) & 255, n & 255]; }

  function exportMIDI(state) {
    const PPQ = 96;
    const stepTicks = PPQ / 4; // sixteenth
    const steps = state.steps;
    const events = []; // {tick, data:[]}

    const add = (tick, data) => events.push({ tick, data });
    const noteAt = (note, step, vel) => {
      const on = step * stepTicks;
      const off = on + Math.floor(stepTicks * 0.8);
      add(on, [0x99, note, Math.max(1, Math.min(127, Math.round(vel)))]);
      add(off, [0x89, note, 0]);
    };

    const chans = state.channels;
    ["zabumba", "triangulo", "pandeiro", "ganza"].forEach((id) => {
      if (chans[id].mute) return;
      const arr = state.grid[id];
      for (let i = 0; i < steps; i++) if (arr[i] > 0) noteAt(GM[id], i, arr[i]);
    });
    if (!chans.bateria.mute) {
      ["bb", "cx", "hh", "tom"].forEach((s) => {
        const arr = state.grid.bateria[s];
        for (let i = 0; i < steps; i++) if (arr[i] > 0) noteAt(GM[s], i, arr[i]);
      });
    }

    events.sort((a, b) => a.tick - b.tick);

    // tempo meta
    const usPerBeat = Math.round(60000000 / state.bpm);
    let track = [];
    track.push(...vlq(0), 0xff, 0x51, 0x03, (usPerBeat >> 16) & 255, (usPerBeat >> 8) & 255, usPerBeat & 255);
    track.push(...vlq(0), 0xff, 0x58, 0x04, 4, 2, PPQ / 4, 8); // 4/4

    let last = 0;
    events.forEach((e) => {
      track.push(...vlq(e.tick - last), ...e.data);
      last = e.tick;
    });
    track.push(...vlq(0), 0xff, 0x2f, 0x00); // end of track

    const header = [...str("MThd"), ...u32(6), ...u16(0), ...u16(1), ...u16(PPQ)];
    const trk = [...str("MTrk"), ...u32(track.length), ...track];
    const bytes = new Uint8Array([...header, ...trk]);
    return new Blob([bytes], { type: "audio/midi" });
  }

  window.FB_AUDIO = { Engine, exportMIDI };
})();
