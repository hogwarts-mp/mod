sdk.on("resourceLoaded", (name) => {
    console.log(`Resource ${name} loaded! Let's do some wizardry!`);
});

sdk.on("playerConnected", (player) => {
  console.log(`Player ${player.getNickname()} is ready to cast some spells!`);
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
