import { useState } from "react";
import { C, FONT, hair } from "../theme";
import { SERVERS, type Server } from "../data";
import { bridge } from "../bridge";

function ModeBadge({ s, size = 10 }: { s: Server; size?: number }) {
  return (
    <span
      style={{
        fontFamily: FONT.cinzel,
        fontSize: size,
        letterSpacing: ".09em",
        padding: "4px 9px",
        borderRadius: 3,
        background: s.modeBg,
        border: `1px solid ${s.modeBorder}`,
        color: s.modeText,
        whiteSpace: "nowrap",
      }}
    >
      {s.mode}
    </span>
  );
}

function ServerRow({ s, selected, onSelect }: { s: Server; selected: boolean; onSelect: () => void }) {
  return (
    <div
      className={selected ? undefined : "hmp-row"}
      onClick={onSelect}
      style={{
        display: "flex",
        alignItems: "center",
        gap: 16,
        padding: "14px 26px",
        position: "relative",
        cursor: "pointer",
        background: selected ? "linear-gradient(90deg,rgba(201,162,90,.12),rgba(201,162,90,0))" : undefined,
        borderBottom: selected ? hair(0.08) : hair(0.06),
      }}
    >
      {selected && <span style={{ position: "absolute", left: 0, top: 0, bottom: 0, width: 3, background: C.bright }} />}
      <div style={{ flex: 1, minWidth: 0 }}>
        <div style={{ fontFamily: FONT.cinzel, fontSize: 15, color: selected ? C.heading : C.body, whiteSpace: "nowrap", overflow: "hidden", textOverflow: "ellipsis" }}>
          {s.name}
        </div>
        <div style={{ fontSize: 12, color: C.faint, marginTop: 3 }}>{s.tag}</div>
      </div>
      <div style={{ width: 150, flex: "none", display: "flex", alignItems: "center", gap: 7 }}>
        <ModeBadge s={s} />
        {s.locked && (
          <span style={{ flex: "none", fontFamily: FONT.cinzel, fontSize: 9, letterSpacing: ".1em", color: C.privateText, border: "1px solid rgba(165,75,65,.5)", background: "rgba(150,60,55,.16)", padding: "2px 6px", borderRadius: 3 }}>
            PRIVATE
          </span>
        )}
      </div>
      <div style={{ width: 96, flex: "none", fontSize: 12, color: C.muted, letterSpacing: ".03em" }}>{s.region}</div>
      <div style={{ width: 116, flex: "none", display: "flex", flexDirection: "column", gap: 5 }}>
        <span style={{ fontFamily: FONT.mono, fontSize: 12, color: selected ? C.data : "#a99c80" }}>{s.playersLabel}</span>
        <div style={{ height: 3, background: C.barTrack, borderRadius: 2, overflow: "hidden" }}>
          <div style={{ height: "100%", width: s.fillPct, background: s.fillColor }} />
        </div>
      </div>
      <div style={{ width: 72, flex: "none", display: "flex", alignItems: "center", justifyContent: "flex-end", gap: 6 }}>
        <span style={{ width: 6, height: 6, borderRadius: "50%", background: s.pingColorHex }} />
        <span style={{ fontFamily: FONT.mono, fontSize: 12, color: selected ? C.data : "#a99c80" }}>{s.ping}</span>
      </div>
    </div>
  );
}

function DetailPanel({ s }: { s: Server }) {
  return (
    <aside style={{ width: 344, flex: "none", height: "100%", background: C.panel, borderLeft: hair(0.14), display: "flex", flexDirection: "column" }}>
      {/* Banner */}
      <div style={{ height: 184, flex: "none", position: "relative", backgroundImage: "repeating-linear-gradient(135deg,#221d16 0 11px,#1b1610 11px 22px)", borderBottom: hair(0.14) }}>
        <span style={{ position: "absolute", inset: 0, display: "flex", alignItems: "center", justifyContent: "center", fontFamily: FONT.mono, fontSize: 10, letterSpacing: ".15em", color: C.faint3 }}>
          server banner
        </span>
        <div style={{ position: "absolute", inset: 0, background: "linear-gradient(180deg,rgba(16,13,8,0) 40%,rgba(16,13,8,.92))" }} />
        <div style={{ position: "absolute", left: 18, right: 18, bottom: 16 }}>
          <div style={{ fontFamily: FONT.cinzel, fontSize: 22, color: C.heading2, letterSpacing: ".02em", lineHeight: 1.1 }}>{s.name}</div>
          <div style={{ display: "flex", alignItems: "center", gap: 10, marginTop: 8 }}>
            <ModeBadge s={s} />
            <span style={{ fontSize: 12, color: C.muted }}>{s.region}</span>
            <span style={{ fontSize: 11, color: C.online, letterSpacing: ".06em" }}>&#9679; OPEN</span>
          </div>
        </div>
      </div>

      {/* Body */}
      <div style={{ flex: 1, minHeight: 0, padding: 22, display: "flex", flexDirection: "column" }}>
        <p style={{ margin: "0 0 22px", fontSize: 14, lineHeight: 1.6, color: "#bcae93" }}>{s.tag}</p>
        <div style={{ display: "flex", gap: 14, marginBottom: 22 }}>
          <div style={{ flex: 1, padding: 14, background: C.well, border: hair(0.12), borderRadius: 4 }}>
            <div style={{ fontFamily: FONT.cinzel, fontSize: 9, letterSpacing: ".16em", color: C.faint2, textTransform: "uppercase", marginBottom: 7 }}>Players</div>
            <div style={{ fontFamily: FONT.mono, fontSize: 18, color: C.primary }}>{s.playersLabel}</div>
            <div style={{ height: 3, background: C.barTrack, borderRadius: 2, overflow: "hidden", marginTop: 9 }}>
              <div style={{ height: "100%", width: s.fillPct, background: s.fillColor }} />
            </div>
          </div>
          <div style={{ flex: 1, padding: 14, background: C.well, border: hair(0.12), borderRadius: 4 }}>
            <div style={{ fontFamily: FONT.cinzel, fontSize: 9, letterSpacing: ".16em", color: C.faint2, textTransform: "uppercase", marginBottom: 7 }}>Latency</div>
            <div style={{ display: "flex", alignItems: "baseline", gap: 5 }}>
              <span style={{ fontFamily: FONT.mono, fontSize: 18, color: C.primary }}>{s.ping}</span>
              <span style={{ fontSize: 12, color: C.faint }}>ms</span>
            </div>
            <div style={{ fontSize: 11, color: C.online, marginTop: 9, letterSpacing: ".04em" }}>
              {s.ping < 40 ? "Excellent connection" : s.ping < 80 ? "Good connection" : "High latency"}
            </div>
          </div>
        </div>
        <div style={{ display: "flex", gap: 8, flexWrap: "wrap", marginBottom: 22 }}>
          {s.tag.split(" · ").map((t) => (
            <span key={t} style={{ fontSize: 11, color: C.muted, border: hair(0.15), borderRadius: 3, padding: "5px 10px" }}>
              {t}
            </span>
          ))}
        </div>
        <div style={{ flex: 1 }} />
        <button
          className="hmp-btn-primary"
          onClick={() => bridge.connect(s.addr)}
          style={{ width: "100%", padding: 15, background: C.goldGrad, border: "none", borderRadius: 4, fontFamily: FONT.cinzel, fontSize: 15, letterSpacing: ".16em", color: C.onGold, fontWeight: 600, cursor: "pointer", boxShadow: "0 6px 18px rgba(201,162,90,.22)" }}
        >
          CONNECT
        </button>
        <button
          className="hmp-btn-ghost"
          style={{ width: "100%", padding: 12, marginTop: 10, background: "transparent", border: hair(0.25), borderRadius: 4, fontFamily: FONT.cinzel, fontSize: 12, letterSpacing: ".1em", color: C.data, cursor: "pointer" }}
        >
          Add to Favourites
        </button>
      </div>
    </aside>
  );
}

export function ServersPage() {
  const [selected, setSelected] = useState(0);
  const sel = SERVERS[selected];
  const totalPlayers = SERVERS.reduce((sum, s) => sum + s.players, 0);

  return (
    <div style={{ flex: 1, minWidth: 0, display: "flex" }}>
      <div style={{ flex: 1, minWidth: 0, display: "flex", flexDirection: "column" }}>
        {/* Header + filters */}
        <div style={{ padding: "22px 26px 16px", borderBottom: hair(0.1), display: "flex", flexDirection: "column", gap: 14 }}>
          <div style={{ display: "flex", alignItems: "flex-end", justifyContent: "space-between" }}>
            <h1 style={{ margin: 0, fontFamily: FONT.cinzel, fontSize: 19, letterSpacing: ".09em", color: C.primary, fontWeight: 600 }}>Server Browser</h1>
            <span style={{ fontSize: 12, color: C.faint, letterSpacing: ".03em" }}>
              {SERVERS.length} servers &middot; {totalPlayers.toLocaleString()} wizards online
            </span>
          </div>
          <div style={{ display: "flex", gap: 10, alignItems: "center" }}>
            <div style={{ flex: 1, padding: "11px 14px", background: C.well, border: hair(0.18), borderRadius: 4 }}>
              <input className="hmp-input" placeholder="Search servers by name or tag…" style={{ fontSize: 13 }} />
            </div>
            <div style={{ padding: "10px 13px", border: hair(0.2), borderRadius: 4, fontSize: 12, color: C.data, letterSpacing: ".02em", whiteSpace: "nowrap" }}>All Regions &#9662;</div>
            <div style={{ padding: "10px 13px", border: hair(0.2), borderRadius: 4, fontSize: 12, color: C.data, letterSpacing: ".02em", whiteSpace: "nowrap" }}>All Modes &#9662;</div>
            <div style={{ padding: "10px 13px", border: hair(0.12), borderRadius: 4, fontSize: 12, color: "#7c7263", letterSpacing: ".02em", whiteSpace: "nowrap" }}>Hide full</div>
          </div>
        </div>

        {/* Column header */}
        <div style={{ display: "flex", alignItems: "center", gap: 16, padding: "13px 26px", fontFamily: FONT.cinzel, fontSize: 10, letterSpacing: ".18em", textTransform: "uppercase", color: C.faint2, borderBottom: hair(0.07) }}>
          <span style={{ flex: 1 }}>Server</span>
          <span style={{ width: 150, flex: "none" }}>Mode</span>
          <span style={{ width: 96, flex: "none" }}>Region</span>
          <span style={{ width: 116, flex: "none" }}>Players</span>
          <span style={{ width: 72, flex: "none", textAlign: "right" }}>Ping</span>
        </div>

        {/* Rows */}
        <div style={{ flex: 1, minHeight: 0, overflowY: "auto" }}>
          {SERVERS.map((s, i) => (
            <ServerRow key={s.name} s={s} selected={i === selected} onSelect={() => setSelected(i)} />
          ))}
        </div>
      </div>

      <DetailPanel s={sel} />
    </div>
  );
}
