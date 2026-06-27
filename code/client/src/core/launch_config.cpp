#include "launch_config.h"

#include <utils/safe_win32.h>

#include <logging/logger.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <system_error>

namespace HogwartsMP::Core {
    namespace {
        // Directory of THIS module (the client DLL), not the process exe — the launcher
        // writes connect.json next to the DLL in bin/. Mirrors the CEF subprocess-path
        // resolution in the framework's GUI manager.
        std::filesystem::path ModuleDir() {
            static const int anchor = 0;
            HMODULE module          = nullptr;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&anchor), &module) || !module) {
                return {};
            }
            wchar_t path[MAX_PATH] = {};
            GetModuleFileNameW(module, path, MAX_PATH);
            return std::filesystem::path(path).parent_path();
        }
    } // namespace

    std::optional<LaunchConfig> ReadConnectConfig(bool consume) {
        const auto log = Framework::Logging::GetInstance()->Get("LaunchConfig");

        const std::filesystem::path dir = ModuleDir();
        if (dir.empty()) {
            log->warn("Could not resolve client module directory");
            return std::nullopt;
        }

        const std::filesystem::path file = dir / "connect.json";
        std::error_code ec;
        const bool exists = std::filesystem::exists(file, ec);
        log->info("Looking for connect.json at '{}' (exists={})", file.string(), exists);
        if (!exists) {
            return std::nullopt;
        }

        std::optional<LaunchConfig> result;
        {
            std::ifstream f(file, std::ios::binary);
            if (f) {
                const nlohmann::json j = nlohmann::json::parse(f, nullptr, /*allow_exceptions=*/false);
                if (j.is_object()) {
                    LaunchConfig cfg;
                    cfg.host     = j.value("host", "");
                    cfg.port     = j.value("port", 27015);
                    cfg.nickname = j.value("nickname", "");
                    if (!cfg.host.empty()) {
                        log->info("Parsed connect.json -> {}:{}", cfg.host, cfg.port);
                        result = std::move(cfg);
                    } else {
                        log->warn("connect.json has no host");
                    }
                } else {
                    log->warn("connect.json is not valid JSON");
                }
            } else {
                log->warn("Failed to open connect.json for reading");
            }
        }

        if (consume) {
            std::filesystem::remove(file, ec);
        }
        return result;
    }
} // namespace HogwartsMP::Core
