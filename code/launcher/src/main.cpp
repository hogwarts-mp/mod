#include <Windows.h>
#include <winhttp.h>

#include <launcher/project.h>
#include <external/steam/wrapper.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace {
    // Tag-pinned Release hosting the pak triplet (swap base to the MafiaHub CDN later). Not in the repo,
    // so the launcher fetches it into the game's Paks for pak_mount to co-mount.
    constexpr const wchar_t *kPakBaseUrl       = L"https://github.com/hogwarts-mp/assets/releases/download/prerelease-v1.0.0/";
    constexpr const wchar_t *const kPakFiles[] = {L"RemoteAvatar_P.pak", L"RemoteAvatar_P.ucas", L"RemoteAvatar_P.utoc"};
    constexpr uint32_t kSteamAppId             = 990080;

    // GET <url> into <outPath> (HTTPS, follows GitHub's 302). Returns true only on a fully-written 200,
    // removing the partial file otherwise. Tight timeouts so a flaky network adds seconds, not 30s.
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

    // Fetch the pak into the game's Paks before launch (build-from-source has no other way to get it).
    // Non-fatal: any failure keeps whatever's on disk. Atomic across the triplet — download all three to
    // .tmp, rename only if all succeed, so a partial fetch never leaves a mismatched set that won't mount.
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

            // GetAppInstallDir is UTF-8; u8path avoids path(std::string)'s ANSI mis-decode of non-ASCII paths.
            const std::filesystem::path paksDir = std::filesystem::u8path(installDir) / "Phoenix" / "Binaries" / "Win64" / "Paks";
            std::error_code ec;
            std::filesystem::create_directories(paksDir, ec);

            std::printf("[launcher] fetching remote-avatar pak from the Release...\n");
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
                std::printf("  pak ready (%zu files)\n", staged.size());
            }
            else {
                for (auto &[tmp, dest] : staged) {
                    (void)dest;
                    std::filesystem::remove(tmp, ec); // discard partial set; keep whatever's already on disk
                }
                std::printf("  fetch incomplete — kept existing pak\n");
            }
        }
        catch (...) {
            // never let pak-fetch break the game launch
        }
    }
} // namespace

int main (void) {
    // Fetch the remote-avatar pak (RemoteAvatar_P.*) from the Release into the game's Paks folder before
    // launching; the in-game pak_mount hook co-mounts it at startup. Non-fatal — see DownloadAvatarPak.
    DownloadAvatarPak();

    Framework::Launcher::ProjectConfiguration config;
    config.destinationDllName = L"HogwartsMPClient.dll";
    config.executableName     = L"HogwartsLegacy.exe";
    config.name               = "HogwartsMP";
    config.launchType         = Framework::Launcher::ProjectLaunchType::DLL_INJECTION;
    config.platform           = Framework::Launcher::ProjectPlatform::STEAM;
    config.preferSteam        = true;
    config.steamAppId         = 990080;
    config.alternativeWorkDir    = L"Phoenix/Binaries/Win64";
    config.additionalLaunchArguments = L" dx12 d3d12 -SaveToUserDir -UserDir=\"Hogwarts Legacy\\HogwartsMP\"";
    config.useAlternativeWorkDir = true;
    config.additionalSearchPaths      = {
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
    if (!project.Launch()) {
        return 1;
    }
    return 0;
}
