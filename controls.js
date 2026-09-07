/* ============================================================================
   FORRÓ BOX — controls
   Flat minimal Knob (single indicator line + arc) and thin Fader.
   Interactions: drag (vertical), shift = fine, double-click = type,
   right-click = reset to default, hover = value tooltip.
============================================================================ */
(function () {
  const NS = "http://www.w3.org/2000/svg";

  // shared tooltip
  let tip;
  function tooltip() {
    if (!tip) {
      tip = document.createElement("div");
      tip.className = "fb-tip";
      document.body.appendChild(tip);
    }
    return tip;
  }
  function showTip(x, y, text) {
    const t = tooltip();
    t.textContent = text;
    t.style.left = x + "px";
    t.style.top = y + "px";
    t.classList.add("on");
  }
  function hideTip() { if (tip) tip.classList.remove("on"); }

  class Knob {
    constructor(opts) {
      this.min = opts.min ?? 0;
      this.max = opts.max ?? 100;
      this.step = opts.step ?? 1;
      this.value = opts.value ?? this.min;
      this.def = opts.value ?? this.min;
      this.size = opts.size ?? 32;
      this.color = opts.color || "var(--fg)";
      this.label = opts.label || "";
      this.fmt = opts.fmt || ((v) => Math.round(v));
      this.onChange = opts.onChange || (() => {});
      this.bipolar = opts.bipolar || false;
      this.el = this._build();
    }

    _build() {
      const wrap = document.createElement("div");
      wrap.className = "fb-knob";
      wrap.style.setProperty("--ksize", this.size + "px");
      wrap.tabIndex = 0;
      wrap.setAttribute("role", "slider");
      wrap.setAttribute("aria-label", this.label);

      const sv = document.createElementNS(NS, "svg");
      sv.setAttribute("viewBox", "0 0 100 100");
      sv.classList.add("fb-knob-svg");

      const trackArc = document.createElementNS(NS, "path");
      trackArc.setAttribute("class", "fb-knob-track");
      const valArc = document.createElementNS(NS, "path");
      valArc.setAttribute("class", "fb-knob-val");
      valArc.style.stroke = this.color;
      const line = document.createElementNS(NS, "line");
      line.setAttribute("class", "fb-knob-line");
      line.setAttribute("x1", "50"); line.setAttribute("y1", "50");
      line.setAttribute("x2", "50"); line.setAttribute("y2", "16");

      const hub = document.createElementNS(NS, "circle");
      hub.setAttribute("cx", "50"); hub.setAttribute("cy", "50"); hub.setAttribute("r", "30");
      hub.setAttribute("class", "fb-knob-hub");

      sv.append(trackArc, hub, valArc, line);
      wrap.append(sv);

      if (this.label) {
        const lab = document.createElement("div");
        lab.className = "fb-knob-label";
        lab.textContent = this.label;
        wrap.append(lab);
      }

      this._line = line;
      this._valArc = valArc;
      this._trackArc = trackArc;
      this._drawStatic();
      this._render();
      this._wire(wrap);
      return wrap;
    }

    _arcPath(a0, a1, r) {
      const p = (a) => {
        const rad = (a - 90) * Math.PI / 180;
        return [50 + r * Math.cos(rad), 50 + r * Math.sin(rad)];
      };
      const [x0, y0] = p(a0);
      const [x1, y1] = p(a1);
      const large = Math.abs(a1 - a0) > 180 ? 1 : 0;
      const sweep = a1 > a0 ? 1 : 0;
      return `M ${x0} ${y0} A ${r} ${r} 0 ${large} ${sweep} ${x1} ${y1}`;
    }

    _drawStatic() {
      this.A0 = -135; this.A1 = 135;
      this._trackArc.setAttribute("d", this._arcPath(this.A0, this.A1, 38));
    }

    _norm() { return (this.value - this.min) / (this.max - this.min); }

    _render() {
      const n = this._norm();
      const ang = this.A0 + n * (this.A1 - this.A0);
      this._line.setAttribute("transform", `rotate(${ang} 50 50)`);
      if (this.bipolar) {
        const mid = (this.A0 + this.A1) / 2; // 0
        this._valArc.setAttribute("d", this._arcPath(Math.min(mid, ang), Math.max(mid, ang), 38));
      } else {
        this._valArc.setAttribute("d", this._arcPath(this.A0, ang, 38));
      }
      this.el && this.el.setAttribute("aria-valuenow", this.fmt(this.value));
    }

    set(v, fire = true) {
      v = Math.max(this.min, Math.min(this.max, v));
      v = Math.round(v / this.step) * this.step;
      this.value = v;
      this._render();
      if (fire) this.onChange(this.value);
    }

    _wire(wrap) {
      let dragging = false, startY = 0, startV = 0;
      const range = this.max - this.min;

      const move = (e) => {
        if (!dragging) return;
        const fine = e.shiftKey ? 0.18 : 1;
        const dy = startY - e.clientY;
        const dv = (dy / 160) * range * fine;
        this.set(startV + dv);
        showTip(e.clientX + 12, e.clientY - 10, this.fmt(this.value));
      };
      const up = () => {
        dragging = false;
        document.body.classList.remove("fb-grabbing");
        window.removeEventListener("mousemove", move);
        window.removeEventListener("mouseup", up);
        hideTip();
      };
      wrap.addEventListener("mousedown", (e) => {
        if (e.button !== 0) return;
        e.preventDefault();
        dragging = true; startY = e.clientY; startV = this.value;
        document.body.classList.add("fb-grabbing");
        window.addEventListener("mousemove", move);
        window.addEventListener("mouseup", up);
        showTip(e.clientX + 12, e.clientY - 10, this.fmt(this.value));
      });
      wrap.addEventListener("wheel", (e) => {
        e.preventDefault();
        const fine = e.shiftKey ? 0.2 : 1;
        this.set(this.value - Math.sign(e.deltaY) * this.step * (e.shiftKey ? 1 : Math.max(1, range / 50)) * fine);
        const r = wrap.getBoundingClientRect();
        showTip(r.right + 8, r.top, this.fmt(this.value));
        clearTimeout(this._wt); this._wt = setTimeout(hideTip, 700);
      }, { passive: false });
      wrap.addEventListener("dblclick", (e) => {
        e.preventDefault();
        const raw = prompt(this.label + " — valor:", this.fmt(this.value));
        if (raw != null) { const n = parseFloat(raw); if (!isNaN(n)) this.set(n); }
      });
      wrap.addEventListener("contextmenu", (e) => {
        e.preventDefault();
        this.set(this.def);
        const r = wrap.getBoundingClientRect();
        showTip(r.right + 8, r.top, "reset");
        clearTimeout(this._wt); this._wt = setTimeout(hideTip, 600);
      });
      wrap.addEventListener("mouseenter", () => {
        const r = wrap.getBoundingClientRect();
        showTip(r.left + r.width / 2, r.top - 6, this.fmt(this.value));
      });
      wrap.addEventListener("mouseleave", () => { if (!dragging) hideTip(); });
      wrap.addEventListener("keydown", (e) => {
        if (e.key === "ArrowUp" || e.key === "ArrowRight") { this.set(this.value + this.step); e.preventDefault(); }
        if (e.key === "ArrowDown" || e.key === "ArrowLeft") { this.set(this.value - this.step); e.preventDefault(); }
      });
    }
  }

  class Fader {
    constructor(opts) {
      this.min = opts.min ?? 0;
      this.max = opts.max ?? 100;
      this.value = opts.value ?? this.min;
      this.color = opts.color || "var(--fg)";
      this.vertical = opts.vertical || false;
      this.onChange = opts.onChange || (() => {});
      this.fmt = opts.fmt || ((v) => Math.round(v));
      this.el = this._build();
    }
    _build() {
      const w = document.createElement("div");
      w.className = "fb-fader" + (this.vertical ? " vert" : "");
      const track = document.createElement("div"); track.className = "fb-fader-track";
      const fill = document.createElement("div"); fill.className = "fb-fader-fill";
      fill.style.background = this.color;
      const thumb = document.createElement("div"); thumb.className = "fb-fader-thumb";
      track.append(fill, thumb);
      w.append(track);
      this._track = track; this._fill = fill; this._thumb = thumb;
      this._render();
      this._wire(w);
      return w;
    }
    _norm() { return (this.value - this.min) / (this.max - this.min); }
    _render() {
      const n = this._norm();
      if (this.vertical) {
        this._fill.style.height = (n * 100) + "%";
        this._thumb.style.bottom = `calc(${n * 100}% - 5px)`;
      } else {
        this._fill.style.width = (n * 100) + "%";
        this._thumb.style.left = `calc(${n * 100}% - 5px)`;
      }
    }
    set(v, fire = true) {
      v = Math.max(this.min, Math.min(this.max, v));
      this.value = v; this._render();
      if (fire) this.onChange(this.value);
    }
    _wire(w) {
      const fromEvent = (e) => {
        const r = this._track.getBoundingClientRect();
        let n;
        if (this.vertical) n = 1 - (e.clientY - r.top) / r.height;
        else n = (e.clientX - r.left) / r.width;
        n = Math.max(0, Math.min(1, n));
        this.set(this.min + n * (this.max - this.min));
      };
      let drag = false;
      const move = (e) => drag && fromEvent(e);
      const up = () => { drag = false; window.removeEventListener("mousemove", move); window.removeEventListener("mouseup", up); hideTip(); };
      w.addEventListener("mousedown", (e) => {
        e.preventDefault(); drag = true; fromEvent(e);
        window.addEventListener("mousemove", move); window.addEventListener("mouseup", up);
      });
    }
  }

  window.FB_CONTROLS = { Knob, Fader };
})();
