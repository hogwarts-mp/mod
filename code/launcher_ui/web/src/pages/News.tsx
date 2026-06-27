import { useState } from "react";
import { C, FONT, hair } from "../theme";
import { NEWS } from "../data";

type Filter = "All" | "Updates" | "Events";
const FILTERS: Filter[] = ["All", "Updates", "Events"];

export function NewsPage() {
  const [filter, setFilter] = useState<Filter>("All");
  const hero = NEWS[0];

  const feed = NEWS.slice(1).filter((n) => {
    if (filter === "Updates") return n.cat === "UPDATE" || n.cat === "HOTFIX";
    if (filter === "Events") return n.cat === "EVENT";
    return true;
  });

  return (
    <div style={{ flex: 1, minWidth: 0, padding: "28px 40px", overflow: "hidden", display: "flex", flexDirection: "column" }}>
      <div style={{ display: "flex", alignItems: "flex-end", justifyContent: "space-between", marginBottom: 22 }}>
        <h1 style={{ margin: 0, fontFamily: FONT.cinzel, fontSize: 24, letterSpacing: ".05em", color: C.heading, fontWeight: 600 }}>News &amp; Patch Notes</h1>
        <div style={{ display: "flex", gap: 8 }}>
          {FILTERS.map((f) => {
            const on = f === filter;
            return (
              <span
                key={f}
                onClick={() => setFilter(f)}
                style={{
                  padding: "7px 14px",
                  borderRadius: 4,
                  background: on ? "rgba(201,162,90,.1)" : "transparent",
                  border: on ? "1px solid rgba(201,162,90,.22)" : hair(0.12),
                  fontFamily: FONT.cinzel,
                  fontSize: 11,
                  letterSpacing: ".06em",
                  color: on ? "#ecdcb7" : C.muted,
                  cursor: "pointer",
                }}
              >
                {f}
              </span>
            );
          })}
        </div>
      </div>

      {/* Hero */}
      <div style={{ position: "relative", height: 176, flex: "none", borderRadius: 5, overflow: "hidden", border: hair(0.14), backgroundImage: "repeating-linear-gradient(135deg,#221d16 0 12px,#1b1610 12px 24px)", marginBottom: 8 }}>
        <span style={{ position: "absolute", inset: 0, display: "flex", alignItems: "center", justifyContent: "center", fontFamily: FONT.mono, fontSize: 10, letterSpacing: ".15em", color: "#574e3a" }}>feature banner</span>
        <div style={{ position: "absolute", inset: 0, background: "linear-gradient(180deg,rgba(16,13,8,0) 30%,rgba(16,13,8,.94))" }} />
        <div style={{ position: "absolute", left: 26, right: 26, bottom: 20 }}>
          <div style={{ display: "flex", alignItems: "center", gap: 10, marginBottom: 9 }}>
            <span style={{ fontFamily: FONT.cinzel, fontSize: 9, letterSpacing: ".1em", color: C.onGold, background: C.bright, padding: "3px 9px", borderRadius: 3 }}>v{hero.tag}</span>
            <span style={{ fontFamily: FONT.cinzel, fontSize: 10, letterSpacing: ".12em", color: hero.catColor }}>{hero.cat}</span>
            <span style={{ fontFamily: FONT.mono, fontSize: 10, color: C.faint }}>{hero.date} {hero.year}</span>
          </div>
          <div style={{ fontFamily: FONT.cinzel, fontSize: 24, color: C.heading2, letterSpacing: ".02em" }}>{hero.title}</div>
          <div style={{ fontSize: 13, color: "#bcae93", lineHeight: 1.5, marginTop: 6, maxWidth: 680 }}>{hero.body}</div>
        </div>
      </div>

      {/* Feed */}
      <div style={{ flex: 1, minHeight: 0, overflowY: "auto" }}>
        {feed.map((n) => (
          <div key={n.tag} style={{ display: "flex", gap: 24, padding: "19px 0", borderBottom: hair(0.08) }}>
            <div style={{ width: 74, flex: "none", textAlign: "right" }}>
              <div style={{ fontFamily: FONT.mono, fontSize: 13, color: C.data, letterSpacing: ".03em" }}>{n.date}</div>
              <div style={{ fontFamily: FONT.mono, fontSize: 10, color: C.faint2, marginTop: 2 }}>{n.year}</div>
            </div>
            <div style={{ flex: 1, minWidth: 0 }}>
              <div style={{ display: "flex", alignItems: "center", gap: 9, marginBottom: 6 }}>
                <span style={{ fontFamily: FONT.cinzel, fontSize: 8, letterSpacing: ".08em", color: C.onGold, background: C.bright, padding: "2px 7px", borderRadius: 2 }}>v{n.tag}</span>
                <span style={{ fontFamily: FONT.cinzel, fontSize: 9, letterSpacing: ".12em", color: n.catColor }}>{n.cat}</span>
              </div>
              <div style={{ fontFamily: FONT.cinzel, fontSize: 17, color: C.primary, letterSpacing: ".01em" }}>{n.title}</div>
              <div style={{ fontSize: 13, color: C.muted, lineHeight: 1.55, marginTop: 5, maxWidth: 720 }}>{n.body}</div>
            </div>
          </div>
        ))}
      </div>
    </div>
  );
}
