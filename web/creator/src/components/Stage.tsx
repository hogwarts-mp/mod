import { T } from "../theme";
import { isHost } from "../bridge";

interface Props {
  title: string;
  summary: string;
  isFinalise: boolean;
  canConfirm: boolean;
  onConfirm: () => void;
}

// Right-hand "character stage". In-game it's transparent so the live 3D avatar renders
// behind it (we only overlay the vignette/specks/chips). In a plain browser (no host) we
// draw the CSS placeholder bust so the design is previewable. Rotation is A/D only.
export function Stage({ title, summary, isFinalise, canConfirm, onConfirm }: Props) {
  return (
    <div
      style={{ flex: 1, position: "relative", overflow: "hidden", borderLeft: `1px solid ${T.goldLineSoft}`, background: isHost ? "transparent" : "radial-gradient(120% 86% at 64% 4%, rgba(58,72,86,.30), rgba(0,0,0,0) 56%), linear-gradient(180deg,#0d0b08,#070504)" }}
    >
      <div style={{ position: "absolute", inset: 0, background: "radial-gradient(74% 70% at 56% 42%, rgba(0,0,0,0) 40%, rgba(0,0,0,.5) 100%)", pointerEvents: "none" }} />

      {/* drifting specks */}
      <div style={{ position: "absolute", left: "38%", top: "62%", width: 3, height: 3, borderRadius: "50%", background: T.gold, animation: "specks 6s ease-in-out infinite", pointerEvents: "none" }} />
      <div style={{ position: "absolute", left: "66%", top: "54%", width: 2, height: 2, borderRadius: "50%", background: T.gold, animation: "specks 7.5s ease-in-out .8s infinite", pointerEvents: "none" }} />
      <div style={{ position: "absolute", left: "54%", top: "70%", width: 2, height: 2, borderRadius: "50%", background: T.goldSoft, animation: "specks 8s ease-in-out 1.6s infinite", pointerEvents: "none" }} />

      {/* placeholder bust — preview only (would cover the real avatar in-game) */}
      {!isHost && (
        <div style={{ position: "absolute", left: "50%", bottom: 0, transform: "translateX(-50%)", width: "min(440px,72%)", height: "84%", pointerEvents: "none" }}>
          <div style={{ position: "absolute", left: "50%", top: "6%", transform: "translateX(-50%)", width: "74%", aspectRatio: "1", borderRadius: "50%", background: "radial-gradient(circle at 42% 36%, rgba(201,162,90,.10), rgba(0,0,0,0) 62%)" }} />
          <div style={{ position: "absolute", bottom: 0, left: "50%", transform: "translateX(-50%)", width: "100%", height: "46%", background: "linear-gradient(180deg,#3b3527,#211e16)", borderRadius: "46% 46% 0 0 / 70% 70% 0 0", boxShadow: "inset 18px -10px 30px rgba(0,0,0,.5), inset -16px 8px 26px rgba(201,162,90,.05)" }} />
          <div style={{ position: "absolute", bottom: "38%", left: "50%", transform: "translateX(-50%)", width: "54%", aspectRatio: ".84", background: "linear-gradient(160deg,#544c39,#332e22)", borderRadius: "50% 50% 48% 48% / 56% 56% 44% 44%", boxShadow: "inset 16px -12px 26px rgba(0,0,0,.45), inset -14px 10px 22px rgba(201,162,90,.08)" }} />
        </div>
      )}

      {/* summary chips */}
      <div style={{ position: "absolute", left: 22, top: 20, display: "flex", flexDirection: "column", gap: 7, pointerEvents: "none" }}>
        <div style={{ fontFamily: T.display, fontSize: 12, letterSpacing: ".18em", color: T.goldSoft }}>{title}</div>
        <div style={{ fontFamily: T.mono, fontSize: 10, letterSpacing: ".08em", color: "#7c7263" }}>{summary}</div>
      </div>

      <div style={{ position: "absolute", left: "50%", bottom: 18, transform: "translateX(-50%)", fontFamily: T.mono, fontSize: 9, letterSpacing: ".26em", color: T.mutedDim, pointerEvents: "none" }}>
        HogwartsMP &middot; CHARACTER RENDER
      </div>

      {/* confirm banner (finalise) */}
      {isFinalise && (
        <div style={{ position: "absolute", left: "50%", bottom: 52, transform: "translateX(-50%)", width: "min(340px,70%)" }}>
          {canConfirm ? (
            <div className="hmp-confirm" onClick={onConfirm} style={{ textAlign: "center", padding: "15px 0", borderRadius: 5, background: "linear-gradient(180deg,#dcb96e,#bf9a4d)", color: "#1a1306", fontFamily: T.display, fontWeight: 600, fontSize: 15, letterSpacing: ".18em", cursor: "pointer", boxShadow: `0 8px 26px rgba(201,162,90,.35)` }}>CONFIRM</div>
          ) : (
            <div style={{ textAlign: "center", padding: "15px 0", borderRadius: 5, background: "#15120c", border: `1px solid ${T.goldLineSoft}`, color: "#6f6553", fontFamily: T.display, fontSize: 14, letterSpacing: ".18em" }}>CONFIRM</div>
          )}
        </div>
      )}
    </div>
  );
}
