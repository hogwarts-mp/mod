/**
 * Default HogwartsMP gamemode - main server script.
 * Relays chat and exposes environment commands.
 *
 * Available APIs (registered by the server):
 *   World.broadcastMessage(message)
 *   World.sendChatMessage(human, message)
 *   Environment.setWeather(name) / setTime(h, m) / setDate(d, m) / setSeason(0-3)
 *   Framework.Human - player entity class (nickname, position, rotation, sendChat)
 */

console.log("[GAMEMODE] Script loading...");

// The framework exposes the event bus on the Core global
const Events = Core.Events;

const SEASONS = { spring: 0, summer: 1, autumn: 2, winter: 3 };

Events.on("playerConnect", (player) => {
    console.log(`[GAMEMODE] ${player.nickname} connected`);
    player.sendChat("[SERVER] Welcome to HogwartsMP! Commands: /weather /time /date /season");
});

Events.on("playerDisconnect", (player) => {
    console.log(`[GAMEMODE] ${player.nickname} disconnected`);
});

Events.on("chatMessage", (player, message) => {
    World.broadcastMessage(`${player.nickname}: ${message}`);
});

Events.on("chatCommand", (player, message, command, args) => {
    switch (command) {
        case "weather":
            if (args.length < 1) {
                player.sendChat("Usage: /weather <setName>");
                break;
            }
            Environment.setWeather(args[0]);
            World.broadcastMessage(`[SERVER] Weather changed to ${args[0]}`);
            break;

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

        default:
            player.sendChat(`Unknown command: /${command}`);
    }
});

console.log("[GAMEMODE] Script loaded");
