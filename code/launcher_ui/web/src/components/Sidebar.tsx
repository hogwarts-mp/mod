import { C, FONT, hair } from "../theme";
import { NEWS, type PageKey } from "../data";

interface SidebarProps {
  page: PageKey;
  onNavigate: (p: PageKey) => void;
}

const NAV: { key: PageKey; label: string }[] = [
  { key: "servers", label: "Servers" },
  { key: "direct", label: "Direct Connect" },
  { key: "news", label: "News" },
  { key: "settings", label: "Settings" },
];

export function Sidebar({ page, onNavigate }: SidebarProps) {
  const newsShort = NEWS.slice(0, 3);

  return (
    <aside
      style={{
        width: 228,
        flex: "none",
        height: "100%",
        background: C.panel,
        borderRight: hair(0.14),
        display: "flex",
        flexDirection: "column",
        padding: "26px 20px",
      }}
    >
      {/* Brand */}
      <div style={{ display: "flex", flexDirection: "column", gap: 7, marginBottom: 30 }}>
        <div style={{ fontFamily: FONT.cinzel, fontWeight: 700, fontSize: 22, letterSpacing: ".12em", color: C.primary, lineHeight: 1 }}>
          HOGWARTSMP
        </div>
        <div style={{ fontFamily: FONT.cinzel, fontSize: 9, letterSpacing: ".4em", color: C.faint }}>
          MULTIPLAYER
        </div>
      </div>

      {/* Nav */}
      <nav style={{ display: "flex", flexDirection: "column", gap: 4 }}>
        {NAV.map((item) => {
          const on = item.key === page;
          return (
            <div
              key={item.key}
              className="hmp-nav"
              onClick={() => onNavigate(item.key)}
              style={{
                display: "flex",
                alignItems: "center",
                gap: 13,
                padding: "11px 12px",
                borderRadius: 4,
                border: on ? "1px solid rgba(201,162,90,.22)" : "1px solid transparent",
                background: on ? "rgba(201,162,90,.10)" : "transparent",
                cursor: "pointer",
              }}
            >
              <span
                style={{
                  width: 7,
                  height: 7,
                  transform: "rotate(45deg)",
                  flex: "none",
                  background: on ? C.bright : "transparent",
                  border: on ? "none" : "1px solid #6b6049",
                }}
              />
              <span style={{ fontFamily: FONT.cinzel, fontSize: 13, letterSpacing: ".07em", color: on ? "#ecdcb7" : C.muted }}>
                {item.label}
              </span>
            </div>
          );
        })}
      </nav>

      {/* Latest Changes */}
      <div style={{ marginTop: 28, paddingTop: 22, borderTop: hair(0.12) }}>
        <div style={{ display: "flex", alignItems: "center", justifyContent: "space-between", marginBottom: 12 }}>
          <span style={{ fontFamily: FONT.cinzel, fontSize: 10, letterSpacing: ".18em", textTransform: "uppercase", color: C.faint }}>
            Latest Changes
          </span>
          <span onClick={() => onNavigate("news")} style={{ fontSize: 10, color: "#cdb583", letterSpacing: ".04em", cursor: "pointer" }}>
            All &rarr;
          </span>
        </div>
        {newsShort.map((n) => (
          <div key={n.tag} style={{ padding: "10px 0", borderBottom: hair(0.07) }}>
            <div style={{ display: "flex", alignItems: "center", gap: 7, marginBottom: 5 }}>
              <span style={{ fontFamily: FONT.cinzel, fontSize: 8, letterSpacing: ".08em", color: C.onGold, background: C.bright, padding: "2px 6px", borderRadius: 2, whiteSpace: "nowrap" }}>
                {n.tag}
              </span>
              <span style={{ fontFamily: FONT.mono, fontSize: 9, color: C.faint2, letterSpacing: ".04em" }}>{n.date}</span>
            </div>
            <div style={{ fontFamily: FONT.cinzel, fontSize: 12, color: C.body2, lineHeight: 1.25 }}>{n.title}</div>
          </div>
        ))}
      </div>

      <div style={{ flex: 1 }} />

      {/* Version / status chip */}
      <div
        style={{
          display: "flex",
          alignItems: "center",
          gap: 9,
          padding: "10px 12px",
          border: "1px solid rgba(120,150,95,.3)",
          background: "rgba(110,140,90,.08)",
          borderRadius: 4,
          marginBottom: 16,
        }}
      >
        <span style={{ width: 7, height: 7, borderRadius: "50%", background: C.online, flex: "none", boxShadow: "0 0 6px rgba(138,168,96,.6)" }} />
        <div style={{ display: "flex", flexDirection: "column", gap: 1 }}>
          <span style={{ fontFamily: FONT.mono, fontSize: 11, color: "#a3bd82", letterSpacing: ".03em" }}>v0.9.4</span>
          <span style={{ fontSize: 10, color: "#7c7263", letterSpacing: ".05em" }}>Mod up to date</span>
        </div>
      </div>

      {/* Profile (account identity only — no per-server level/class) */}
      <div style={{ display: "flex", alignItems: "center", gap: 11, paddingTop: 16, borderTop: hair(0.12) }}>
        <div
          style={{
            width: 38,
            height: 38,
            borderRadius: "50%",
            flex: "none",
            backgroundImage: "repeating-linear-gradient(135deg,#2a2318 0 6px,#221c13 6px 12px)",
            border: hair(0.3),
            display: "flex",
            alignItems: "center",
            justifyContent: "center",
            fontFamily: FONT.cinzel,
            fontSize: 13,
            color: C.bright,
          }}
        >
          EV
        </div>
        <div style={{ display: "flex", flexDirection: "column", gap: 3, minWidth: 0 }}>
          <span style={{ fontFamily: FONT.cinzel, fontSize: 13, color: C.primary, whiteSpace: "nowrap" }}>Evangeline R.</span>
          <span style={{ display: "flex", alignItems: "center", gap: 6, fontSize: 11, color: C.faint }}>
            <span style={{ width: 6, height: 6, borderRadius: "50%", background: C.online }} />
            Signed in
          </span>
        </div>
      </div>
    </aside>
  );
}
