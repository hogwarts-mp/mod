import { T } from "../theme";
import { Slider } from "./Slider";
import { PITCH_MAX } from "../data";

interface Props {
  voice: number;
  onVoice: (tone: number) => void;
  pitch: number;
  onPitch: (v: number) => void;
  first: string;
  last: string;
  onFirst: (v: string) => void;
  onLast: (v: string) => void;
  nameError: boolean;
  dorm: "witch" | "wizard" | null;
  onDorm: (d: "witch" | "wizard") => void;
}

function SectionLabel({ text }: { text: string }) {
  return (
    <div style={{ display: "flex", alignItems: "center", gap: 14, marginBottom: 16 }}>
      <div style={{ fontFamily: T.display, fontSize: 11, letterSpacing: ".24em", color: T.label }}>{text}</div>
      <div style={{ flex: 1, height: 1, background: "linear-gradient(90deg, rgba(201,162,90,.28), rgba(201,162,90,0))" }} />
    </div>
  );
}

function Choice({ label, active, onClick }: { label: string; active: boolean; onClick: () => void }) {
  return (
    <div className="hmp-choice" onClick={onClick} style={{ position: "relative", padding: "14px 0", textAlign: "center", border: `1px solid ${T.goldLine}`, borderRadius: 5, background: T.panel, fontFamily: T.display, fontSize: 13, letterSpacing: ".14em", color: T.ink, cursor: "pointer" }}>
      {label}
      {active && <div style={{ position: "absolute", inset: 0, border: `1px solid ${T.gold}`, borderRadius: 5, background: "rgba(201,162,90,.12)", boxShadow: `0 0 14px ${T.goldGlow}`, pointerEvents: "none" }} />}
    </div>
  );
}

export function Finalise(p: Props) {
  const input: React.CSSProperties = { padding: "12px 14px", background: T.panel, border: `1px solid ${T.goldLine}`, borderRadius: 4, fontSize: 14, color: T.inkInput, outline: "none" };
  return (
    <div>
      <SectionLabel text="VOICE · TONE" />
      <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 12, marginBottom: 26 }}>
        <Choice label="VOICE ONE" active={p.voice === 1} onClick={() => p.onVoice(1)} />
        <Choice label="VOICE TWO" active={p.voice === 2} onClick={() => p.onVoice(2)} />
      </div>

      <div style={{ fontFamily: T.display, fontSize: 11, letterSpacing: ".24em", color: T.muted, marginBottom: 12 }}>PITCH</div>
      <div style={{ marginBottom: 28 }}>
        <Slider value={p.pitch} max={PITCH_MAX} onChange={p.onPitch} />
      </div>

      <SectionLabel text="NAME YOUR CHARACTER" />
      <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 12, marginBottom: 8 }}>
        <input value={p.first} onChange={(e) => p.onFirst(e.target.value)} placeholder="First Name" style={input} />
        <input value={p.last} onChange={(e) => p.onLast(e.target.value)} placeholder="Last Name" style={input} />
      </div>
      <div style={{ height: 14, marginBottom: 22 }}>
        {p.nameError && (
          <div style={{ fontFamily: T.display, fontSize: 10, letterSpacing: ".12em", color: T.error }}>YOU MUST NAME YOUR CHARACTER TO PROCEED.</div>
        )}
      </div>

      <SectionLabel text="DORMITORY" />
      <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 12, marginBottom: 8 }}>
        <Choice label="WITCH" active={p.dorm === "witch"} onClick={() => p.onDorm("witch")} />
        <Choice label="WIZARD" active={p.dorm === "wizard"} onClick={() => p.onDorm("wizard")} />
      </div>
      <div style={{ fontSize: 12, color: T.muted }}>This choice determines your character&rsquo;s dormitory.</div>
    </div>
  );
}
