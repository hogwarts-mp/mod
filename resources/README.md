# Writing HogwartsMP Server Scripts

This guide is for **server operators and modders** who want to write gameplay logic for a
HogwartsMP server. No C++ required — server scripts are plain **JavaScript**, run by the server in
a Node.js runtime, so most of the Node standard library (timers, `console`, etc.) is available.

> Looking for *how the scripting layer is built* or *what's planned next*? That's in the
> repo-root design docs (`SCRIPTING_PLUGIN_IMPROVEMENTS.md`). This file is the **author's manual**
> for the API that exists today.

---

## 1. Concepts

- A **resource** is a folder under the server's `resources/` directory containing a
  `package.json` manifest plus your script files. The server discovers and starts every resource
  on boot.
- Scripts are **server-authoritative**: they run on the server and tell clients what to do (spawn
  an NPC, change the weather, send a chat line). There is no client-side scripting yet.
- Logic is **event-driven**: you register handlers on the event bus (`Core.Events`) and react to
  players connecting, chatting, running commands, etc.

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
| `client` | Path to a client entry script. **Client scripting is not implemented yet** — listing client files only ships them to clients as assets; they are not executed. |
| `priority` | Higher loads later; use it to order resources. Default `0`. |
| `errorBehavior` | What to do if the resource throws: `"stop"` (default), `"restart"`, or `"continue"`. |
| `exports` | Array of names this resource exposes to other resources. |
| `resourceDependencies` | Array of resource names (or `{ "name", "version", "optional" }`) that must load first. |

---

## 4. Events

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

## 5. API reference

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
- `Environment.setWeather(name)` — set a weather preset by name (see the `wizard-test` resource for
  the list of valid preset names).
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

> v1 is a **single global namespace**. There is no per-player store yet, because the server has no
> stable identity that survives a reconnect. For now, key per-player data yourself with whatever
> stable id you have, e.g. `Storage.set("points:" + player.nickname, "10")`.

### `Human` (the player / NPC object)
Properties:
- `human.id` — numeric network id (stable for the entity's lifetime).
- `human.nickname` — display name (read-only).
- `human.position` — a **Vector3** (`.x`, `.y`, `.z`). See the footgun in §6.
- `human.rotation` — a **Vector3** of Euler angles in degrees.

Methods:
- `human.sendChat(message)` — send a chat line to this player.
- `human.kick(reason)` — disconnect this player (real players only).
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
