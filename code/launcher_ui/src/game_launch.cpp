#include "game_launch.h"

#include <windows.h>
#include <winhttp.h>

#include <launcher/project.h>
#include <external/steam/wrapper.h>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace HogwartsMP::LauncherUI::GameLaunch {
    namespace {
        bool g_pending = false; // armed by PrepareConnect, consumed by RunPendingLaunch

        constexpr uint32_t kSteamAppId = 990080;

        // Tag-pinned Release hosting the pak triplet (swap base to the MafiaHub CDN later).
        // Not in the repo, so the launcher fetches it into the game's Paks for pak_mount.
        constexpr const wchar_t *kPakBaseUrl       = L"https://github.com/hogwarts-mp/assets/releases/download/prerelease-v1.0.0/";
        constexpr const wchar_t *const kPakFiles[] = {L"RemoteAvatar_P.pak", L"RemoteAvatar_P.ucas", L"RemoteAvatar_P.utoc"};

        // Substrate save the client auto-loads on boot; hosted alongside the paks.
        constexpr const wchar_t *kSubstrateFile = L"HL-01-00.sav";

        std::filesystem::path ExeDir() {
            wchar_t buf[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, buf, MAX_PATH);
            return std::filesystem::path(buf).parent_path();
        }

        void Trim(std::string &s) {
            const char *ws = " \t\r\n";
            size_t a       = s.find_first_not_of(ws);
            size_t b       = s.find_last_not_of(ws);
            s              = (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
        }

        // Parse "host", "host:port", or "hogwartsmp://join/host:port" into host + port.
        // Port defaults to 27015 (server bindPort) when absent/invalid.
        void ParseAddress(std::string address, std::string &host, int &port) {
            Trim(address);

            const std::string scheme = "hogwartsmp://";
            if (address.rfind(scheme, 0) == 0) {
                address = address.substr(scheme.size());
                const std::string join = "join/";
                if (address.rfind(join, 0) == 0) {
                    address = address.substr(join.size());
                }
                while (!address.empty() && address.back() == '/') {
                    address.pop_back();
                }
            }

            port             = 27015;
            const size_t col = address.rfind(':');
            if (col != std::string::npos) {
                host = address.substr(0, col);
                try {
                    const int v = std::stoi(address.substr(col + 1));
                    if (v > 0 && v <= 65535) {
                        port = v;
                    }
                } catch (...) {
                    // keep default
                }
            } else {
                host = address;
            }
            Trim(host);
        }

        // GET <url> into <outPath> (HTTPS, follows GitHub's 302). Returns true only on a
        // fully-written 200, removing the partial file otherwise. Tight timeouts so a flaky
        // network adds seconds, not 30s.
        bool DownloadToFile(const std::wstring &url, const std::filesystem::path &outPath) {
            URL_COMPONENTS uc{};
            wchar_t host[256]   = {};
            wchar_t path[2048]  = {};
            uc.dwStructSize     = sizeof(uc);
            uc.lpszHostName     = host;
            uc.dwHostNameLength = ARRAYSIZE(host);
            uc.lpszUrlPath      = path;
            uc.dwUrlPathLength  = ARRAYSIZE(path);
            if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) {
                return false;
            }

            HINTERNET hSession = WinHttpOpen(L"HogwartsMPLauncher/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                             WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            if (!hSession) {
                return false;
            }
            WinHttpSetTimeouts(hSession, /*resolve*/ 5000, /*connect*/ 5000, /*send*/ 10000, /*receive*/ 15000);

            bool ok            = false;
            HINTERNET hConnect = WinHttpConnect(hSession, host, uc.nPort, 0);
            if (hConnect) {
                const DWORD secure = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
                HINTERNET hReq     = WinHttpOpenRequest(hConnect, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                                        WINHTTP_DEFAULT_ACCEPT_TYPES, secure);
                if (hReq) {
                    if (WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                        WinHttpReceiveResponse(hReq, nullptr)) {
                        DWORD status = 0;
                        DWORD len    = sizeof(status);
                        WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
                        if (status == 200) {
                            std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
                            if (out) {
                                ok = true;
                                std::vector<char> buf(64 * 1024);
                                for (;;) {
                                    DWORD read = 0;
                                    if (!WinHttpReadData(hReq, buf.data(), static_cast<DWORD>(buf.size()), &read)) {
                                        ok = false;
                                        break;
                                    }
                                    if (read == 0) {
                                        break;
                                    }
                                    out.write(buf.data(), static_cast<std::streamsize>(read));
                                    if (!out) {
                                        ok = false;
                                        break;
                                    }
                                }
                                out.close();
                                if (!ok) {
                                    std::error_code ec;
                                    std::filesystem::remove(outPath, ec);
                                }
                            }
                        }
                    }
                    WinHttpCloseHandle(hReq);
                }
                WinHttpCloseHandle(hConnect);
            }
            WinHttpCloseHandle(hSession);
            return ok;
        }

        // Fetch the pak into the game's Paks before launch (build-from-source has no other
        // way to get it). Non-fatal: any failure keeps whatever's on disk. Atomic across the
        // triplet — download all three to .tmp, rename only if all succeed, so a partial
        // fetch never leaves a mismatched set that won't mount.
        void DownloadAvatarPak() {
            try {
                Framework::External::Steam::Wrapper steam;
                if (!steam.Init()) {
                    return;
                }
                const std::string installDir = steam.IsAppInstalled(kSteamAppId) ? steam.GetAppInstallDir(kSteamAppId) : std::string {};
                steam.Shutdown();
                if (installDir.empty()) {
                    return;
                }

                // GetAppInstallDir is UTF-8; u8path avoids path(std::string)'s ANSI mis-decode.
                const std::filesystem::path paksDir = std::filesystem::u8path(installDir) / "Phoenix" / "Binaries" / "Win64" / "Paks";
                std::error_code ec;
                std::filesystem::create_directories(paksDir, ec);

                std::vector<std::pair<std::filesystem::path, std::filesystem::path>> staged; // (tmp, dest)
                bool all = true;
                for (const wchar_t *const f : kPakFiles) {
                    std::filesystem::path dest = paksDir / std::filesystem::path(f);
                    std::filesystem::path tmp  = dest;
                    tmp += L".tmp";
                    if (DownloadToFile(std::wstring(kPakBaseUrl) + f, tmp)) {
                        staged.emplace_back(std::move(tmp), std::move(dest));
                    }
                    else {
                        all = false;
                        break;
                    }
                }

                if (all && staged.size() == ARRAYSIZE(kPakFiles)) {
                    for (auto &[tmp, dest] : staged) {
                        std::filesystem::rename(tmp, dest, ec); // same-dir rename: atomic on the volume
                    }
                }
                else {
                    for (auto &[tmp, dest] : staged) {
                        (void)dest;
                        std::filesystem::remove(tmp, ec); // discard partial set; keep what's on disk
                    }
                }
            }
            catch (...) {
                // never let pak-fetch break the game launch
            }
        }

        // Fetch the substrate into the modded SaveGames folder for the in-game auto-loader. HL keys
        // SaveGames by the Steam account id, so resolve it via the wrapper. Non-fatal throughout.
        void DownloadSubstrateSave() {
            try {
                uint32_t accountId = 0;
                {
                    Framework::External::Steam::Wrapper steam;
                    if (!steam.Init()) {
                        return;
                    }
                    accountId = steam.GetSteamID().GetAccountID();
                    steam.Shutdown();
                }
                if (accountId == 0) {
                    return;
                }

                wchar_t localAppData[MAX_PATH] = {};
                if (!GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH)) {
                    return;
                }
                const std::filesystem::path saveDir = std::filesystem::path(localAppData) / L"Hogwarts Legacy" /
                                                      L"HogwartsMP" / L"Saved" / L"SaveGames" / std::to_wstring(accountId);
                const std::filesystem::path dest = saveDir / kSubstrateFile;

                std::error_code ec;
                std::filesystem::create_directories(saveDir, ec);

                // Always overwrite: this folder is mod-managed (not the player's real \Saved\
                // progress), so the substrate stays a pristine baseline and a bumped release
                // propagates. Atomic via .tmp; keep on failure so offline still launches.
                std::filesystem::path tmp = dest;
                tmp += L".tmp";
                if (DownloadToFile(std::wstring(kPakBaseUrl) + kSubstrateFile, tmp)) {
                    std::filesystem::rename(tmp, dest, ec); // same-dir rename: atomic on the volume
                    if (ec) {
                        std::filesystem::remove(tmp, ec); // rename failed — don't orphan the .tmp
                    }
                }
                else {
                    std::filesystem::remove(tmp, ec);
                }
            }
            catch (...) {
                // never let the save fetch break the game launch
            }
        }
    } // namespace

    bool PrepareConnect(const std::string &address) {
        std::string host;
        int port = 0;
        ParseAddress(address, host, port);
        if (host.empty()) {
            return false;
        }

        // Write the connect-config the injected client reads on init.
        std::ofstream f(ExeDir() / "connect.json", std::ios::binary | std::ios::trunc);
        if (!f) {
            return false;
        }
        const nlohmann::json cfg = {
            {"host", host},
            {"port", port},
            {"nickname", ""},
        };
        f << cfg.dump(2) << "\n";
        if (!f) {
            return false;
        }

        g_pending = true;
        return true;
    }

    bool LaunchPending() {
        return g_pending;
    }

    int RunPendingLaunch() {
        // Fetch the remote-avatar pak (RemoteAvatar_P.*) into the game's Paks; the in-game
        // pak_mount hook co-mounts it at startup. Non-fatal — see DownloadAvatarPak.
        DownloadAvatarPak();

        // Fetch the substrate save so the client boots into the baseline. Non-fatal.
        DownloadSubstrateSave();

        Framework::Launcher::ProjectConfiguration config;
        config.destinationDllName        = L"HogwartsMPClient.dll";
        config.executableName            = L"HogwartsLegacy.exe";
        config.name                      = "HogwartsMP";
        config.launchType                = Framework::Launcher::ProjectLaunchType::DLL_INJECTION;
        config.platform                  = Framework::Launcher::ProjectPlatform::STEAM;
        config.preferSteam               = true;
        config.steamAppId                = kSteamAppId;
        config.alternativeWorkDir        = L"Phoenix/Binaries/Win64";
        config.additionalLaunchArguments = L" dx12 d3d12 -SaveToUserDir -UserDir=\"Hogwarts Legacy\\HogwartsMP\"";
        config.useAlternativeWorkDir     = true;
        config.additionalSearchPaths     = {
            L"Engine\\Binaries\\ThirdParty\\DbgHelp",
            L"Engine\\Binaries\\ThirdParty\\NVIDIA\\GeForceNOW\\Win64",
            L"Engine\\Binaries\\ThirdParty\\NVIDIA\\NVaftermath\\Win64\\GFSDK_Aftermath_Lib",
            L"Engine\\Binaries\\ThirdParty\\Ogg\\Win64\\VS2015",
            L"Engine\\Binaries\\ThirdParty\\Steamworks\\Steamv154\\Win64",
            L"Engine\\Binaries\\ThirdParty\\Windows\\XAudio2_9\\x64",
            L"Engine\\Plugins\\Runtime\\Intel\\XeSS\\Binaries\\ThirdParty\\Win64",
            L"Engine\\Plugins\\Runtime\\Nvidia\\Ansel\\Binaries\\ThirdParty",
            L"Engine\\Plugins\\Runtime\\Nvidia\\DLSS\\Binaries\\ThirdParty\\Win64",
            L"Engine\\Plugins\\Runtime\\Nvidia\\NVIDIAGfeSDK\\ThirdParty\\NVIDIAGfeSDK\\redist\\Win64",
            L"Engine\\Plugins\\Runtime\\Nvidia\\Streamline\\Binaries\\ThirdParty\\Win64",
            L"Engine\\Plugins\\Runtime\\Nvidia\\Streamline\\Binaries\\ThirdParty\\Win64\\sl",
            L"Phoenix\\Binaries\\Win64\\EOSSDK-Win64",
            L"Phoenix\\Binaries\\Win64",
            L"Phoenix\\Plugins\\ChromaSDKPlugin\\Binaries\\Win64",
            L"Phoenix\\Plugins\\Wwise\\ThirdParty\\x64_vc160\\Release\\bin",
        };

#ifdef FW_DEBUG
        config.allocateDeveloperConsole = true;
        config.developerConsoleTitle    = L"hogwartsmp: dev-console";
#endif

        Framework::Launcher::Project project(config);
        return project.Launch() ? 0 : 1;
    }
} // namespace HogwartsMP::LauncherUI::GameLaunch
