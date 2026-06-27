// Mock launcher data, ported from the design handoff's logic class.
//
// This is placeholder content for v1 (shell + launch game). The live server browser
// (masterlist query) and real news/recents feeds are deferred — when they land, replace
// these literals with fetched data; the component layer consumes the same types.

import { MODE_COLORS, CAT_COLORS, C, pingColor } from "./theme";

export type ServerMode = "Roleplay" | "PvP" | "Co-op" | "Trade";

export interface RawServer {
  name: string;
  mode: ServerMode;
  region: string;
  players: number;
  cap: number;
  ping: number;
  locked: boolean;
  tag: string;
  /** host:port the CONNECT/JOIN buttons hand to the client. Mock until the browser is live. */
  addr: string;
}

export interface Server extends RawServer {
  playersLabel: string;
  fillPct: string;
  full: boolean;
  pingColorHex: string;
  fillColor: string;
  modeBg: string;
  modeBorder: string;
  modeText: string;
}

const RAW_SERVERS: RawServer[] = [
  // Real, connectable test servers (top of the list so they're the default selection).
  { name: "Local Dev Server", mode: "Co-op", region: "Local", players: 1, cap: 32, ping: 1, locked: false, tag: "Local development · loopback", addr: "127.0.0.1:27015" },
  { name: "Public Testing", mode: "Co-op", region: "US-Central", players: 3, cap: 64, ping: 60, locked: false, tag: "Open public test server", addr: "35.192.11.25:27015" },
  // Mock servers (placeholder until the live masterlist query lands).
  { name: "Hogsfield Roleplay", mode: "Roleplay", region: "EU-West", players: 142, cap: 200, ping: 24, locked: false, tag: "Story-driven RP · living economy", addr: "eu1.hogsfield.gg:7777" },
  { name: "The Forbidden Vale", mode: "PvP", region: "NA-East", players: 88, cap: 128, ping: 41, locked: false, tag: "Wand duels · ranked ladder", addr: "na.forbiddenvale.gg:7777" },
  { name: "Moonstone Academy", mode: "Co-op", region: "NA-West", players: 36, cap: 64, ping: 58, locked: true, tag: "Class progression · whitelist", addr: "na.moonstone.gg:7777" },
  { name: "Hollowmere Township", mode: "Roleplay", region: "EU-Central", players: 200, cap: 200, ping: 33, locked: false, tag: "Serious RP · faction politics", addr: "eu.hollowmere.gg:7777" },
  { name: "Duelling Pits", mode: "PvP", region: "EU-West", players: 19, cap: 40, ping: 22, locked: false, tag: "Free-for-all arena", addr: "arena.duellingpits.net:28015" },
  { name: "Greaves & Grindle Co.", mode: "Trade", region: "NA-East", players: 54, cap: 100, ping: 46, locked: false, tag: "Economy · crafting · auctions", addr: "na.greavesgrindle.gg:7777" },
  { name: "The Astronomy Tower", mode: "Co-op", region: "OCE", players: 12, cap: 32, ping: 121, locked: true, tag: "PvE dungeon crawl", addr: "oce.astronomytower.gg:7777" },
  { name: "Cinderfall Festival", mode: "Roleplay", region: "EU-West", players: 167, cap: 250, ping: 28, locked: false, tag: "Seasonal event server", addr: "eu.cinderfall.gg:7777" },
];

function deriveServer(s: RawServer): Server {
  const full = s.players >= s.cap;
  const [modeBg, modeBorder, modeText] = MODE_COLORS[s.mode] ?? MODE_COLORS.Roleplay;
  return {
    ...s,
    playersLabel: `${s.players} / ${s.cap}`,
    fillPct: `${Math.round((s.players / s.cap) * 100)}%`,
    full,
    pingColorHex: pingColor(s.ping),
    fillColor: full ? C.barFull : C.accent,
    modeBg,
    modeBorder,
    modeText,
  };
}

export const SERVERS: Server[] = RAW_SERVERS.map(deriveServer);

export interface NewsEntry {
  date: string;
  year: string;
  tag: string;
  cat: "UPDATE" | "HOTFIX" | "EVENT";
  title: string;
  body: string;
  catColor: string;
}

const RAW_NEWS: Omit<NewsEntry, "catColor">[] = [
  { date: "JUN 21", year: "2026", tag: "0.9.5", cat: "EVENT", title: "Attack On Hogwarts!", body: "The seasonal Attack On Hogwarts Event server will comment next Sunday, June 28 at 1PM EDT/5PM GMT." },
  { date: "JUN 20", year: "2026", tag: "0.9.4", cat: "UPDATE", title: "Appearance Synchronization", body: "Apperance synchronization is now supported--correctly presenting avatar and gear customization across all clients." },
  { date: "JUN 20", year: "2026", tag: "0.9.4", cat: "UPDATE", title: "Pregame Launcher", body: "You're now looking at the new pregame launcher which is now currently in alpha. This includes server selection or direction connection, news feed, and various settings." },
  { date: "JUN 19", year: "2026", tag: "0.9.4", cat: "UPDATE", title: "Skill Synchronization", body: "Spell synchronization is now supported--correctly presenting spell casts and direction across all clients." },
  { date: "APR 30", year: "2026", tag: "0.9.0", cat: "UPDATE", title: "Open Beta Begins", body: "HogwartsMP enters open beta. Thank you to everyone in the closed test — see the full notes for the complete changelog." },
];

export const NEWS: NewsEntry[] = RAW_NEWS.map((n) => ({
  ...n,
  catColor: CAT_COLORS[n.cat] ?? "#dab96e",
}));

export interface RecentConnection {
  name: string;
  addr: string;
  meta: string;
}

export const RECENTS: RecentConnection[] = [
  { name: "Hogsfield Roleplay", addr: "eu1.hogsfield.gg:7777", meta: "EU-West" },
  { name: "Duelling Pits", addr: "arena.duellingpits.net:28015", meta: "EU-West" },
  { name: "Moonstone Academy", addr: "na.moonstone.gg:7777", meta: "NA-West · password required" },
];

export type PageKey = "servers" | "direct" | "news" | "settings";
