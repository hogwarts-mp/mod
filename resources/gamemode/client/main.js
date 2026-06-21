/**
 * Default HogwartsMP gamemode - client script.
 * Runs in-game in the sandboxed client V8 engine (no fs/net; require/timers/console available).
 *
 * Client-side APIs:
 *   Core.Events.on(name, handler) - event bus (also receives server EmitLuaEvent events)
 *   Game.notify(text)             - show a line in the local chat UI
 *   console.log(...)              - logs to the client console (F8)
 */

console.log("[CLIENT] gamemode client script loaded");

// Proves the end-to-end path: a client resource started -> a HogwartsMP client builtin -> the game UI.
Game.notify("[CLIENT] HogwartsMP client scripting is live!");

// Server-driven events: the server sends these via player.emit / World.emitAllClients; the payload
// is JSON.parsed into this single handler argument. Try /ping and /announce in chat.
Core.Events.on("ping", (data) => {
    Game.notify(`[CLIENT] pong! server time ${data.time} (from ${data.from})`);
    // Reply up to the server's scripts (client -> server), completing the round-trip.
    Game.emitServer("clientReady", JSON.stringify({ ok: true }));
});

Core.Events.on("announce", (data) => {
    Game.notify(`[ANNOUNCE] ${data.text}`);
});
