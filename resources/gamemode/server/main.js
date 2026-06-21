/**
 * Default HogwartsMP gamemode - main server script.
 * Relays chat and exposes environment commands.
 *
 * Available APIs (registered by the server):
 *   World.broadcastMessage(message)
 *   World.sendChatMessage(human, message)
 *   Environment.setWeather(name) / setTime(h, m) / setDate(d, m) / setSeason(0-3)
 *   Framework.Human - player entity class (nickname, position, rotation, sendChat)
 *   Storage.get(key) / set(key, value) / has(key) / delete(key) / keys()
 *     - persistent key/value store; values are strings (use JSON.stringify/parse for objects).
 */

console.log("[GAMEMODE] Script loading...");

// The framework exposes the event bus on the Core global
const Events = Core.Events;

// Persistent across restarts via the Storage builtin (storage.json). Demonstrates the persistence
// layer the server-concept ideas (House Points, leaderboards, NPC memory) build on.
function bumpVisitCount() {
    const next = (parseInt(Storage.get("visits") ?? "0", 10) || 0) + 1;
    Storage.set("visits", String(next));
    return next;
}

const SEASONS = { spring: 0, summer: 1, autumn: 2, winter: 3 };

// Known weather-set names (game asset names, case-sensitive). /weather validates against this so a
// typo gives clear feedback instead of silently applying nothing. This originated from an old test
// resource and may not be exhaustive — add a name here if you confirm a valid preset is missing.
const WEATHER_PRESETS = new Set([
    "Clear",
    "Default_PHY",
    "Announce",
    "Astronomy",
    "Intro_01",
    "MKT_Nov11",
    "LightClouds_01",
    "LightRain_01",
    "Rainy",
    "Misty_01",
    "MistyOvercast_01",
    "Overcast_01",
    "Overcast_Heavy_01",
    "Overcast_Windy_01",
    "Stormy_01",
    "StormyLarge_01",
    "FIG_07_Storm",
    "TestStormShort",
    "TestWind",
    "HighAltitudeOnly",
    "ForbiddenForest_01",
    "Sanctuary_Bog",
    "Sanctuary_Coastal",
    "Sanctuary_Forest",
    "Sanctuary_Grasslands",
    "Summer_Overcast_Heavy_01",
    "Overcast_Heavy_Winter_01",
    "Winter_Misty_01",
    "Winter_Overcast_01",
    "Winter_Overcast_Windy_01",
    "Snow_01",
    "Snow_Const",
    "SnowLight_01",
    "SnowShort",
]);

// --- Dev: server-spawned NPCs ---
// World.spawnHuman(x, y, z) returns a server-owned human the clients render via
// the student-proxy path; npc.destroy() despawns it. Also doubles as a
// single-client test of the remote-avatar flow (spawn -> dress -> move ->
// despawn) without a second client.
//
// NOTE: dev scaffolding for a single tester — npcs/timer are module-global, not
// per-player, so concurrent testers would step on each other. Fine for now.
const npcs = [];
const MAX_NPCS = 20;
let npcWalkTimer = null;

Events.on("playerConnect", (player) => {
    const visits = bumpVisitCount();
    console.log(`[GAMEMODE] ${player.nickname} connected (visit #${visits})`);
    player.sendChat("[SERVER] Welcome to HogwartsMP! Commands: /weather /time /date /season");
    player.sendChat(`[SERVER] You are visitor #${visits} since the server started keeping count.`);
});

Events.on("playerDisconnect", (player) => {
    console.log(`[GAMEMODE] ${player.nickname} disconnected`);
});

Events.on("chatMessage", (player, message) => {
    World.broadcastMessage(`${player.nickname}: ${message}`);
});

// Client -> server event (sent from the client gamemode's /ping handler via Game.emitServer).
// Receives (player, payload); proves the up-direction of scripted messaging.
Events.on("clientReady", (player, data) => {
    console.log(`[GAMEMODE] clientReady from ${player.nickname}: ${JSON.stringify(data)}`);
    player.sendChat("[SERVER] received your client event — round-trip OK");
});

Events.on("chatCommand", (player, message, command, args) => {
    switch (command) {
        case "weather": {
            const name = args[0];
            if (!name) {
                player.sendChat("Usage: /weather <setName>");
                break;
            }
            if (!WEATHER_PRESETS.has(name)) {
                player.sendChat(`[SERVER] Unknown weather preset '${name}'. See the gamemode README for valid names.`);
                break;
            }
            Environment.setWeather(name);
            World.broadcastMessage(`[SERVER] Weather changed to ${name}`);
            break;
        }

        case "time": {
            const hour = parseInt(args[0], 10);
            const minute = parseInt(args[1] ?? "0", 10);
            if (isNaN(hour) || hour < 0 || hour > 23 || isNaN(minute) || minute < 0 || minute > 59) {
                player.sendChat("Usage: /time <hour 0-23> [minute 0-59]");
                break;
            }
            Environment.setTime(hour, minute);
            World.broadcastMessage(`[SERVER] Time set to ${hour}:${String(minute).padStart(2, "0")}`);
            break;
        }

        case "date": {
            const day = parseInt(args[0], 10);
            const month = parseInt(args[1], 10);
            if (isNaN(day) || day < 1 || day > 31 || isNaN(month) || month < 1 || month > 12) {
                player.sendChat("Usage: /date <day 1-31> <month 1-12>");
                break;
            }
            Environment.setDate(day, month);
            World.broadcastMessage(`[SERVER] Date set to ${day}/${month}`);
            break;
        }

        case "season": {
            const season = SEASONS[args[0]?.toLowerCase()] ?? parseInt(args[0], 10);
            if (isNaN(season) || season < 0 || season > 3) {
                player.sendChat("Usage: /season <spring|summer|autumn|winter or 0-3>");
                break;
            }
            Environment.setSeason(season);
            World.broadcastMessage(`[SERVER] Season changed`);
            break;
        }

        case "ping": {
            // Server -> this client's scripts: player.emit(name, jsonPayload). The client gamemode
            // listens for "ping" and replies in-game (a HUD/chat notify), proving the reactive path.
            player.emit("ping", JSON.stringify({ time: Date.now(), from: player.nickname }));
            break;
        }

        case "announce": {
            // Server -> every client's scripts: World.emitAllClients broadcasts the event.
            const text = args.join(" ") || "Hello from the server!";
            World.emitAllClients("announce", JSON.stringify({ text }));
            break;
        }

        case "spawnnpc": {
            if (npcs.length >= MAX_NPCS) {
                player.sendChat(`[DEV] NPC limit reached (${MAX_NPCS}) — /clearnpcs first`);
                break;
            }
            const p = player.position;
            // Spawn ~1 m to the side so it's right next to the player.
            const npc = World.spawnHuman(p.x + 100, p.y, p.z);
            npcs.push(npc);
            player.sendChat(`[DEV] Spawned NPC #${npcs.length} next to you`);
            break;
        }

        case "walknpcs": {
            if (npcWalkTimer) {
                clearInterval(npcWalkTimer);
                npcWalkTimer = null;
                player.sendChat("[DEV] NPCs stopped");
                break;
            }
            if (npcs.length === 0) {
                player.sendChat("[DEV] No NPCs to walk — use /spawnnpc first");
                break;
            }
            // Orbit each NPC around an absolute center (the player's current spot)
            // with a wide radius so the motion is unmistakable. Setting absolute
            // positions avoids the tiny oscillation a += accumulator produced.
            const c = player.position;
            const center = { x: c.x, y: c.y, z: c.z };
            const RADIUS = 300; // ~3 m
            let angle = 0;
            npcWalkTimer = setInterval(() => {
                angle += 0.05;
                for (let i = 0; i < npcs.length; i++) {
                    const phase = angle + (i * 2 * Math.PI) / npcs.length;
                    // Vector3.set() — assigning pos.x/.y/.z directly only writes a
                    // JS shadow (x/y/z are SetNativeDataProperty), leaving the
                    // underlying C++ vector unchanged, so the move would be a no-op.
                    const pos = npcs[i].position;
                    pos.set(center.x + Math.cos(phase) * RADIUS, center.y + Math.sin(phase) * RADIUS, center.z);
                    npcs[i].position = pos;
                }
            }, 50);
            player.sendChat(`[DEV] Walking ${npcs.length} NPC(s) in a circle (run /walknpcs again to stop)`);
            break;
        }

        case "clearnpcs": {
            if (npcWalkTimer) {
                clearInterval(npcWalkTimer);
                npcWalkTimer = null;
            }
            for (const npc of npcs) {
                npc.destroy();
            }
            const n = npcs.length;
            npcs.length = 0;
            player.sendChat(`[DEV] Despawned ${n} NPC(s)`);
            break;
        }

        default:
            player.sendChat(`Unknown command: /${command}`);
    }
});

console.log("[GAMEMODE] Script loaded");
