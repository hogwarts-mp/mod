#pragma once

// Central, version-keyed catalog of everything that breaks when Hogwarts Legacy
// is patched: AOB byte patterns and struct field offsets.
//
// Why this exists: a single game update recompiled the exe and shifted ~half the
// mod's hooks AND a struct offset, which used to be
// scattered across playground.cpp / world.cpp / ue4_impl.cpp / render_device.cpp /
// player_controller.cpp / localplayer.cpp / the SDK headers. Consolidating them
// here means a new build is ONE GameLayout to add, every call site reads from it,
// and old builds are kept as snapshots for diffing / posterity.
//
// One catalog entry = one complete snapshot of a build's version-specific data.
// `gLayout` selects the active build. Switching it changes BOTH the runtime
// pattern scans (via core/aob_scan.h) and the compile-time SDK struct layout
// (sdk/** pad arrays derive from Offset::, below) — so the whole struct is
// constexpr and header-only.
//
// `optional` marks a hook with no downstream consumer (logging/trace/detection
// or a dev redirect): a miss logs [warn] instead of [error].

#include <cstdint>

namespace HogwartsMP::Game {
    // One IDA-style AOB pattern + its scan metadata.
    struct Aob {
        const char *name;    // log/debug label, e.g. "World/GObjectArray"
        const char *pattern; // space-separated bytes, '?' = wildcard
        bool optional;       // true => no consumer yet; miss logs [warn] not [error]
    };

    // Struct field offsets (compile-time; they size the SDK class pads).
    struct Offsets {
        uint32_t UWorld_PersistentLevel;     // UWorld -> ULevel*
        uint32_t UWorld_OwningGameInstance;  // UWorld -> UGameInstance*
        uint32_t UGameInstance_LocalPlayers; // UGameInstance -> TArray<ULocalPlayer*>
    };

    // Everything specific to one Hogwarts Legacy build.
    struct GameLayout {
        const char *version; // human label for the build this snapshot targets

        // --- Core object access (CRITICAL: FindUObject / world / memory) ---
        Aob gObjectArray;          // lea rcx,[GUObjectArray]; resolves to FUObjectArray*
        Aob gWorld;                // mov rbx,[GWorld]; resolves to UWorld**
        Aob gMalloc;               // resolves to FMalloc*
        Aob fnameToString;         // void(const FName*, FString& out)
        Aob uworldSpawnActor;      // AActor*(UWorld*, UClass*, FVector const*, FRotator const*, FActorSpawnParameters const&)
        Aob uworldDestroyActor;    // bool*(UWorld*, AActor*, bool, bool)
        Aob staticConstructObject; // UObject*(FStaticConstructObjectParameters const&)
        Aob staticLoadObject;      // UObject*(UClass*, UObject* outer, const TCHAR* name, ...) — load asset by path
        Aob engineTick;            // engine main-tick (call site)

        // --- Pak mounting (inject our Paks folder into UE's startup scan; hooks/pak_mount.cpp) ---
        Aob getPakFolders;  // FPakPlatformFile::GetPakFolders(self, TArray<FString>* out)
        Aob fmemoryRealloc; // void*(void* ptr, size_t size, uint32 alignment) — grow the folder array

        // --- Rendering (window + device for the ImGui overlay) ---
        Aob fwindowsWindowInitialize;  // FWindowsWindow::Initialize
        Aob fwindowsAppProcessMessage; // FWindowsApplication::ProcessMessage (call site) — ImGui input
        Aob fd3d12CreateRootDevice;    // FD3D12Adapter::CreateRootDevice

        // --- Optional: logging/trace/detection + dev redirect (no consumer yet) ---
        Aob uengineLoadMap;   // dev redirect RootLevel -> Overland
        Aob uworldCtor;       // UWorld::UWorld trace
        Aob apcBeginPlay;     // APlayerController::BeginPlay (join/respawn detection)
        Aob apcEndPlay;       // APlayerController::EndPlay
        Aob ulocalplayerCtor; // ULocalPlayer::ULocalPlayer (call site)
        Aob apcCtor;          // APlayerController::APlayerController (call site)

        Offsets offsets;
    };

    // ════════════════════════════════════════════════════════════════════════
    //  Build catalog — newest first. Add a new entry when the game patches.
    // ════════════════════════════════════════════════════════════════════════

    // Current Steam build — buildid 20773316, dated 2025-12-21. Patterns/offsets
    // re-derived 2026-06 against Phoenix-Win64-Shipping.exe (image base
    // 0x140000000) via runtime SEH probes + capstone.
    inline constexpr GameLayout kHL_2025_12 = {
        "Steam buildid 20773316 (2025-12-21)",

        // Core object access
        {"World/GObjectArray", "48 8D 0D ? ? ? ? E8 ? ? ? ? 48 8D 8D 90 03 00 00", false},
        {"World/GWorld", "48 8B 1D ? ? ? ? 48 85 DB 74 3B 41 B0 01", false},
        {"UE4/GMalloc", "48 8B 0D ? ? ? ? 48 85 C9 75 0C E8 ? ? ? ? 48 8B 0D ? ? ? ? 48 8B 01 48 8B D3 FF 50 ? 48 83 C4 20", false},
        {"UE4/FName::ToString", "48 89 5C 24 18 48 89 74 24 20 57 48 83 EC 20 8B 01 48 8B DA 8B F8 44 0F B7 C0 C1", false},
        // UWorld::SpawnActor — the FVector+FRotator overload
        //   AActor*(UWorld*, UClass*, FVector const*, FRotator const*, FActorSpawnParameters const&)
        // Re-derived via Ghidra (FUN_145868e90, 2026-06): the engine/console spawn
        // path and UE4SS use THIS overload. The old pattern matched the FTransform
        // overload, which is the WRONG function on this build (returned null).
        {"Playground/UWorld::SpawnActor", "40 53 56 57 48 83 EC 70 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 60 0F 28 1D", false},
        {"Playground/UWorld::DestroyActor", "40 53 56 57 41 54 41 55 41 57 48 81 EC 18", false}, // verified: despawn works
        {"Playground/StaticConstructObject_Internal", "48 89 5C 24 10 48 89 74 24 18 55 57 41 54 41 56 41 57 48 8D AC 24 50 FF FF FF", false}, // unverified (hooked, no-op)
        {"Core/StaticLoadObject", "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 88 FC FF FF 48 81 EC 78 04 00 00 48 8B 05", false}, // verified: assets load
        {"Engine/EngineTick", "E8 ? ? ? ? 80 3D ? ? ? ? ? 74 EB", false}, // verified: tick runs

        // Pak mounting (stock engine fns — inject <exeDir>\Paks into UE's startup scan)
        {"Pak/FPakPlatformFile::GetPakFolders", "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 4C 89 74 24 20 55 48 8B EC 48 83 EC 40 48 8D 4D F0", false},
        {"Pak/FMemory::Realloc", "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B F1 41 8B D8 48 8B 0D ? ? ? ? 48 8B FA", false},

        // Rendering — !! both render hooks below are STALE (match the WRONG
        // function; their handlers never fire). The overlay is brought up by the
        // EngineTick DX12 bootstrap instead (render_device.cpp). TODO re-derive.
        {"Render/FWindowsWindow::Initialize", "4C 8B DC 53 55 56 41 54 41 55 41 57 48 81 EC 98 00 00 00", false},
        {"Render/FWindowsApplication::ProcessMessage", "E8 ? ? ? ? 48 8B 5C 24 ? 48 8B 6C 24 ? 48 8B 74 24 ? 48 98", false}, // verified: input works
        {"Render/FD3D12Adapter::CreateRootDevice", "40 55 41 54 41 57 48 8D AC 24 10 FF FF FF 48 81 EC F0 01 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 44 0F B6 FA", false},

        // Optional (no consumer yet -> [warn] on miss)
        {"Playground/UEngine::LoadMap", "48 89 5C 24 20 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 A0 FE FF FF 48 81 EC 60 02 00 00 0F", true},
        {"World/UWorld::UWorld", "40 53 56 57 48 83 EC 20 4C 89 74 24 ?", true},
        {"PlayerController/APlayerController::BeginPlay", "40 56 48 83 EC 40 48 89 7C 24 ?", true},
        {"PlayerController/APlayerController::EndPlay", "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 30 48 8B B9 ? ? ? ? 8B F2", true},
        {"LocalPlayer/ULocalPlayer::ULocalPlayer", "E9 ? ? ? ? C3 66 66 66 2E 0F 1F 84 00 00 00 00 00 48 8D 64 24 D8 41 54 F7 1C 24", true},
        {"LocalPlayer/APlayerController::APlayerController", "E9 ? ? ? ? C3 85 C0 3C 88", true},

        // Offsets
        {/*PersistentLevel*/ 0x30, /*OwningGameInstance*/ 0x330, /*LocalPlayers*/ 0x38},
    };

    // Original MafiaHub target — older build (~2024, exact buildid unknown). The
    // patterns/offsets the mod shipped with. Kept for posterity / diffing; NOT
    // valid on the current build. Differs from kHL_2025_12 in: gObjectArray
    // (rbp+0x2A0 tail), fnameToString (prologue), fwindowsWindowInitialize,
    // fd3d12CreateRootDevice, and UWorld::OwningGameInstance (0x320).
    inline constexpr GameLayout kHL_2024 = {
        "Steam ~2024 (original MafiaHub target, exact build unknown)",

        {"World/GObjectArray", "48 8D 0D ? ? ? ? E8 ? ? ? ? 48 8D 8D A0 02 00 00", false},
        {"World/GWorld", "48 8B 1D ? ? ? ? 48 85 DB 74 3B 41 B0 01", false},
        {"UE4/GMalloc", "48 8B 0D ? ? ? ? 48 85 C9 75 0C E8 ? ? ? ? 48 8B 0D ? ? ? ? 48 8B 01 48 8B D3 FF 50 ? 48 83 C4 20", false},
        {"UE4/FName::ToString", "48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 48 83 EC 20 8B 01 48 8B DA 8B F8 44 0F B7 C0 C1", false},
        {"Playground/UWorld::SpawnActor", "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 08 FF FF FF 48 81 EC F8 01 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45", false},
        {"Playground/UWorld::DestroyActor", "40 53 56 57 41 54 41 55 41 57 48 81 EC 18", false},
        {"Playground/StaticConstructObject_Internal", "48 89 5C 24 10 48 89 74 24 18 55 57 41 54 41 56 41 57 48 8D AC 24 50 FF FF FF", false},
        // StaticLoadObject wasn't used by the original mod; pattern below is the
        // current-build one (not independently verified for this older build).
        {"Core/StaticLoadObject", "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 88 FC FF FF 48 81 EC 78 04 00 00 48 8B 05", false},
        {"Engine/EngineTick", "E8 ? ? ? ? 80 3D ? ? ? ? ? 74 EB", false},

        // Pak mounting (stock engine fns — same patterns as the current build)
        {"Pak/FPakPlatformFile::GetPakFolders", "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 4C 89 74 24 20 55 48 8B EC 48 83 EC 40 48 8D 4D F0", false},
        {"Pak/FMemory::Realloc", "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B F1 41 8B D8 48 8B 0D ? ? ? ? 48 8B FA", false},

        {"Render/FWindowsWindow::Initialize", "4C 8B DC 53 55 56 41 54 41 55 41 56", false},
        {"Render/FWindowsApplication::ProcessMessage", "E8 ? ? ? ? 48 8B 5C 24 ? 48 8B 6C 24 ? 48 8B 74 24 ? 48 98", false},
        {"Render/FD3D12Adapter::CreateRootDevice", "48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 44 0F B6 FA", false},

        {"Playground/UEngine::LoadMap", "48 89 5C 24 20 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 A0 FE FF FF 48 81 EC 60 02 00 00 0F", true},
        {"World/UWorld::UWorld", "40 53 56 57 48 83 EC 20 4C 89 74 24 ?", true},
        {"PlayerController/APlayerController::BeginPlay", "40 56 48 83 EC 40 48 89 7C 24 ?", true},
        {"PlayerController/APlayerController::EndPlay", "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 30 48 8B B9 ? ? ? ? 8B F2", true},
        {"LocalPlayer/ULocalPlayer::ULocalPlayer", "E9 ? ? ? ? C3 66 66 66 2E 0F 1F 84 00 00 00 00 00 48 8D 64 24 D8 41 54 F7 1C 24", true},
        {"LocalPlayer/APlayerController::APlayerController", "E9 ? ? ? ? C3 85 C0 3C 88", true},

        {/*PersistentLevel*/ 0x30, /*OwningGameInstance*/ 0x320, /*LocalPlayers*/ 0x38},
    };

    // ════════════════════════════════════════════════════════════════════════
    //  >>> Active build for this binary. Change this one line to retarget. <<<
    // ════════════════════════════════════════════════════════════════════════
    // A value (not a reference) so `gLayout.offsets.X` is unambiguously a
    // constant expression for the SDK pad arithmetic below.
    inline constexpr GameLayout gLayout = kHL_2025_12;

    // Compile-time offsets for the active build — these size the SDK class pads
    // (sdk/**). Derived from gLayout so they can never diverge from it.
    namespace Offset {
        inline constexpr uint32_t UWorld_PersistentLevel     = gLayout.offsets.UWorld_PersistentLevel;
        inline constexpr uint32_t UWorld_OwningGameInstance  = gLayout.offsets.UWorld_OwningGameInstance;
        inline constexpr uint32_t UGameInstance_LocalPlayers = gLayout.offsets.UGameInstance_LocalPlayers;
    } // namespace Offset
} // namespace HogwartsMP::Game
