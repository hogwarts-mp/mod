// Inject our Paks folder into UE's startup scan so an IoStore container
// placed there co-mounts during engine init.
//
// Mechanism: hook FPakPlatformFile::GetPakFolders(self, TArray<FString>* out)
// and append "<exeDir>\Paks" to the returned folder list. UE's own startup scan
// then enumerates that folder and mounts any .pak/.ucas/.utoc it finds.
//
// We grow the engine's TArray<FString> ourselves via one FMemory::Realloc call
// (the same heap UE used) instead of needing the templated TArray::Grow symbol.

#include <utils/safe_win32.h>

#include <MinHook.h>
#include <utils/hooking/hook_function.h>
#include <utils/string_utils.h>

#include <logging/logger.h>

#include "../aob_scan.h"

#include <cstdint>
#include <cstring>
#include <cwchar>

namespace {
    auto Log() {
        return Framework::Logging::GetLogger("PakMount");
    }

    // UE4.27 layouts. FString = {Data, Num, Max}; Num/Max include the null terminator.
    struct UEFString {
        wchar_t *Data;
        int32_t Num;
        int32_t Max;
    };
    struct UETArrayFString {
        UEFString *Data;
        int32_t Num;
        int32_t Max;
    };

    using GetPakFolders_t = void(__fastcall *)(void *self, UETArrayFString *out);
    using Realloc_t       = void *(__fastcall *)(void *ptr, size_t size, uint32_t alignment);

    GetPakFolders_t g_orig         = nullptr;
    Realloc_t g_realloc            = nullptr;
    wchar_t g_modsPath[MAX_PATH]   = {};

    // resolves to <...>\Phoenix\Binaries\Win64\Paks.
    bool ResolveModsPath() {
        wchar_t exe[MAX_PATH];
        const DWORD len = GetModuleFileNameW(nullptr, exe, MAX_PATH);
        if (!len || len >= MAX_PATH) {
            return false;
        }
        wchar_t *slash = wcsrchr(exe, L'\\');
        if (!slash) {
            return false;
        }
        *slash = L'\0';
        return _snwprintf_s(g_modsPath, MAX_PATH, _TRUNCATE, L"%s\\Paks", exe) >= 0;
    }

    void __fastcall GetPakFolders__Hook(void *self, UETArrayFString *out) {
        g_orig(self, out);
        if (!out || !g_realloc) {
            return;
        }

        const int n = out->Num;
        Log()->info("GetPakFolders: engine returned {} folder(s)", n);
        for (int i = 0; i < n; ++i) {
            if (out->Data && out->Data[i].Data) {
                Log()->info("  [{}] {}", i, Framework::Utils::StringUtils::WideToNormal(out->Data[i].Data));
                // Idempotency: if our folder is already present (engine reused/pre-populated the
                // array, or called us twice on the same buffer), don't append a duplicate.
                if (wcscmp(out->Data[i].Data, g_modsPath) == 0) {
                    Log()->info("  '{}' already present — skipping append",
                                Framework::Utils::StringUtils::WideToNormal(g_modsPath));
                    return;
                }
            }
        }

        // Grow the backing buffer by one FString via the engine's own allocator
        // (== TArray::Grow internally, no Grow symbol needed).
        auto *grown = static_cast<UEFString *>(
            g_realloc(out->Data, static_cast<size_t>(n + 1) * sizeof(UEFString), 0));
        if (!grown) {
            Log()->error("array realloc failed");
            return;
        }
        out->Data = grown;
        out->Max  = n + 1;

        const int chars = static_cast<int>(wcslen(g_modsPath)) + 1; // incl null
        auto *sbuf      = static_cast<wchar_t *>(
            g_realloc(nullptr, static_cast<size_t>(chars) * sizeof(wchar_t), 0));
        if (!sbuf) {
            Log()->error("string alloc failed");
            return;
        }
        memcpy(sbuf, g_modsPath, static_cast<size_t>(chars) * sizeof(wchar_t));

        grown[n].Data = sbuf;
        grown[n].Num  = chars;
        grown[n].Max  = chars;
        out->Num      = n + 1;
        Log()->info("appended '{}' -> now {} folders (drop a .pak/.ucas/.utoc triplet there)",
                    Framework::Utils::StringUtils::WideToNormal(g_modsPath), out->Num);
    }
} // namespace

// Registered here, created in InitFunction::RunAll() and enabled by
// MH_EnableHook(MH_ALL_HOOKS) — both from main.cpp's pre-WinMain CRT hook, so the
// detour is live before UE's startup pak scan.
static InitFunction init(
    []() {
        auto *gpf       = HogwartsMP::Core::AobFirst(HogwartsMP::Game::gLayout.getPakFolders);
        auto *reallocFn = HogwartsMP::Core::AobFirst(HogwartsMP::Game::gLayout.fmemoryRealloc);
        if (!gpf || !reallocFn) {
            Log()->warn("pak mount disabled — GetPakFolders/Realloc pattern not resolved (see [AOB])");
            return;
        }
        if (!ResolveModsPath()) {
            Log()->warn("pak mount disabled — could not resolve exe Paks folder");
            return;
        }
        g_realloc = reinterpret_cast<Realloc_t>(reallocFn);
        if (MH_CreateHook(gpf, reinterpret_cast<PBYTE>(GetPakFolders__Hook),
                          reinterpret_cast<void **>(&g_orig)) != MH_OK) {
            Log()->error("MH_CreateHook(GetPakFolders) failed");
            return;
        }
        Log()->info("GetPakFolders hooked @ {} (realloc @ {}); inject folder = {}",
                    gpf, reallocFn, Framework::Utils::StringUtils::WideToNormal(g_modsPath));
    },
    "PakMount");
