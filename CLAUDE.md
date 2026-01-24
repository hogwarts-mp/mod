# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

HogwartsMP is a multiplayer modification for Hogwarts Legacy built on the MafiaHub Framework. It enables networked multiplayer gameplay through game hooking, reverse engineering, and the framework's ECS-based synchronization.

**Status**: Experimental playground - basic networking works but entity spawning is incomplete.

## Build Commands

This project cannot be compiled standalone. It must be built from the Framework root:

```bash
# From E:\MafiaHub\Framework (not this directory)
cmake -B build
cmake --build build --target HogwartsMPClient   # Client DLL
cmake --build build --target HogwartsMPServer   # Server executable
```

**Windows only**: Client requires Visual Studio 2022 with CMake tools. Open the Framework folder in VS.

## Project Structure

```
code/
├── client/src/
│   ├── main.cpp              # DLL entry, hooks _get_narrow_winmain_command_line
│   ├── core/
│   │   ├── application.h/cpp # Main client class (extends Framework::Integrations::Client::Instance)
│   │   ├── hooks/            # UE4 function hooks (dx12, engine tick, world, player)
│   │   ├── states/           # State machine (Initialize→Menu→Session→Shutdown)
│   │   ├── ui/               # ImGui panels (console, chat, teleport, season)
│   │   ├── modules/          # ECS modules for entity sync
│   │   └── dev_features.cpp  # Debug tools and UI
│   ├── game/
│   │   └── game_input.h/cpp  # Keyboard input mapping
│   └── sdk/                  # Reversed Unreal Engine 4 structures
│       ├── entities/         # ABiped_Player, ABiped_Character, APlayerController
│       ├── components/       # UE4 component types
│       ├── containers/       # TArray, TMap, FString, etc.
│       └── game/             # SeasonChanger, UScheduler
│
├── server/src/
│   ├── main.cpp
│   └── core/
│       ├── server.h/cpp      # Server instance (extends Framework::Integrations::Server::Instance)
│       ├── modules/human.h   # Human entity ECS module
│       └── builtins/         # Lua scripting API extensions
│
└── shared/
    ├── modules/
    │   ├── mod.hpp           # Shared ECS components (Mod_Human, etc.)
    │   └── human_sync.hpp    # Entity sync components
    ├── messages/human/       # Network messages (spawn, despawn, update)
    └── rpc/                  # RPCs (ChatMessage, SetWeather)
```

## Architecture

### Client Injection Flow

1. DLL injected into Hogwarts Legacy process
2. `DLL_PROCESS_ATTACH` hooks `_get_narrow_winmain_command_line` (Win32 CRT)
3. Hook installs engine tick callback before game main loop starts
4. `Application::Init()` sets up Framework systems + UE4 hooks
5. State machine manages game flow: Initialize → Menu → SessionConnection → SessionConnected

### Key Hooks (client/src/core/hooks/)

| Hook | Purpose |
|------|---------|
| `dx12_pointer_grab.cpp` | Extracts D3D12 device/swapchain for ImGui rendering |
| `engine.cpp` | Engine tick - main update loop integration |
| `world.cpp` | UWorld initialization, GObjects array detection |
| `player_controller.cpp` | Player controller spawn/destroy events |
| `localplayer.cpp` | Local player viewport setup |

### SDK Structure (client/src/sdk/)

Reversed Unreal Engine 4 types. Key classes:

- `UObject` - Base for all Unreal objects
- `UWorld` - Game world with persistent level
- `ABiped_Player` - Player character with components
- `APlayerController` - Input controller
- `ULocalPlayer` - Local player with viewport
- `SeasonChanger` - Weather/time system
- `UScheduler` - Game scheduler/timing

### Entity Synchronization

**Messages** (shared/messages/human/):
- `MOD_HUMAN_SPAWN` - Spawn networked entity
- `MOD_HUMAN_DESPAWN` - Remove entity
- `MOD_HUMAN_UPDATE` - Position/state update from server
- `MOD_HUMAN_SELF_UPDATE` - Local player state to server

**ECS Components** (shared/modules/):
- `Mod_Human` - Base human entity component
- `HumanClientComponent` - Client-side interpolation state

### State Machine States

Located in `client/src/core/states/`:

| State | Description |
|-------|-------------|
| `Initialize` | Load resources, detect UE4 objects |
| `Menu` | Main menu interaction |
| `SessionConnection` | Connecting to server |
| `SessionConnected` | Active multiplayer session |
| `SessionDisconnection` | Cleanup after disconnect |
| `SessionOfflineDebug` | Debug offline mode |
| `Shutdown` | Graceful shutdown |

## Key Patterns

### Accessing Game Objects

```cpp
// Get local player character
auto* localPlayer = SDK::GetLocalPlayer();
auto* playerController = localPlayer->GetPlayerController();
auto* character = static_cast<SDK::ABiped_Player*>(playerController->GetCharacter());
```

### Weather/Time Control

```cpp
// Set season (Spring=0, Summer=1, Autumn=2, Winter=3)
SDK::SeasonChanger::Get()->SetSeason(SDK::ESeason::Summer);

// Set time
SDK::UScheduler::Get()->SetTimeOfDay(11, 0); // 11:00 AM
```

### Adding ImGui UI

Inherit from `UIBase` in `core/ui/`:

```cpp
class MyUI : public UIBase {
public:
    void Update() override {
        if (ImGui::Begin("My Window")) {
            // UI code
        }
        ImGui::End();
    }
};
```

Register in `DevFeatures::Init()`.

## Code Style

- Namespaces: `HogwartsMP::Core::`, `HogwartsMP::Shared::`, `SDK::`
- SDK types use UE4 naming (prefixes: U, A, F, T, E)
- Private members: `_` prefix
- Follow Framework's `.clang-format`

## Current Limitations

- Entity spawning incomplete (needs StudentManager reversing)
- No gear/spell/broomstick sync
- No web UI rendering (Ultralight not integrated)
- World boundaries not removed
