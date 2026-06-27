import { useState, type ReactNode } from "react";
import { C, FONT, hair } from "../theme";

const CATEGORIES = ["General", "Graphics", "Audio", "Controls", "Mod & Account"];

function Segmented<T extends string>({ options, value, onChange }: { options: readonly T[]; value: T; onChange: (v: T) => void }) {
  return (
    <div style={{ display: "flex", border: hair(0.2), borderRadius: 5, overflow: "hidden" }}>
      {options.map((opt, i) => {
        const on = opt === value;
        return (
          <span
            key={opt}
            onClick={() => onChange(opt)}
            style={{
              padding: "8px 14px",
              background: on ? "rgba(201,162,90,.16)" : "transparent",
              color: on ? "#ecdcb7" : C.muted,
              fontFamily: FONT.cinzel,
              fontSize: 11,
              letterSpacing: ".05em",
              cursor: "pointer",
              borderLeft: i === 0 ? undefined : "1px solid rgba(201,162,90,.14)",
            }}
          >
            {opt}
          </span>
        );
      })}
    </div>
  );
}

function Row({ title, helper, children }: { title: string; helper: string; children: ReactNode }) {
  return (
    <div style={{ display: "flex", alignItems: "center", justifyContent: "space-between", padding: "17px 0", borderBottom: hair(0.08) }}>
      <div>
        <div style={{ fontFamily: FONT.cinzel, fontSize: 14, color: C.primary }}>{title}</div>
        <div style={{ fontSize: 12, color: C.faint, marginTop: 3 }}>{helper}</div>
      </div>
      {children}
    </div>
  );
}

function Select({ label }: { label: string }) {
  return (
    <div style={{ display: "flex", alignItems: "center", gap: 10, justifyContent: "space-between", minWidth: 172, padding: "9px 13px", background: C.well, border: hair(0.2), borderRadius: 4 }}>
      <span style={{ fontSize: 13, color: C.body2 }}>{label}</span>
      <span style={{ color: C.faint }}>&#9662;</span>
    </div>
  );
}

const DISPLAY_MODES = ["Borderless", "Fullscreen", "Windowed"] as const;
const QUALITY = ["Low", "Medium", "High", "Ultra"] as const;

export function SettingsPage() {
  const [category, setCategory] = useState("Graphics");
  const [displayMode, setDisplayMode] = useState<(typeof DISPLAY_MODES)[number]>("Borderless");
  const [quality, setQuality] = useState<(typeof QUALITY)[number]>("Ultra");
  const [vsync, setVsync] = useState(true);

  return (
    <div style={{ flex: 1, minWidth: 0, display: "flex" }}>
      {/* Category rail */}
      <div style={{ width: 192, flex: "none", borderRight: hair(0.1), padding: "26px 16px" }}>
        <div style={{ fontFamily: FONT.cinzel, fontSize: 10, letterSpacing: ".2em", textTransform: "uppercase", color: C.faint, padding: "0 10px", marginBottom: 14 }}>Settings</div>
        <div style={{ display: "flex", flexDirection: "column", gap: 2 }}>
          {CATEGORIES.map((cat) => {
            const on = cat === category;
            return (
              <div
                key={cat}
                onClick={() => setCategory(cat)}
                style={{
                  padding: "10px 12px",
                  borderRadius: 4,
                  background: on ? "rgba(201,162,90,.1)" : "transparent",
                  border: on ? "1px solid rgba(201,162,90,.22)" : "1px solid transparent",
                  fontFamily: FONT.cinzel,
                  fontSize: 13,
                  letterSpacing: ".04em",
                  color: on ? "#ecdcb7" : C.muted,
                  cursor: "pointer",
                }}
              >
                {cat}
              </div>
            );
          })}
        </div>
      </div>

      {/* Content */}
      <div style={{ flex: 1, minWidth: 0, display: "flex", flexDirection: "column" }}>
        <div style={{ flex: 1, minHeight: 0, overflowY: "auto", padding: "28px 36px" }}>
          {category === "Graphics" ? (
            <>
              <h1 style={{ margin: "0 0 4px", fontFamily: FONT.cinzel, fontSize: 22, letterSpacing: ".05em", color: C.heading, fontWeight: 600 }}>Graphics</h1>
              <p style={{ margin: "0 0 14px", fontSize: 13, color: C.faint }}>Tune visual fidelity and performance for your machine.</p>

              <Row title="Resolution" helper="Native display resolution recommended.">
                <Select label="2560 × 1440" />
              </Row>
              <Row title="Display Mode" helper="How the game fills your screen.">
                <Segmented options={DISPLAY_MODES} value={displayMode} onChange={setDisplayMode} />
              </Row>
              <Row title="Quality Preset" helper="Overall detail level for textures, shadows and effects.">
                <Segmented options={QUALITY} value={quality} onChange={setQuality} />
              </Row>
              <Row title="Vertical Sync" helper="Prevents screen tearing at a small latency cost.">
                <div
                  onClick={() => setVsync((v) => !v)}
                  style={{ width: 44, height: 24, borderRadius: 13, background: vsync ? "#c19a4e" : C.barTrack, position: "relative", flex: "none", cursor: "pointer", transition: "background .12s ease" }}
                >
                  <span style={{ position: "absolute", top: 2, left: vsync ? 22 : 2, width: 20, height: 20, borderRadius: "50%", background: vsync ? C.onGold : C.faint2, transition: "left .12s ease" }} />
                </div>
              </Row>
              <Row title="Frame Rate Cap" helper="Limit FPS to reduce heat and power draw.">
                <Select label="144 FPS" />
              </Row>
              <div style={{ display: "flex", alignItems: "center", justifyContent: "space-between", padding: "17px 0" }}>
                <div>
                  <div style={{ fontFamily: FONT.cinzel, fontSize: 14, color: C.primary }}>Render Scale</div>
                  <div style={{ fontSize: 12, color: C.faint, marginTop: 3 }}>Internal resolution multiplier for sharper visuals.</div>
                </div>
                <div style={{ width: 200, display: "flex", alignItems: "center", gap: 12 }}>
                  <div style={{ flex: 1, height: 4, background: C.barTrack, borderRadius: 2, position: "relative" }}>
                    <div style={{ position: "absolute", left: 0, top: 0, bottom: 0, width: "80%", background: "#c19a4e", borderRadius: 2 }} />
                    <span style={{ position: "absolute", left: "80%", top: "50%", transform: "translate(-50%,-50%)", width: 15, height: 15, borderRadius: "50%", background: C.bright, boxShadow: "0 0 0 3px rgba(201,162,90,.2)" }} />
                  </div>
                  <span style={{ fontFamily: FONT.mono, fontSize: 12, color: C.data, width: 40, textAlign: "right" }}>100%</span>
                </div>
              </div>
            </>
          ) : (
            <>
              <h1 style={{ margin: "0 0 4px", fontFamily: FONT.cinzel, fontSize: 22, letterSpacing: ".05em", color: C.heading, fontWeight: 600 }}>{category}</h1>
              <p style={{ margin: "0 0 14px", fontSize: 13, color: C.faint }}>These settings are coming soon.</p>
            </>
          )}
        </div>

        {/* Footer */}
        <div style={{ flex: "none", display: "flex", alignItems: "center", justifyContent: "flex-end", gap: 12, padding: "16px 36px", borderTop: hair(0.1), background: C.panel }}>
          <button className="hmp-btn-ghost" style={{ padding: "11px 20px", background: "transparent", border: hair(0.22), borderRadius: 4, fontFamily: FONT.cinzel, fontSize: 12, letterSpacing: ".08em", color: C.data, cursor: "pointer" }}>
            Restore Defaults
          </button>
          <button className="hmp-btn-primary" style={{ padding: "11px 26px", background: C.goldGrad, border: "none", borderRadius: 4, fontFamily: FONT.cinzel, fontSize: 12, letterSpacing: ".1em", color: C.onGold, fontWeight: 600, cursor: "pointer" }}>
            Apply Changes
          </button>
        </div>
      </div>
    </div>
  );
}
