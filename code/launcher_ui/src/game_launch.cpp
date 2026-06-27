#include "game_launch.h"

#include <windows.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <vector>

namespace HogwartsMP::LauncherUI::GameLaunch {
    namespace {
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
    } // namespace

    bool ConnectAndLaunch(const std::string &address) {
        std::string host;
        int port = 0;
        ParseAddress(address, host, port);
        if (host.empty()) {
            return false;
        }

        const std::filesystem::path dir = ExeDir();

        // Write the connect-config the injected client reads on init.
        {
            std::ofstream f(dir / "connect.json", std::ios::binary | std::ios::trunc);
            if (!f) {
                return false;
            }
            const nlohmann::json cfg = {
                {"host", host},
                {"port", port},
                {"nickname", ""},
            };
            f << cfg.dump(2) << "\n";
        }

        // Spawn the existing injector (same directory) which launches + injects the game.
        const std::filesystem::path injector = dir / "HogwartsMPLauncher.exe";
        const std::wstring appPath           = injector.wstring();
        std::wstring cmdline                 = L"\"" + appPath + L"\"";
        std::vector<wchar_t> cmd(cmdline.begin(), cmdline.end());
        cmd.push_back(L'\0');

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        const BOOL ok = CreateProcessW(appPath.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.wstring().c_str(), &si, &pi);
        if (!ok) {
            return false;
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }
} // namespace HogwartsMP::LauncherUI::GameLaunch
