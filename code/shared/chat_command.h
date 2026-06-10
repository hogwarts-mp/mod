#pragma once

#include <sstream>
#include <string>
#include <vector>

namespace HogwartsMP::Shared {
    struct ChatCommand {
        bool isCommand = false;
        std::string command;
        std::vector<std::string> args;
    };

    /**
     * Parse raw chat text into a command and whitespace-separated arguments.
     * Text starting with '/' is a command; anything else is a plain message.
     */
    inline ChatCommand ParseChatCommand(const std::string &text) {
        ChatCommand result;
        if (text.empty() || text[0] != '/') {
            return result;
        }

        result.isCommand = true;

        std::string argsPart;
        const auto spacePos = text.find(' ');
        if (spacePos != std::string::npos) {
            result.command = text.substr(1, spacePos - 1);
            argsPart       = text.substr(spacePos + 1);
        }
        else {
            result.command = text.substr(1);
        }

        std::istringstream iss(argsPart);
        std::string arg;
        while (iss >> arg) {
            result.args.push_back(arg);
        }

        return result;
    }
} // namespace HogwartsMP::Shared
