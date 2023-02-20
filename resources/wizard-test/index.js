// weather presets

const SEASON_SPRING = 0;
const SEASON_SUMMER = 1;
const SEASON_FALL = 2;
const SEASON_WINTER = 3;

const WEATHER_SPRING = [
    "Clear",
    "Overcast_Heavy_Winter_01",
    "Winter_Misty_01",
    "Winter_Overcast_01",
    "Winter_Overcast_Windy_01",
    "Snow_01",
    "Snow_Const",
    "SnowLight_01",
    "SnowShort",
];

const WEATHER_SUMMER = [
    "Summer_Overcast_Heavy_01",
    "Clear",
    "Announce",
    "Astronomy",
    "Default_PHY",
    "FIG_07_Storm",
    "ForbiddenForest_01",
    "HighAltitudeOnly",
    "Intro_01",
    "LightClouds_01",
    "LightRain_01",
    "Misty_01",
    "MistyOvercast_01",
    "MKT_Nov11",
    "Overcast_01",
    "Overcast_Heavy_01",
    "Overcast_Windy_01",
    "Rainy",
    "Sanctuary_Bog",
    "Sanctuary_Coastal",
    "Sanctuary_Forest",
    "Sanctuary_Grasslands",
    "Stormy_01",
    "StormyLarge_01",
    "TestStormShort",
    "TestWind",
];

const WEATHER_FALL = [
    "Clear",
    "Announce",
    "Astronomy",
    "Default_PHY",
    "FIG_07_Storm",
    "ForbiddenForest_01",
    "HighAltitudeOnly",
    "Intro_01",
    "LightClouds_01",
    "LightRain_01",
    "Misty_01",
    "MistyOvercast_01",
    "MKT_Nov11",
    "Overcast_01",
    "Overcast_Heavy_01",
    "Overcast_Windy_01",
    "Rainy",
    "Sanctuary_Bog",
    "Sanctuary_Coastal",
    "Sanctuary_Forest",
    "Sanctuary_Grasslands",
    "Stormy_01",
    "StormyLarge_01",
    "TestStormShort",
    "TestWind",
];

const WEATHER_WINTER = [
    "Clear",
    "Announce",
    "Astronomy",
    "Default_PHY",
    "FIG_07_Storm",
    "ForbiddenForest_01",
    "HighAltitudeOnly",
    "Intro_01",
    "LightClouds_01",
    "LightRain_01",
    "Misty_01",
    "MistyOvercast_01",
    "MKT_Nov11",
    "Overcast_01",
    "Overcast_Heavy_01",
    "Overcast_Windy_01",
    "Rainy",
    "Sanctuary_Bog",
    "Sanctuary_Coastal",
    "Sanctuary_Forest",
    "Sanctuary_Grasslands",
    "Stormy_01",
    "StormyLarge_01",
    "TestStormShort",
    "TestWind",
];

sdk.on("resourceLoaded", (name) => {
    console.log(`Resource ${name} loaded! Let's do some wizardry!`);
});

sdk.on("playerConnected", (player) => {
  console.log(`Player ${player.getNickname()} is ready to cast some spells!`);

  sdk.Environment.setTime(12, 0);
  sdk.Environment.setDate(12, 6);
  sdk.Environment.setSeason(SEASON_SUMMER);
  sdk.Environment.setWeather(WEATHER_SUMMER[0]);
});

sdk.on("chatMessage", (player, message) => {
    console.log(`Player ${player.getNickname()} said: ${message}`);
    sdk.broadcastMessage(`<${player.getNickname()}>: ${message}`);
});

sdk.on("chatCommand", (player, message, command, args) => {
    console.log(`Player ${player.getNickname()} used: ${command}`);

    if (command === "foo") {
        sdk.broadcastMessage(`Player ${player.getNickname()} used /foo!`);
    } else if (command === "showArgs") {
        sdk.broadcastMessage(`Player ${player.getNickname()} used /showArgs with args: ${args}`);
    }
});
