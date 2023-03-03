<div align="center">
   <img src="https://user-images.githubusercontent.com/9026786/132325309-2e8ebecf-1154-45b2-b07a-ac9c0d3f6f94.png" alt="MafiaHUB" />
</div>

<div align="center">
    <a href="https://discord.gg/cka5vyWC"><img src="https://img.shields.io/discord/402098213114347520.svg" alt="Discord server" /></a>
</div>

<br />
<div align="center">
  Hogwarts Legacy: Advanced Multiplayer Edition
</div>

<div align="center">
  <sub>
    Brought to you by <a href="https://github.com/Segfaultd">@Segfault</a>,
    <a href="https://github.com/zaklaus">@zaklaus</a>,
    <a href="https://github.com/DavoSK">@DavoSK</a>,
    <a href="https://github.com/stevenklar">@StevenKlar</a>,
    and other contributors!
  </sub>
</div>
<hr/>

## Introduction

This is a multiplayer mod for Hogwart sLegacy. Publicly available so anyone can tinker around!

**NOTE:** This repository can **NOT** be compiled standalone and requires a special configuration provided by the framework itself. Follow the instructions on the [MafiaHub Framework](https://github.com/mafiahub/framework/) repository to learn how to use this project.

## Contributing

We're always looking for new contributors, so if you have any ideas or suggestions, please let us know and we'll see what we can do to make it better. You can either reach us at our Discord server [MafiaHub](https://discord.gg/c6gW9yRXZH) / [HogwartsMP](https://discord.gg/cka5vyWC), or by raising an issue on our repository.

If you're interested in development, please read our [Contribution Guidelines](https://github.com/MafiaHub/Framework/blob/develop/.github/CONTRIBUTING.md).

Please check the open [issues](https://github.com/hogwarts-mp/mod/issues) since we document everything there.

## Building

We use **CMake** to build our projects, so you can use any of the supported build systems. We support **Windows**, **Linux**, and **MacOS** operating systems at the moment. You can follow the following instructions to learn how to use this repository [here](https://github.com/MafiaHub/Framework#add-a-multi-player-project-to-the-framework)

## Current state

Actually, the mod isn't playable. It offers a stable and safe playground to experiment and reverse engineer the game:

- [x] DirectX 12 hooks for ImGUI rendering
- [x] Basic game structures reversing (world, players, components, containers etc...) under the SDK namespace
- [x] Fast game load
- [x] Game save bypass
- [x] Debug UI
- [x] Weather / time management (networked and script-exposed)
- [x] Battle-tested networking code with player entity basic information replication
- [x] Hooks
- [x] NodeJS scripting-enabled dedicated server

It does not cover (yet):

- [] Entity spawning (we had a prototype with cats but not working for players atm), we need to reverse StudentManager
- [] Gear synchronization
- [] World boundaries removal
- [] Web rendering for UIs


## License

The code is licensed under the [GPL V3](LICENSE.txt) license.
