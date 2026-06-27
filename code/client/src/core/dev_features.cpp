/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "dev_features.h"
#include "application.h"

#include <logging/logger.h>

#include <cppfs/FileHandle.h>
#include <cppfs/fs.h>

#include "shared/rpc/set_weather.h"

#include "modules/human.h"

namespace HogwartsMP::Core {
    DevFeatures::DevFeatures() {
        _teleportManager = std::make_shared<UI::TeleportManager>();
        _seasonManager = std::make_shared<UI::SeasonManager>();
    }

    void DevFeatures::Init() {
        SetupCommands();
    }

    void DevFeatures::Shutdown() {}

    void DevFeatures::Disconnect() {
        (void)gApplication->GetNetworkingEngine()->GetNetworkClient()->Disconnect();
    }

    void DevFeatures::CrashMe() {
        *(int *)5 = 5;
    }

    void DevFeatures::BreakMe() {
        __debugbreak();
    }

    void DevFeatures::CloseGame() {
        // very lazy game shutdown
        // don't try at home
        ExitProcess(0);
    }

    void DevFeatures::SetupCommands() {
        gApplication->_commandProcessor->RegisterCommand(
            "test", {{"a,aargument", "Test argument 1", cxxopts::value<std::string>()}, {"b,bargument", "Test argument 2", cxxopts::value<int>()}},
            [this](const cxxopts::ParseResult &result) {
                if (result.count("aargument")) {
                    std::string argument1 = result["aargument"].as<std::string>();
                    Framework::Logging::GetLogger("Debug")->info("aargument - {}", argument1);
                }
                if (result.count("bargument")) {
                    int argument2 = result["bargument"].as<int>();
                    Framework::Logging::GetLogger("Debug")->info("bargument - {}", argument2);
                }
            },
            "Testing command");
        gApplication->_commandProcessor->RegisterCommand(
            "crash", {},
            [this](cxxopts::ParseResult &) {
                CrashMe();
            },
            "crashes the game");
        gApplication->_commandProcessor->RegisterCommand(
            "echo", {},
            [this](const cxxopts::ParseResult &result) {
                std::string argsConcat;
                cxxopts::PositionalList args = result.unmatched();
                for (auto &arg : args) {
                    argsConcat += arg + " ";
                }
                Framework::Logging::GetLogger("Debug")->info(argsConcat);
            },
            "[args] - prints the arguments back");
        gApplication->_commandProcessor->RegisterCommand(
            "help", {},
            [this](cxxopts::ParseResult &) {
                std::stringstream ss;
                for (const auto &name : gApplication->_commandProcessor->GetCommandNames()) {
                    ss << fmt::format("{} {:>8}\n", name, gApplication->_commandProcessor->GetCommandInfo(name)->options->help());
                }
                Framework::Logging::GetLogger("Debug")->info("Available commands:\n{}", ss.str());
            },
            "prints all available commands");
        gApplication->_commandProcessor->RegisterCommand(
            "exit", {},
            [this](cxxopts::ParseResult &) {
                CloseGame();
            },
            "quits the game");
        gApplication->_commandProcessor->RegisterCommand(
            "tele", {},
            [this](cxxopts::ParseResult &) {
                gApplication->GetHud()->OpenDevMenu("teleport");
            },
            "open the teleport picker (dev menu)");
        gApplication->_commandProcessor->RegisterCommand(
            "webdebug", {},
            [this](cxxopts::ParseResult &) {
                gApplication->GetHud()->OpenDevMenu("webdebug");
            },
            "open the web-debug panel (dev menu)");
        gApplication->_commandProcessor->RegisterCommand(
            "break", {},
            [this](cxxopts::ParseResult &) {
                BreakMe();
            },
            "triggers a debug break");
        gApplication->_commandProcessor->RegisterCommand(
            "weather", {},
            [this](cxxopts::ParseResult &) {
                GetSeasonManager()->SetRandomSeason();
            },
            "randomize the season/weather");
        gApplication->_commandProcessor->RegisterCommand(
            "time", {{"h,hours", "hours to advance", cxxopts::value<int>()->default_value("6")}},
            [this](const cxxopts::ParseResult &result) {
                GetSeasonManager()->AdvanceHours(result["hours"].as<int>());
            },
            "[--hours N] advance the time of day");
        gApplication->_commandProcessor->RegisterCommand(
            "chat", {{"m,msg", "message to send", cxxopts::value<std::string>()->default_value("")}},
            [this](const cxxopts::ParseResult &result) {
                const auto net = gApplication->GetNetworkingEngine()->GetNetworkClient();
                if (net->GetConnectionState() == Framework::Networking::PeerState::CONNECTED) {
                    gApplication->SendChatMessage(result["msg"].as<std::string>());
                }
            },
            "sends a chat message");

        gApplication->_commandProcessor->RegisterCommand(
            "disconnect", {},
            [this](const cxxopts::ParseResult &) {
                Disconnect();
            },
            "disconnect from server");
    }
} // namespace HogwartsMP::Core
