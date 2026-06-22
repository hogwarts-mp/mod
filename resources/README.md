# Writing HogwartsMP Scripts

This guide is for **server operators and modders** who want to write gameplay logic for a
HogwartsMP server. No C++ required — scripts are plain **JavaScript**. A resource can have a
**server** script (runs on the dedicated server in a Node.js runtime) and/or a **client** script
(runs in-game on each player's machine in a sandboxed V8). This guide covers the server API first
(§1–§7), then client scripts (§8).

> Looking for *how the scripting layer is built* or *what's planned next*? That's in the
> repo-root design docs (`SCRIPTING_PLUGIN_IMPROVEMENTS.md`). This file is the **author's manual**
> for the API that exists today.

---

## 1. Concepts

- A **resource** is a folder under the server's `resources/` directory containing a
  `package.json` manifest plus your script files. The server discovers and starts every resource
  on boot.
- Server scripts are **authoritative**: they run on the dedicated server and decide game logic
  (spawn an NPC, change the weather, send a chat line). **Client scripts** (§8) run in-game on each
  player and handle presentation + local reads; the two talk over a small event channel.
- Logic is **event-driven**: you register handlers on the event bus (`Core.Events`) and react to
  players connecting, chatting, running commands, etc.
- **Document your resource.** Each resource should ship its own `README.md` describing *how to use
  it* — its chat commands, config, and the events it emits/listens for. Keep it to the resource's own
  surface (the engine API lives here and in `types/`, so don't duplicate it). See
  [`gamemode/README.md`](gamemode/README.md) for the shape.

Where resources live: the server loads from `resources/` relative to its working directory
(in this repo that is `Framework/code/projects/mod/resources/`). The built-in `gamemode` resource
is the default example to copy.

---

## 2. Quick start — a minimal resource

Create `resources/my-gamemode/package.json`:

```json
{
  "name": "my-gamemode",
  "version": "1.0.0",
  "author": "you",
  "description": "My first HogwartsMP gamemode",
  "mafiahub": {
    "server": "server/main.js",
    "priority": 10
  }
}
```

Create `resources/my-gamemode/server/main.js`:

```js
const Events = Core.Events;

Events.on("playerConnect", (player) => {
    console.log(`${player.nickname} joined`);
    player.sendChat("Welcome to my server!");
});

Events.on("chatMessage", (player, message) => {
    // Relay chat to everyone.
    World.broadcastMessage(`${player.nickname}: ${message}`);
});
```

Start the server and your resource runs. (If you only want one gamemode active, give yours a higher
`priority` than the default `gamemode` resource, or remove that folder.)

---

## 3. `package.json` manifest reference

Top-level fields:

| Field | Required | Notes |
|---|---|---|
| `name` | ✅ | Unique resource name. |
| `version` | | Defaults to `"1.0.0"`. |
| `author` | | Free text. |
| `description` | | Free text. |
| `mafiahub` | | Object holding the framework-specific config below. |

Inside `mafiahub`:

| Field | Notes |
|---|---|
| `server` | Path to the server entry script, relative to the resource folder (e.g. `server/main.js`). |
| `client` | Path to a client entry script (e.g. `client/main.js`), relative to the resource folder. Runs in-game on each connecting player — see §8. |
| `priority` | Higher loads later; use it to order resources. Default `0`. |
| `errorBehavior` | What to do if the resource throws: `"stop"` (default), `"restart"`, or `"continue"`. |
| `exports` | Array of names this resource exposes to other resources. |
| `resourceDependencies` | Array of resource names (or `{ "name", "version", "optional" }`) that must load first. |

---

## 4. Server events

Subscribe with `Core.Events.on(eventName, handler)`. The events the server currently fires:

| Event | Handler arguments | Fires when |
|---|---|---|
| `playerConnect` | `(player)` | A player finishes connecting. |
| `playerDisconnect` | `(player)` | A player leaves. |
| `chatMessage` | `(player, message)` | A player sends a plain chat message. |
| `chatCommand` | `(player, message, command, args)` | A player sends `/command arg1 arg2 …`. `command` is the word after the slash; `args` is a string array. |

`player` is a **Human** object (see §5).

> `playerDied` exists in the engine but is not emitted yet (no server-side death detection). Don't
> rely on it.

---

## 5. Server API reference

These globals are available in every server script.

### `World`
- `World.broadcastMessage(message)` — send a chat line to every connected player.
- `World.sendChatMessage(human, message)` — send a chat line to one player.
- `World.getPlayers()` → **Human[]** — every connected player. Server-owned NPCs (from
  `spawnHuman`) are **not** included.
- `World.getPlayer(id)` → **Human | undefined** — the connected player with the given network id
  (`human.id`), or `undefined` if none.
- `World.getPlayerCount()` → number of connected players (cheaper than `getPlayers().length`).
- `World.spawnHuman(x, y, z)` → **Human** — spawn a server-owned NPC at a world position. Clients
  render it like any other player. Remove it with `human.destroy()`.

### `Environment`
- `Environment.setWeather(name)` — set a weather preset by name. (The `gamemode` resource keeps a
  validated list of valid preset names — see its README.)
- `Environment.setTime(hour, minute)` — `hour` 0–23, `minute` 0–59.
- `Environment.setDate(day, month)` — `day` 1–31, `month` 1–12.
- `Environment.setSeason(season)` — `0`=spring, `1`=summer, `2`=autumn, `3`=winter.

All four broadcast the change to every client.

### `Storage` — persistent key/value store
Survives server restarts (backed by `storage.json` in the server's working directory; every write
is flushed immediately). **Values are strings** — wrap structured data with `JSON.stringify` /
`JSON.parse`.

- `Storage.set(key, value)` — store a string value (overwrites any existing).
- `Storage.get(key)` → `string | undefined`.
- `Storage.has(key)` → `boolean`.
- `Storage.delete(key)` → `boolean` (true if a key was removed).
- `Storage.keys()` → `string[]`.

```js
// Store an object:
Storage.set("config", JSON.stringify({ pvp: true, maxPlayers: 50 }));
const config = JSON.parse(Storage.get("config") ?? "{}");

// A simple counter:
const n = (parseInt(Storage.get("visits") ?? "0", 10) || 0) + 1;
Storage.set("visits", String(n));
```

> `Storage` is a **single global namespace**. For data scoped to one player, use `player.getData` /
> `player.setData` (see `Human` below) — those persist against the player's **stable identity**
> (survives reconnect), so you don't have to key by the unstable `nickname` yourself.

### `Human` (the player / NPC object)
Properties:
- `human.id` — numeric network id (stable for the entity's lifetime).
- `human.nickname` — display name (read-only).
- `human.position` — a **Vector3** (`.x`, `.y`, `.z`). See the footgun in §6.
- `human.rotation` — a **Vector3** of Euler angles in degrees.

Methods:
- `human.sendChat(message)` — send a chat line to this player.
- `human.kick(reason)` — disconnect this player (real players only).
- **Per-player persistent data** — keyed to the player's stable identity (survives reconnect, unlike
  `id`/`nickname`); values are strings (use `JSON.stringify`/`parse`). No-op / `undefined` on
  server NPCs (no identity):
  - `human.getData(key)` → `string | undefined`
  - `human.setData(key, value)`
  - `human.hasData(key)` → `boolean`
  - `human.deleteData(key)` → `boolean`
- `human.destroy()` — despawn. Only affects **server-owned** entities (NPCs from
  `World.spawnHuman`); real players are managed by the network layer and ignore this.

### Node.js
Because the server runs Node, you also have `console.log`, `setTimeout`, `setInterval`,
`clearInterval`, etc. Timers are handy for periodic logic (e.g. an event countdown) — but note they
do **not** persist across a restart; combine with `Storage` for anything durable.

---

## 6. Gotchas

- **Mutating position/rotation:** assigning to a component (`human.position.x = 100`) only changes a
  throwaway JS copy and is a no-op. Read the vector, call `.set(...)`, then assign it back:
  ```js
  const pos = human.position;
  pos.set(pos.x + 100, pos.y, pos.z);
  human.position = pos;
  ```
- **`destroy()` on real players does nothing** — it is for NPCs you spawned. Use `kick()` to remove
  a real player.
- **`Storage` values are strings** — `Storage.set("n", 5)` will throw; use `String(5)`.
- **Paths are relative to the server's working directory** — `storage.json` and the `resources/`
  folder are resolved from wherever you launch the server, not from the resource folder.

---

## 7. A worked example — persistent House Points

```js
const Events = Core.Events;

function getPoints(house) {
    return parseInt(Storage.get("points:" + house) ?? "0", 10) || 0;
}
function addPoints(house, delta) {
    const total = getPoints(house) + delta;
    Storage.set("points:" + house, String(total));
    return total;
}

Events.on("chatCommand", (player, message, command, args) => {
    if (command === "points") {
        const house = (args[0] ?? "").toLowerCase();
        if (!house) {
            player.sendChat("Usage: /points <house> [amount]");
            return;
        }
        const amount = parseInt(args[1] ?? "0", 10) || 0;
        const total = amount ? addPoints(house, amount) : getPoints(house);
        World.broadcastMessage(`[HOUSE CUP] ${house} now has ${total} points.`);
    }
});
```

Because points live in `Storage`, the standings survive a server restart — the foundation for House
Cups, leaderboards, and the other persistent-world ideas.

To act on everyone currently online — e.g. an announcement that addresses each player, or tallying a
live scoreboard — iterate `World.getPlayers()`:

```js
Events.on("chatCommand", (player, message, command) => {
    if (command === "online") {
        const players = World.getPlayers();
        player.sendChat(`There are ${World.getPlayerCount()} wizards online:`);
        for (const p of players) {
            player.sendChat(` - ${p.nickname}`);
        }
    }
});
```

---

## 8. Client scripts

A resource's `mafiahub.client` entry runs **in-game on each connecting player**, in a sandboxed V8
engine — **not** Node. There's no `fs` / `net` / `child_process`; you get `console`, `setTimeout` /
`setInterval`, `require` (for local `.js` within the resource), and the builtins below. Client
scripts handle presentation and local reads; the server stays authoritative.

Authoritative signatures for everything below live in
[`types/hogwartsmp-client.d.ts`](types/hogwartsmp-client.d.ts) — `// @ts-check` + a `/// <reference>`
to it gives you autocomplete in plain `.js` (see `types/README.md`).

### `Game`
- `Game.notify(text)` — show a line in the local chat UI.
- `Game.emitServer(name, payloadJson)` — send a named event up to the server's scripts. `payloadJson`
  is JSON text (use `JSON.stringify`); the server receives it via `Core.Events.on(name, (player, payload))`.

### `LocalPlayer`
- `LocalPlayer.getPosition()` → `{ x, y, z } | null` — the local pawn's world position, or `null`
  before it has spawned (loading / menu / torn down).
- `LocalPlayer.getRotation()` → `{ pitch, yaw, roll } | null` — degrees.
- `LocalPlayer.getProp(path)` → `number | boolean | string | null | undefined` — generic reflection
  read of a property off the local pawn (scalars + name/string). `path` is a property name, or a
  **dotted path** that hops object properties to reach a component/sub-object, e.g.
  `getProp("HealthComponent.CurrentHealth")`. `null` = no pawn; `undefined` = not found or an
  unsupported type (structs/objects aren't read).
- `LocalPlayer.getPropNames(path?)` → `string[]` — property names of the pawn, or of the object reached
  by a dotted path (e.g. `getPropNames("HealthComponent")`). Use it to discover what `getProp` can read
  at each level (the list can be large).

### Receiving server events
`Core.Events.on(name, payload)` handles events the server sent. The handler gets a **single**
argument — the JSON-parsed payload (no `player`, since the event is addressed to this client):

```js
Core.Events.on("announce", (data) => {
    Game.notify(`[ANNOUNCE] ${data.text}`);
});
```

### The server ⇄ client event model
| Direction | Server side | Client side |
|---|---|---|
| Server → one player | `player.emit(name, json)` | `Core.Events.on(name, (payload) => …)` |
| Server → all clients | `World.emitAllClients(name, json)` | `Core.Events.on(name, (payload) => …)` |
| Client → server | `Core.Events.on(name, (player, payload) => …)` | `Game.emitServer(name, json)` |

Payloads cross the wire as JSON text: the sender passes `JSON.stringify(obj)` and the receiver gets
the parsed object back. An empty payload calls the handler with no payload argument; a malformed
(non-JSON) payload is dropped. **Client-emitted events are untrusted input** — any client can send any
name/payload, so validate them server-side and don't gate authoritative logic on them.

See `gamemode/client/main.js` for a working client script (handles `ping`/`announce`, reads the local
position, and emits back to the server).
