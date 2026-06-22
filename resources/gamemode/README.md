# gamemode

The default HogwartsMP gamemode: chat relay, environment controls, a few dev tools, and a working
demo of server↔client scripted events. It's the resource to copy when starting your own.

This is a **how-to-use** guide for the resource. For the scripting API itself (`World`, `Game`,
`Storage`, `Core.Events`, …) see [`../README.md`](../README.md) and the type defs in
[`../types/`](../types/).

## What it does
- Relays player chat to everyone (`nickname: message`).
- Welcomes players on join and tracks both a **global** visit counter (total connections) and a
  **per-player** one (keyed to the player's stable identity, so it survives their reconnect). Both
  persist in `storage.json`.
- Exposes the chat commands below.
- On the client side, shows a "scripting is live" line on connect and reacts to the server's `ping`
  / `announce` events.

## Chat commands
| Command | Usage | Effect |
|---|---|---|
| `/weather` | `/weather <setName>` | Set the weather preset (validated against the list below; case-sensitive). |
| `/time` | `/time <hour 0-23> [minute 0-59]` | Set the in-game time of day. |
| `/date` | `/date <day 1-31> <month 1-12>` | Set the in-game date. |
| `/season` | `/season <spring\|summer\|autumn\|winter or 0-3>` | Change the season. |
| `/ping` | `/ping` | Round-trip demo: server → your client (shows a "pong" + your position) → back to server. |
| `/announce` | `/announce <text>` | Broadcast a message to every client's HUD/notify. |

### Weather presets
Valid `/weather` names (case-sensitive; defined in `server/main.js` as `WEATHER_PRESETS`). This list
came from an older test resource and may not be exhaustive — extend it if you confirm a missing one.

`Clear`, `Default_PHY`, `Announce`, `Astronomy`, `Intro_01`, `MKT_Nov11`, `LightClouds_01`,
`LightRain_01`, `Rainy`, `Misty_01`, `MistyOvercast_01`, `Overcast_01`, `Overcast_Heavy_01`,
`Overcast_Windy_01`, `Stormy_01`, `StormyLarge_01`, `FIG_07_Storm`, `TestStormShort`, `TestWind`,
`HighAltitudeOnly`, `ForbiddenForest_01`, `Sanctuary_Bog`, `Sanctuary_Coastal`, `Sanctuary_Forest`,
`Sanctuary_Grasslands`, `Summer_Overcast_Heavy_01`, `Overcast_Heavy_Winter_01`, `Winter_Misty_01`,
`Winter_Overcast_01`, `Winter_Overcast_Windy_01`, `Snow_01`, `Snow_Const`, `SnowLight_01`, `SnowShort`

## Events (interop surface)
- **Listens (client → server):** `clientReady` — sent by this resource's client script in response to
  `/ping`; the server acknowledges with a chat line.
- **Emits (server → client):** `ping` (to the requesting player) and `announce` (broadcast). The
  client script handles both via `Core.Events.on(...)`.

## Persistent state
- `storage.json` → `visits`: total connections counted since the store was created.
- Per-player visit count via `player.setData("visits", …)` (stored under the player's stable identity).

## Files
- `package.json` — manifest (`mafiahub.server` / `mafiahub.client` entry points).
- `server/main.js` — server logic (chat, commands, events).
- `client/main.js` — client logic (notifications, server-event handlers, local-player read).

To extend it, edit those scripts against the scripting API — start from [`../README.md`](../README.md).
