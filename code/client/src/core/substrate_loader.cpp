#include "substrate_loader.h"

// Logger (fmt) MUST precede the SDK/UE headers: those tighten MSVC warnings (4582 -> error),
// which breaks fmt's templates.
#include <logging/logger.h>

#include "sdk/natives/ue4_natives.h"
#include "sdk/reflection/ue4_reflection.h"

#include "UObject/Class.h"

#include <cstdint>
#include <string>

namespace HogwartsMP::Core::SubstrateLoader {
    using namespace HogwartsMP::Core::UE4;

    namespace {
        // The 2nd number is the CharacterID (files are HL-<id>-<idx>), not a save index — and
        // LoadGame ignores its args and loads the continue save, so we select the char explicitly.
        const wchar_t *const kSlot   = L"HL-01-00";
        const int32_t        kCharId = 1;

        auto Log() {
            return Framework::Logging::GetLogger("Substrate");
        }

        // string_view overload, not a "{}" arg: a runtime string as a format arg trips fmt's
        // consteval check, which the bundled MSVC can't compile.
        void Emit(const std::string &line) {
            Log()->log(spdlog::level::info, spdlog::string_view_t(line.data(), line.size()));
        }

        std::string GetSaveGameWorld(UObjectBase *gi) {
            struct {
                FString ReturnValue;
            } p{FString()};
            if (!CallUFunction(gi, "GetSaveGameWorld", &p)) {
                return {};
            }
            return narrow(p.ReturnValue);
        }

        // LoadGame(slot,idx) is a trap: it ignores its args and loads the continue save. Select the
        // character explicitly (GameModManager requested slot+id, then LoadGameDataInSlot), then
        // travel with PhoenixOpenLevel. Game-thread only.
        bool LoadAndTravel(UObjectBase *gi) {
            // Select THIS character (not the continue save).
            if (auto *gmm = FindFirstInstanceOfSubclass("GameModManagerSubsystem")) {
                {
                    struct {
                        FString SlotToLoad;
                    } p{FString(kSlot)};
                    CallUFunction(gmm, "SetRequestedLoadSaveSlot", &p);
                }
                {
                    struct {
                        int32_t CharacterID;
                    } p{kCharId};
                    CallUFunction(gmm, "SetRequestedLoadCharacterId", &p);
                }
            }

            // Stage the exact slot/char. False until PersistentData is ready, which times the retry.
            auto *pd = FindFirstInstanceOfSubclass("PersistentData");
            if (!pd) {
                return false;
            }
            {
                struct {
                    FString SlotName;
                    int32_t CharacterID;
                    uint32_t ReturnValue;
                } p{FString(kSlot), kCharId, 0};
                if (!CallUFunction(pd, "LoadGameDataInSlot", &p) || !(p.ReturnValue & 0xff)) {
                    return false;
                }
            }

            // Travel to the save's world; GetSaveGameWorld can be empty off this path, so fall back.
            std::string world = GetSaveGameWorld(gi);
            if (world.empty()) {
                world = "Overland";
            }
            {
                uint8_t empty[8] = {0};
                CallUFunction(gi, "SetPlayingFromFrontend", &empty); // no params
            }
            {
                const std::wstring ww(world.begin(), world.end());
                struct {
                    UObjectBase *WorldContextObject;
                    FName        LevelName;
                    bool         bAbsolute;
                    FString      OPTIONS;
                } p{gi, MakeFName(ww.c_str()), true, FString()};
                CallUFunction(gi, "PhoenixOpenLevel", &p);
            }
            Emit("substrate auto-load: travel issued -> \"" + world + "\"");
            return true;
        }
    } // namespace

    void TryAutoLoad() {
        static bool fired = false;
        static int  tick  = 0;
        if (fired) {
            return;
        }
        // Settle ~2s after boot, then retry ~2x/s; LoadAndTravel returns false until the front-end
        // is ready, so the retry self-times. Latch on first success.
        if ((++tick % 30) != 0 || tick < 120) {
            return;
        }
        auto *gi = FindFirstInstanceOfSubclass("PhoenixGameInstance");
        if (!gi) {
            return;
        }
        if (LoadAndTravel(gi)) {
            fired = true;
        }
    }
} // namespace HogwartsMP::Core::SubstrateLoader
