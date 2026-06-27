import type { CSSProperties } from "react";
import { C, FONT, hair } from "../theme";
import { bridge } from "../bridge";

export function TitleBar() {
  return (
    <div
      style={{
        height: 36,
        flex: "none",
        display: "flex",
        alignItems: "center",
        justifyContent: "space-between",
        padding: "0 16px",
        background: C.titlebar,
        borderBottom: hair(0.12),
        // Lets the host treat the bar as the OS-window drag region (ignored in a browser).
        WebkitAppRegion: "drag",
      } as CSSProperties}
    >
      <div style={{ display: "flex", alignItems: "center", gap: 9 }}>
        <span
          style={{
            width: 8,
            height: 8,
            background: C.bright,
            transform: "rotate(45deg)",
            display: "inline-block",
          }}
        />
        <span style={{ fontFamily: FONT.cinzel, fontSize: 11, letterSpacing: ".22em", color: C.muted }}>
          HOGWARTSMP
        </span>
      </div>

      <div
        style={
          {
            display: "flex",
            alignItems: "center",
            gap: 18,
            color: C.faint2,
            fontSize: 13,
            WebkitAppRegion: "no-drag",
          } as CSSProperties
        }
      >
        <span className="hmp-win-btn" style={{ cursor: "pointer" }} onClick={() => bridge.minimize()} title="Minimize">
          &minus;
        </span>
        <span className="hmp-win-btn" style={{ cursor: "pointer", fontSize: 10 }} onClick={() => bridge.maximize()} title="Maximize">
          &#9633;
        </span>
        <span className="hmp-win-btn" style={{ cursor: "pointer" }} onClick={() => bridge.close()} title="Close">
          &times;
        </span>
      </div>
    </div>
  );
}
