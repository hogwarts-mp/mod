import { useEffect, useRef, useState, type CSSProperties } from "react";
import { T } from "./theme";
import { TABS } from "./data";
import { bridge } from "./bridge";
import { TopBar } from "./components/TopBar";
import { Slider } from "./components/Slider";
import { Stage } from "./components/Stage";
import { Finalise } from "./components/Finalise";

// Left control panel width (px). Used both for layout and to derive the camera's lateral
// shift so the character stays centered in the un-occluded viewport at any window size.
const PANEL_W = 460;

// Footer key-legend styles.
const keyChip: CSSProperties = { fontFamily: T.mono, fontSize: 10, color: T.goldSoft, padding: "3px 7px", border: `1px solid ${T.goldLine}`, borderRadius: 4 };
const legend: CSSProperties = { fontFamily: T.display, fontSize: 10, letterSpacing: ".16em", color: "#9c8f74" };

// Initial option indices (1-based) — mirror the design's defaults. Keys match data.ts:
// marking0 = Freckles/Moles, marking1 = Complexion, marking2 = Scars/Markings.
const DEFAULTS: Record<string, number> = {
  preset: 11, faceShape: 7, eyeColour: 12, glasses: 1,
  hairStyle: 23, hairColour: 9,
  skin: 5, marking0: 3, marking1: 1, marking2: 2,
  browShape: 6, browColour: 8,
};

export function App() {
  // Hidden until the host calls window.hmpCreator.open() (F5). While hidden we render
  // nothing, so the page stays transparent and the game keeps input + visibility.
  const [open, setOpen] = useState(false);
  const [tabId, setTabId] = useState(TABS[0].id);
  const [opts, setOpts] = useState<Record<string, number>>(DEFAULTS);

  // Finalise state
  const [voice, setVoice] = useState(1);
  const [pitch, setPitch] = useState(5);
  const [first, setFirst] = useState("");
  const [last, setLast] = useState("");
  const [dorm, setDorm] = useState<"witch" | "wizard" | null>(null);

  // Live (dev) framing override — tune in-game instead of alt-tabbing to edit data.ts.
  const [cam, setCam] = useState(TABS[0].camera);

  const tab = TABS.find((t) => t.id === tabId) ?? TABS[0];
  const isFinalise = !!tab.finalise;

  // Show/hide is host-driven: Creator::Open/Close evaluate window.hmpCreator.open/close.
  useEffect(() => {
    window.hmpCreator = { open: () => setOpen(true), close: () => setOpen(false) };
  }, []);

  // Escape closes the creator (host releases control lock).
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => { if (e.key === "Escape") bridge.close(); };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, []);

  // Reset the framing to the tab's default when switching tabs.
  useEffect(() => { setCam(tab.camera); }, [tabId]); // eslint-disable-line react-hooks/exhaustive-deps
  // Push the framing on open / tab change / resize. The lateral shift is DERIVED from the
  // live window width so the character stays centered in the visible (un-panelled) region
  // at any size; cam.shift is just a manual nudge added on top.
  useEffect(() => {
    if (!open) return;
    const push = () => {
      const panelFrac = Math.min(0.6, PANEL_W / Math.max(1, window.innerWidth));
      const autoShift = panelFrac * cam.dist * Math.tan((cam.fov * Math.PI) / 180 / 2);
      bridge.setCamera({ ...cam, shift: autoShift + cam.shift });
    };
    push();
    window.addEventListener("resize", push);
    return () => window.removeEventListener("resize", push);
  }, [open, cam]);

  // Hold the idle pose still while the creator is open (resumed by the host on close).
  useEffect(() => { if (open) bridge.setFreeze(true); }, [open]);

  // Hold A / D to rotate the character (smooth: rotate each frame while held).
  useEffect(() => {
    if (!open) return;
    const keys = new Set<string>();
    const down = (e: KeyboardEvent) => {
      // Don't hijack a/d (or spin the avatar) while the user is typing in a field, e.g. names.
      if (e.target instanceof HTMLInputElement) return;
      const k = e.key.toLowerCase();
      if (k === "a" || k === "d") { keys.add(k); e.preventDefault(); }
    };
    const up = (e: KeyboardEvent) => keys.delete(e.key.toLowerCase());
    const clear = () => keys.clear(); // a keyup lost to a focus change must not leave a key stuck
    let raf = 0;
    const tick = () => {
      if (keys.has("a")) bridge.rotate(-2.5);
      if (keys.has("d")) bridge.rotate(2.5);
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    window.addEventListener("keydown", down);
    window.addEventListener("keyup", up);
    window.addEventListener("blur", clear);
    return () => { cancelAnimationFrame(raf); window.removeEventListener("keydown", down); window.removeEventListener("keyup", up); window.removeEventListener("blur", clear); };
  }, [open]);

  const setOption = (key: string, next: number) => {
    setOpts((o) => ({ ...o, [key]: next }));
    bridge.setOption(key, next);
  };

  // Voice preview drives a facial-anim reflection call host-side; debounce so dragging the
  // pitch slider previews once on settle rather than firing a burst on every step crossed.
  const previewTimer = useRef<number>();
  const previewVoiceSoon = () => {
    clearTimeout(previewTimer.current);
    previewTimer.current = window.setTimeout(() => bridge.previewVoice(), 180);
  };

  const cycleTab = (dir: 1 | -1) => {
    const i = TABS.findIndex((t) => t.id === tabId);
    setTabId(TABS[(i + dir + TABS.length) % TABS.length].id);
  };

  const nameError = isFinalise && (!first.trim() || !last.trim());
  const canConfirm = !!first.trim() && !!last.trim() && dorm !== null;
  const summary = `HAIR ${opts.hairStyle} · EYES ${opts.eyeColour} · SKIN ${opts.skin}`;

  if (!open) return null; // host owns input + visibility until opened

  return (
    // Root is transparent so the right-hand stage is a true see-through viewport (live
    // avatar / game behind it). The chrome (top bar, left panel, footer) carries its own
    // opaque background so it stays readable over the game.
    <div style={{ height: "100vh", display: "flex", flexDirection: "column", overflow: "hidden", background: "transparent", fontFamily: T.body, color: T.ink }}>
      <TopBar tabs={TABS} activeId={tabId} title={tab.title} onSelect={setTabId} onPrev={() => cycleTab(-1)} onNext={() => cycleTab(1)} />

      <div style={{ flex: 1, display: "flex", minHeight: 0 }}>
        {/* LEFT: controls (opaque parchment) */}
        <div style={{ flex: "none", width: PANEL_W, padding: "26px 30px", overflowY: "auto", background: "radial-gradient(700px 520px at 30% -10%, rgba(201,162,90,.06), rgba(0,0,0,0) 60%), " + T.bg }}>
          {isFinalise ? (
            <Finalise
              voice={voice} onVoice={(t) => { setVoice(t); bridge.setVoice(t); previewVoiceSoon(); }}
              pitch={pitch} onPitch={(v) => { setPitch(v); bridge.setPitch(v); previewVoiceSoon(); }}
              first={first} last={last}
              onFirst={(v) => { setFirst(v); bridge.setName(v, last); }}
              onLast={(v) => { setLast(v); bridge.setName(first, v); }}
              nameError={nameError}
              dorm={dorm} onDorm={(d) => { setDorm(d); bridge.setDormitory(d); }}
            />
          ) : (
            tab.sections.map((sec) => (
              <div key={sec.key} style={{ marginBottom: 26 }}>
                <div style={{ display: "flex", alignItems: "center", gap: 14, marginBottom: 14 }}>
                  <div style={{ fontFamily: T.display, fontSize: 11, letterSpacing: ".24em", color: T.label }}>{sec.label.toUpperCase()}</div>
                  <div style={{ flex: 1, height: 1, background: "linear-gradient(90deg, rgba(201,162,90,.28), rgba(201,162,90,0))" }} />
                </div>
                <Slider value={opts[sec.key] ?? 1} max={sec.max} onChange={(v) => setOption(sec.key, v)} />
              </div>
            ))
          )}

          {/* Live camera tuning (dev) — adjust framing in-game; bake into data.ts when happy. */}
          <div style={{ marginTop: 30, paddingTop: 16, borderTop: `1px solid ${T.goldLineSoft}` }}>
            <div style={{ fontFamily: T.display, fontSize: 11, letterSpacing: ".24em", color: T.mutedDim, marginBottom: 10 }}>CAMERA · DEV</div>
            <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: "6px 10px" }}>
              {(["dist", "height", "pitch", "fov", "shift"] as const).map((k) => (
                <div key={k}>
                  <label style={{ display: "block", fontSize: 10, letterSpacing: ".1em", color: T.mutedDim, marginBottom: 2 }}>{k.toUpperCase()}</label>
                  <input
                    type="number"
                    value={cam[k]}
                    step={k === "fov" ? 1 : 5}
                    onChange={(e) => setCam((c) => ({ ...c, [k]: parseFloat(e.target.value) || 0 }))}
                    style={{ width: "100%", padding: "6px 8px", background: T.panel, border: `1px solid ${T.goldLine}`, borderRadius: 4, color: T.ink, fontSize: 13, outline: "none" }}
                  />
                </div>
              ))}
            </div>
            <div style={{ fontSize: 10, color: T.mutedDim, marginTop: 8 }}>Tweak live, then tell me the values to bake into data.ts.</div>
          </div>
        </div>

        {/* RIGHT: live avatar stage (transparent viewport over the live avatar) */}
        <Stage title={tab.title} summary={summary} isFinalise={isFinalise} canConfirm={canConfirm} onConfirm={bridge.confirm} />
      </div>

      {/* footer */}
      <div style={{ flex: "none", height: 44, display: "flex", alignItems: "center", justifyContent: "space-between", padding: "0 26px", borderTop: `1px solid ${T.goldLineSoft}`, background: "#0c0906" }}>
        <span style={{ fontFamily: T.mono, fontSize: 10, letterSpacing: ".14em", color: T.mutedDim }}>HogwartsMP</span>
        <div style={{ display: "flex", alignItems: "center", gap: 20 }}>
          <span style={{ display: "flex", alignItems: "center", gap: 6 }}>
            <span style={keyChip}>A</span>
            <span style={keyChip}>D</span>
            <span style={legend}>ROTATE</span>
          </span>
          <span style={{ display: "flex", alignItems: "center", gap: 8 }}>
            <span style={keyChip}>ESC</span>
            <span style={legend}>BACK</span>
          </span>
        </div>
      </div>
    </div>
  );
}
