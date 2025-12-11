/**
 * @file command_manager.h
 * @brief Command dispatcher for maintenance commands
 *
 * @license GPL-3.0
 * @author OpenLux Contributors
 */

#pragma once

#include <Arduino.h>

#include <functional>
#include <map>
#include <vector>

struct CommandResult {
    bool ok;
    String message;

    CommandResult() : ok(false), message("") {}
    CommandResult(bool ok_value, const String& msg) : ok(ok_value), message(msg) {}
};

using CommandHandler = std::function<CommandResult(const std::vector<String>& args)>;

/**
 * @brief Simple command dispatcher for maintenance commands over Telnet/Serial.
 */
class CommandManager {
  public:
    static CommandManager& getInstance();

    void registerCommand(const String& name, const String& help, CommandHandler handler);
    CommandResult execute(const String& line);

    String help() const;

    // Register built-in commands (status, reboot, probe_rs485)
    void registerCoreCommands();

  private:
    CommandManager() = default;
    ~CommandManager() = default;
    CommandManager(const CommandManager&) = delete;
    CommandManager& operator=(const CommandManager&) = delete;

    struct Entry {
        String help;
        CommandHandler handler;
    };

    std::map<String, Entry> commands_;
};
