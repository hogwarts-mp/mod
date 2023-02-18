sdk.on("resourceLoaded", (name) => {
    console.log(`Resource ${name} loaded! Let's do some wizardry!`);
});

sdk.on("playerConnected", (player) => {
  console.log(`Player ${player.getNickname()} is ready to cast some spells!`);
});
