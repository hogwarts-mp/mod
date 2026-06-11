#pragma once

#include "shared/chat_command.h"

MODULE(chat_command, {
    using HogwartsMP::Shared::ParseChatCommand;

    IT("treats plain messages as not a command", {
        const auto parsed = ParseChatCommand("hello there");
        EQUALS(parsed.isCommand, false);
        EQUALS(parsed.command.empty(), true);
        EQUALS(parsed.args.empty(), true);
    });

    IT("treats empty text as not a command", {
        const auto parsed = ParseChatCommand("");
        EQUALS(parsed.isCommand, false);
    });

    IT("parses a command without arguments", {
        const auto parsed = ParseChatCommand("/help");
        EQUALS(parsed.isCommand, true);
        STREQUALS(parsed.command.c_str(), "help");
        EQUALS(parsed.args.empty(), true);
    });

    IT("parses a bare slash as an empty command", {
        const auto parsed = ParseChatCommand("/");
        EQUALS(parsed.isCommand, true);
        EQUALS(parsed.command.empty(), true);
        EQUALS(parsed.args.empty(), true);
    });

    IT("parses a command with arguments", {
        const auto parsed = ParseChatCommand("/time 11 30");
        EQUALS(parsed.isCommand, true);
        STREQUALS(parsed.command.c_str(), "time");
        EQUALS(parsed.args.size(), 2);
        STREQUALS(parsed.args[0].c_str(), "11");
        STREQUALS(parsed.args[1].c_str(), "30");
    });

    IT("collapses repeated whitespace between arguments", {
        const auto parsed = ParseChatCommand("/weather   Clear   ");
        EQUALS(parsed.isCommand, true);
        STREQUALS(parsed.command.c_str(), "weather");
        EQUALS(parsed.args.size(), 1);
        STREQUALS(parsed.args[0].c_str(), "Clear");
    });
});
