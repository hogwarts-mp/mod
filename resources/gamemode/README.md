# gamemode

The default HogwartsMP gamemode: chat relay, environment controls, a few dev tools, and a working
demo of server↔client scripted events. It's the resource to copy when starting your own.

This is a **how-to-use** guide for the resource. For the scripting API itself (`World`, `Game`,
`Storage`, `Core.Events`, …) see [`../README.md`](../README.md) and the type defs in
[`../types/`](../types/).

## What it does
- Relays player chat to everyone (`nickname: message`).
- Welcomes players on join and tracks a persistent **visit counter** (stored in `storage.json` under
  the key `visits`, so it survives restarts).
- Exposes the chat commands below.
- On the client side, shows a "scripting is live" line on connect and reacts to the server's `ping`
  / `announce` events.

## Chat commands
| Command | Usage | Effect |
|---|---|---|
| `/weather` | `/weather <setName>` | Set the weather preset (names listed in the `wizard-test` resource). |
| `/time` | `/time <hour 0-23> [minute 0-59]` | Set the in-game time of day. |
| `/date` | `/date <day 1-31> <month 1-12>` | Set the in-game date. |
| `/season` | `/season <spring\|summer\|autumn\|winter or 0-3>` | Change the season. |
| `/ping` | `/ping` | Round-trip demo: server → your client (shows a "pong" + your position) → back to server. |
| `/announce` | `/announce <text>` | Broadcast a message to every client's HUD/notify. |

## Events (interop surface)
- **Listens (client → server):** `clientReady` — sent by this resource's client script in response to
  `/ping`; the server acknowledges with a chat line.
- **Emits (server → client):** `ping` (to the requesting player) and `announce` (broadcast). The
  client script handles both via `Core.Events.on(...)`.

## Persistent state
- `storage.json` → `visits`: total connections counted since the store was created.

## Files
- `package.json` — manifest (`mafiahub.server` / `mafiahub.client` entry points).
- `server/main.js` — server logic (chat, commands, events).
- `client/main.js` — client logic (notifications, server-event handlers, local-player read).

To extend it, edit those scripts against the scripting API — start from [`../README.md`](../README.md).
