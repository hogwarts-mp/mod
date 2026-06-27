import { useState } from "react";
import { C, FONT, hair } from "../theme";
import { RECENTS } from "../data";
import { bridge } from "../bridge";

export function DirectConnectPage() {
  const [addr, setAddr] = useState("");

  return (
    <div style={{ flex: 1, minWidth: 0, display: "flex", alignItems: "center", justifyContent: "center", padding: 40 }}>
      <div style={{ width: 600, maxWidth: "100%" }}>
        <div style={{ fontFamily: FONT.cinzel, fontSize: 10, letterSpacing: ".32em", color: "#b89a64", marginBottom: 14 }}>CONNECT MANUALLY</div>
        <h1 style={{ margin: "0 0 14px", fontFamily: FONT.cinzel, fontSize: 30, letterSpacing: ".03em", color: C.heading, fontWeight: 600 }}>Direct Connect</h1>
        <p style={{ margin: "0 0 30px", fontSize: 15, lineHeight: 1.6, color: "#bcae93", maxWidth: 520 }}>
          Have a code or address from a friend or a server's website? Paste it below to jump straight in &mdash; no browsing required.
        </p>

        <div style={{ fontFamily: FONT.cinzel, fontSize: 10, letterSpacing: ".16em", textTransform: "uppercase", color: C.faint, marginBottom: 10 }}>Server Address</div>
        <div style={{ display: "flex", gap: 12 }}>
          <div style={{ flex: 1, padding: "15px 16px", background: C.well, border: hair(0.22), borderRadius: 4 }}>
            <input
              className="hmp-input"
              style={{ fontFamily: FONT.mono, fontSize: 13 }}
              placeholder="hogwartsmp://join/  ·  or  ·  203.0.113.4:7777"
              value={addr}
              onChange={(e) => setAddr(e.target.value)}
              onKeyDown={(e) => e.key === "Enter" && bridge.connect(addr)}
            />
          </div>
          <button
            className="hmp-btn-primary"
            onClick={() => bridge.connect(addr)}
            style={{ padding: "0 30px", background: C.goldGrad, border: "none", borderRadius: 4, fontFamily: FONT.cinzel, fontSize: 14, letterSpacing: ".14em", color: C.onGold, fontWeight: 600, cursor: "pointer", boxShadow: "0 6px 18px rgba(201,162,90,.22)", whiteSpace: "nowrap" }}
          >
            CONNECT
          </button>
        </div>
        <div style={{ display: "flex", alignItems: "center", gap: 8, marginTop: 11, fontSize: 12, color: "#7c7263" }}>
          <span style={{ width: 5, height: 5, background: C.online, borderRadius: "50%", display: "inline-block" }} />
          Copy an invite link and it will be detected from your clipboard automatically.
        </div>

        <div style={{ height: 1, background: "rgba(201,162,90,.12)", margin: "34px 0 24px" }} />

        <div style={{ display: "flex", alignItems: "center", justifyContent: "space-between", marginBottom: 14 }}>
          <span style={{ fontFamily: FONT.cinzel, fontSize: 14, letterSpacing: ".08em", color: "#cdb583" }}>Recent Connections</span>
          <span style={{ fontSize: 11, color: "#7c7263", cursor: "pointer" }}>Clear history</span>
        </div>

        <div style={{ display: "flex", flexDirection: "column", gap: 8 }}>
          {RECENTS.map((r) => (
            <div key={r.addr} style={{ display: "flex", alignItems: "center", gap: 16, padding: "13px 15px", background: C.card, border: hair(0.12), borderRadius: 4 }}>
              <div style={{ flex: 1, minWidth: 0 }}>
                <div style={{ fontFamily: FONT.cinzel, fontSize: 14, color: C.primary }}>{r.name}</div>
                <div style={{ fontFamily: FONT.mono, fontSize: 11, color: "#7c7263", marginTop: 3 }}>
                  {r.addr} &middot; {r.meta}
                </div>
              </div>
              <button
                className="hmp-btn-ghost"
                onClick={() => bridge.connect(r.addr)}
                style={{ padding: "8px 18px", background: "transparent", border: hair(0.3), borderRadius: 4, fontFamily: FONT.cinzel, fontSize: 11, letterSpacing: ".1em", color: "#d8c69c", cursor: "pointer" }}
              >
                JOIN
              </button>
            </div>
          ))}
        </div>
      </div>
    </div>
  );
}
