// Design tokens for the HogwartsMP launcher UI (gold-on-charcoal dark academia).
// Gold-on-charcoal dark-academia. Gold is the only brand hue; functional status colors are
// used for MEANING only, never decoration. Values are kept verbatim from the spec so the UI
// stays pixel-faithful.

export const C = {
  // Surfaces
  pageBg: "#0a0805",
  window: "#15120c",
  panel: "#100d08",
  titlebar: "#0c0906",
  well: "#0e0b07",
  card: "#16120b",

  // Gold accent
  accent: "#c9a25a",
  bright: "#d8b56b",
  goldGrad: "linear-gradient(180deg,#dcb96e,#c19a4e)",
  onGold: "#1a1306",

  // Text shades
  heading: "#f0e3c2",
  heading2: "#f3e7c6",
  primary: "#e7d8b4",
  body: "#ddd0b2",
  body2: "#d5c8a9",
  data: "#c2b291",
  muted: "#9c8f74",
  faint: "#8a7b5f",
  faint2: "#6f6553",
  faint3: "#5e553f",

  // Status (meaning only)
  online: "#8aa860",
  privateText: "#cf8a72",

  // Bars
  barTrack: "#2a241a",
  barFull: "#8f4a44",
} as const;

// Hairline border helper.
export const hair = (a: number) => `1px solid rgba(201,162,90,${a})`;

export const FONT = {
  cinzel: "'Cinzel',serif",
  spectral: "'Spectral',Georgia,serif",
  mono: "ui-monospace,monospace",
} as const;

// Server mode badge palette: [bg, border, text].
export const MODE_COLORS: Record<string, [string, string, string]> = {
  Roleplay: ["rgba(201,162,90,.12)", "rgba(201,162,90,.42)", "#dab96e"],
  PvP: ["rgba(150,60,55,.18)", "rgba(165,75,65,.5)", "#d2876e"],
  "Co-op": ["rgba(110,140,90,.15)", "rgba(125,155,100,.45)", "#a3bd82"],
  Trade: ["rgba(90,120,132,.15)", "rgba(105,135,148,.45)", "#92b6c0"],
};

// News category label colors.
export const CAT_COLORS: Record<string, string> = {
  UPDATE: "#dab96e",
  HOTFIX: "#cf7a5e",
  EVENT: "#a3bd82",
};

export const pingColor = (ping: number) =>
  ping < 40 ? "#8aa860" : ping < 80 ? "#d8b56b" : "#cf7a5e";
