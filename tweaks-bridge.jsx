/* Tweaks bridge — small React island that renders the Tweaks panel and pushes
   values into the vanilla Forró Box app via window.FB.applyTweaks(). */
const TWEAK_DEFAULTS = /*EDITMODE-BEGIN*/{
  "theme": "dark",
  "accent": 100,
  "radius": 2,
  "mono": "plex",
  "defaultSteps": 16
}/*EDITMODE-END*/;

function ForroTweaks() {
  const [t, setTweak] = useTweaks(TWEAK_DEFAULTS);

  // apply on mount + whenever values change
  React.useEffect(() => {
    if (window.FB && window.FB.applyTweaks) window.FB.applyTweaks(t);
  }, [t.theme, t.accent, t.radius, t.mono, t.defaultSteps]);

  return (
    <TweaksPanel title="Tweaks">
      <TweakSection label="Chassis" />
      <TweakRadio label="Theme" value={t.theme} options={[
        { value: "dark", label: "Dark" }, { value: "light", label: "OP-1 Light" },
      ]} onChange={(v) => setTweak("theme", v)} />
      <TweakRadio label="Corner radius" value={t.radius} options={[
        { value: 0, label: "0 · hard" }, { value: 2, label: "2px" },
      ]} onChange={(v) => setTweak("radius", v)} />

      <TweakSection label="Color" />
      <TweakSlider label="Accent intensity" value={t.accent} min={35} max={100} unit="%"
                   onChange={(v) => setTweak("accent", v)} />

      <TweakSection label="Type" />
      <TweakSelect label="Display font" value={t.mono} options={[
        { value: "plex", label: "IBM Plex Mono" },
        { value: "jetbrains", label: "JetBrains Mono" },
        { value: "space", label: "Space Mono" },
      ]} onChange={(v) => setTweak("mono", v)} />

      <TweakSection label="Sequencer" />
      <TweakRadio label="Default steps" value={t.defaultSteps} options={[
        { value: 16, label: "16" }, { value: 32, label: "32" },
      ]} onChange={(v) => setTweak("defaultSteps", v)} />
    </TweaksPanel>
  );
}

(function mount() {
  const start = () => {
    const host = document.createElement("div");
    host.id = "fb-tweaks-root";
    document.body.appendChild(host);
    ReactDOM.createRoot(host).render(<ForroTweaks />);
    // apply defaults immediately so theme/font/etc are right on load
    if (window.FB && window.FB.applyTweaks) window.FB.applyTweaks(TWEAK_DEFAULTS);
  };
  if (window.FB) start();
  else window.addEventListener("load", start);
})();
