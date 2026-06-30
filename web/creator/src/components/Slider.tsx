import { useRef } from "react";
import { T } from "../theme";

interface Props {
  value: number; // 1-based
  max: number;
  onChange: (next: number) => void;
}

// Discrete dot-track slider from the design: ‹ prev / dot track / next › + readout.
// Small sets render one dot per option (true discrete); large sets render a capped row of
// decorative ticks with the nearest one highlighted. Click a tick or the track to jump.
export function Slider({ value, max, onChange }: Props) {
  const trackRef = useRef<HTMLDivElement>(null);
  const dragging = useRef(false);
  const clamp = (n: number) => Math.min(max, Math.max(1, n));

  const dotCount = Math.min(max, 21);
  // Index (0-based) of the decorative dot nearest the current value.
  const activeDot = max <= 1 ? 0 : Math.round(((value - 1) / (max - 1)) * (dotCount - 1));

  const jumpFromX = (clientX: number) => {
    const el = trackRef.current;
    if (!el) return;
    const r = el.getBoundingClientRect();
    const frac = Math.min(1, Math.max(0, (clientX - r.left) / r.width));
    onChange(clamp(Math.round(frac * (max - 1)) + 1));
  };

  // Grab-and-drag: capture the pointer so move/up keep firing even past the track edge.
  const onDown = (e: React.PointerEvent) => {
    dragging.current = true;
    trackRef.current?.setPointerCapture(e.pointerId);
    jumpFromX(e.clientX);
  };
  const onMove = (e: React.PointerEvent) => {
    if (dragging.current) jumpFromX(e.clientX);
  };
  const onUp = (e: React.PointerEvent) => {
    dragging.current = false;
    trackRef.current?.releasePointerCapture(e.pointerId);
  };

  const arrow: React.CSSProperties = {
    flex: "none", width: 30, height: 30, borderRadius: "50%", border: `1px solid ${T.goldLine}`,
    display: "flex", alignItems: "center", justifyContent: "center", color: T.goldSoft,
    fontSize: 18, cursor: "pointer", userSelect: "none",
  };

  return (
    <div style={{ display: "flex", alignItems: "center", gap: 14 }}>
      <div className="hmp-arrow" style={arrow} onClick={() => onChange(clamp(value - 1))}>&#8249;</div>
      <div
        ref={trackRef}
        onPointerDown={onDown}
        onPointerMove={onMove}
        onPointerUp={onUp}
        style={{ flex: 1, position: "relative", height: 26, cursor: "pointer", touchAction: "none" }}
      >
        <div style={{ position: "absolute", left: 2, right: 2, top: "50%", transform: "translateY(-50%)", height: 1, background: "rgba(201,162,90,.16)" }} />
        <div style={{ position: "relative", display: "flex", alignItems: "center", justifyContent: "space-between", height: "100%" }}>
          {Array.from({ length: dotCount }, (_, i) =>
            i === activeDot ? (
              <span key={i} style={{ width: 14, height: 14, borderRadius: "50%", background: T.goldBright, boxShadow: `0 0 0 3px rgba(201,162,90,.2), 0 0 10px rgba(216,181,107,.55)`, pointerEvents: "none" }} />
            ) : (
              <span key={i} style={{ width: 5, height: 5, borderRadius: "50%", background: T.dotOff, pointerEvents: "none" }} />
            ),
          )}
        </div>
      </div>
      <div className="hmp-arrow" style={arrow} onClick={() => onChange(clamp(value + 1))}>&#8250;</div>
      <div style={{ flex: "none", width: 52, textAlign: "right", fontFamily: T.mono, fontSize: 12, letterSpacing: ".04em", color: "#c2b291" }}>
        {value} / {max}
      </div>
    </div>
  );
}
