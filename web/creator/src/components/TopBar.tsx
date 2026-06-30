import { T } from "../theme";
import type { Tab } from "../data";

interface Props {
  tabs: Tab[];
  activeId: string;
  title: string;
  onSelect: (id: string) => void;
  onPrev: () => void;
  onNext: () => void;
}

export function TopBar({ tabs, activeId, title, onSelect, onPrev, onNext }: Props) {
  const navArrow: React.CSSProperties = {
    flex: "none", width: 34, height: 34, borderRadius: "50%", border: `1px solid ${T.goldLine}`,
    display: "flex", alignItems: "center", justifyContent: "center", color: T.goldSoft,
    fontSize: 18, cursor: "pointer", userSelect: "none",
  };

  return (
    <div style={{ flex: "none", padding: "20px 30px 14px", borderBottom: `1px solid ${T.goldLineSoft}`, background: "rgba(10,8,5,.92)" }}>
      {/* brand */}
      <div style={{ display: "flex", alignItems: "center", gap: 11, marginBottom: 12 }}>
        <span style={{ width: 10, height: 10, background: T.gold, transform: "rotate(45deg)", display: "inline-block" }} />
        <span style={{ fontFamily: T.display, fontWeight: 700, fontSize: 15, letterSpacing: ".30em", color: T.goldSoft }}>HogwartsMP</span>
        <span style={{ fontFamily: T.display, fontSize: 10, letterSpacing: ".34em", color: "#7c7263", marginLeft: 4 }}>CHARACTER CREATION</span>
      </div>

      {/* category title */}
      <div style={{ display: "flex", alignItems: "center", gap: 18, marginBottom: 16 }}>
        <h1 style={{ margin: 0, fontFamily: T.display, fontWeight: 600, fontSize: 25, letterSpacing: ".10em", color: T.inkBright }}>{title}</h1>
        <div style={{ flex: 1, height: 1, background: "linear-gradient(90deg, rgba(201,162,90,.32), rgba(201,162,90,0))" }} />
      </div>

      {/* tab medallions */}
      <div style={{ display: "flex", alignItems: "center", gap: 14 }}>
        <div className="hmp-arrow" style={navArrow} onClick={onPrev}>&#8249;</div>
        <div style={{ display: "flex", alignItems: "center", gap: 12 }}>
          {tabs.map((tab) => {
            const active = tab.id === activeId;
            return (
              <div
                key={tab.id}
                className="hmp-medallion"
                onClick={() => onSelect(tab.id)}
                style={{ position: "relative", width: 46, height: 46, borderRadius: "50%", border: `1px solid ${T.goldLine}`, background: "#100d08", display: "flex", alignItems: "center", justifyContent: "center", cursor: "pointer" }}
              >
                <span style={{ fontFamily: T.display, fontSize: 14, letterSpacing: ".04em", color: "#d8c69c" }}>{tab.numeral}</span>
                {active && (
                  <>
                    <div style={{ position: "absolute", inset: -1, borderRadius: "50%", border: `1px solid ${T.gold}`, boxShadow: `0 0 0 1px rgba(216,181,107,.25), 0 0 16px ${T.goldGlow}`, background: "rgba(201,162,90,.12)" }} />
                    <span style={{ position: "absolute", top: -13, width: 6, height: 6, background: T.gold, transform: "rotate(45deg)" }} />
                  </>
                )}
              </div>
            );
          })}
        </div>
        <div className="hmp-arrow" style={navArrow} onClick={onNext}>&#8250;</div>
      </div>
    </div>
  );
}
