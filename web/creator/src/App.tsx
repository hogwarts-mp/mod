import { useEffect, useState } from "react";
import { T } from "./theme";
import { TABS } from "./data";
import { bridge } from "./bridge";
import { TopBar } from "./components/TopBar";
import { Slider } from "./components/Slider";
import { Stage } from "./components/Stage";
import { Finalise } from "./components/Finalise";

// Initial option indices (1-based) — mirror the design's defaults.
const DEFAULTS: Record<string, number> = {
  preset: 11, faceShape: 7, eyeColour: 12, glasses: 1,
  hairStyle: 23, hairColour: 9,
  skin: 5, freckles: 3, scars: 2,
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

  const setOption = (key: string, next: number) => {
    setOpts((o) => ({ ...o, [key]: next }));
    bridge.setOption(key, next);
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
    <div style={{ height: "100vh", display: "flex", flexDirection: "column", overflow: "hidden", background: "radial-gradient(1100px 700px at 26% -8%, rgba(201,162,90,.06), rgba(0,0,0,0) 58%), " + T.bg, fontFamily: T.body, color: T.ink }}>
      <TopBar tabs={TABS} activeId={tabId} title={tab.title} onSelect={setTabId} onPrev={() => cycleTab(-1)} onNext={() => cycleTab(1)} />

      <div style={{ flex: 1, display: "flex", minHeight: 0 }}>
        {/* LEFT: controls */}
        <div style={{ flex: "none", width: 460, padding: "26px 30px", overflowY: "auto" }}>
          {isFinalise ? (
            <Finalise
              voice={voice} onVoice={(t) => { setVoice(t); bridge.setVoice(t); }}
              pitch={pitch} onPitch={(v) => { setPitch(v); bridge.setPitch(v); }}
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
        </div>

        {/* RIGHT: live avatar stage */}
        <Stage title={tab.title} summary={summary} isFinalise={isFinalise} canConfirm={canConfirm} onConfirm={bridge.confirm} />
      </div>

      {/* footer */}
      <div style={{ flex: "none", height: 44, display: "flex", alignItems: "center", justifyContent: "space-between", padding: "0 26px", borderTop: `1px solid ${T.goldLineSoft}`, background: "#0c0906" }}>
        <span style={{ fontFamily: T.mono, fontSize: 10, letterSpacing: ".14em", color: T.mutedDim }}>HogwartsMP</span>
        <span style={{ display: "flex", alignItems: "center", gap: 8 }}>
          <span style={{ fontFamily: T.mono, fontSize: 10, color: T.goldSoft, padding: "3px 7px", border: `1px solid ${T.goldLine}`, borderRadius: 4 }}>ESC</span>
          <span style={{ fontFamily: T.display, fontSize: 10, letterSpacing: ".16em", color: "#9c8f74" }}>BACK</span>
        </span>
      </div>
    </div>
  );
}
