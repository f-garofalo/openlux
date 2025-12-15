/**
 * @file command_manager.cpp
 * @brief Command dispatcher for maintenance commands
 *
 * @license GPL-3.0
 * @author OpenLux Contributors
 */

#include "command_manager.h"

#include "../config.h"
#include "logger.h"
#include "network_manager.h"
#include "ntp_manager.h"
#include "rs485_manager.h"
#include "tcp_server.h"

#include <Esp.h>

#include <numeric>

#include <WiFi.h>

static const char* CMD_TAG = "cmd";

CommandManager& CommandManager::getInstance() {
    static CommandManager instance;
    return instance;
}

void CommandManager::registerCommand(const String& name, const String& help,
                                     CommandHandler handler) {
    commands_[name] = Entry{help, handler};
}

CommandResult CommandManager::execute(const String& line) {
    String trimmed = line;
    trimmed.trim();
    if (trimmed.startsWith("!")) {
        trimmed.remove(0, 1); // strip '!'
    }
    if (trimmed.length() == 0) {
        return CommandResult{false, "Empty command"};
    }

    // Split into tokens
    std::vector<String> tokens;
    int start = 0;
    while (start >= 0) {
        int space = trimmed.indexOf(' ', start);
        if (space == -1) {
            tokens.push_back(trimmed.substring(start));
            break;
        } else {
            tokens.push_back(trimmed.substring(start, space));
            start = space + 1;
            while (start < (int) trimmed.length() && trimmed[start] == ' ')
                start++; // skip spaces
            if (start >= (int) trimmed.length())
                break;
        }
    }

    if (tokens.empty()) {
        return CommandResult{false, "Empty command"};
    }

    String cmd = tokens[0];
    tokens.erase(tokens.begin()); // args

    auto it = commands_.find(cmd);
    if (it == commands_.end()) {
        return CommandResult{false, "Unknown command: " + cmd};
    }

    return it->second.handler(tokens);
}

String CommandManager::help() const {
    String out;
    for (const auto& kv : commands_) {
        out += " - " + kv.first + ": " + kv.second.help + "\n";
    }
    return out;
}

void CommandManager::registerCoreCommands() {
    // status
    registerCommand("status", "Show system status (link, network, version, heap)",
                    [](const std::vector<String>&) -> CommandResult {
                        const auto& net = NetworkManager::getInstance();
                        const auto& rs = RS485Manager::getInstance();

                        String msg;
                        msg.reserve(300);
                        msg += "Link: ";
                        msg += rs.is_inverter_link_up() ? "UP" : "DOWN";
                        msg += "\nRS485 SN: ";
                        msg += rs.get_detected_inverter_serial();

                        msg += "\nNET: ";
                        msg += (OPENLUX_USE_ETHERNET ? "ETH" : "WIFI");
                        msg += " ";
                        msg += net.getIP().toString();
                        if (!OPENLUX_USE_ETHERNET) {
                            msg += " (";
                            msg += net.getSSID();
                            msg += ", RSSI ";
                            msg += net.getRSSI();
                            msg += " dBm)";
                        }

                        msg += "\nHeap: ";
                        msg += String(ESP.getFreeHeap());
                        msg += " bytes";

                        uint32_t up_ms = millis();
                        uint32_t up_sec = up_ms / 1000;
                        uint32_t up_min = up_sec / 60;
                        uint32_t up_hr = up_min / 60;
                        msg += "\nUptime: ";
                        msg += String(up_hr);
                        msg += "h ";
                        msg += String(up_min % 60);
                        msg += "m ";
                        msg += String(up_sec % 60);
                        msg += "s";

                        msg += "\nFW: ";
                        msg += FIRMWARE_NAME;
                        msg += " v";
                        msg += FIRMWARE_VERSION;
                        msg += " (built ";
                        msg += BUILD_TIMESTAMP;
                        msg += ")";

                        return CommandResult{true, msg};
                    });

    // reboot
    registerCommand("reboot", "Reboot the device", [](const std::vector<String>&) -> CommandResult {
        static uint32_t last_reboot_ms = 0;
        uint32_t now = millis();
        if (now - last_reboot_ms < COMMAND_DEBOUNCE_MS) {
            uint32_t wait_ms = COMMAND_DEBOUNCE_MS - (now - last_reboot_ms);
            return CommandResult{false,
                                 "Reboot debounced, try again in " + String(wait_ms / 1000) + "s"};
        }
        last_reboot_ms = now;
        LOGI(CMD_TAG, "Rebooting on user command");
        NetworkManager::getInstance().rebootDevice("User command");
        return CommandResult{true, "Rebooting..."};
    });

    // probe_rs485
    registerCommand("probe_rs485", "Probe inverter serial (regs 115-119)",
                    [](const std::vector<String>&) -> CommandResult {
                        auto& rs = RS485Manager::getInstance();
                        rs.probe_inverter_serial();
                        return CommandResult{true, "RS485 serial probe triggered"};
                    });

    // help
    registerCommand("help", "Show available commands",
                    [this](const std::vector<String>&) -> CommandResult {
                        return CommandResult{true, this->help()};
                    });

    // wifi_restart: full off/on cycle
    registerCommand("wifi_restart", "Restart WiFi interface (off/on + reconnect)",
                    [](const std::vector<String>&) -> CommandResult {
                        static uint32_t last_restart_ms = 0;
                        uint32_t now = millis();
                        if (now - last_restart_ms < COMMAND_DEBOUNCE_MS) {
                            uint32_t wait_ms = COMMAND_DEBOUNCE_MS - (now - last_restart_ms);
                            return CommandResult{false, "WiFi restart debounced, try again in " +
                                                            String(wait_ms / 1000) + "s"};
                        }
                        last_restart_ms = now;
                        LOGI(CMD_TAG, "WiFi restart requested");
                        NetworkManager::getInstance().restartInterface();
                        return CommandResult{true, "WiFi restart triggered"};
                    });

    // wifi_reconnect: soft reconnect without power cycling
    registerCommand("wifi_reconnect", "Disconnect and reconnect WiFi (soft)",
                    [](const std::vector<String>&) -> CommandResult {
                        LOGI(CMD_TAG, "WiFi soft reconnect requested");
                        NetworkManager::getInstance().softReconnect();
                        return CommandResult{true, "WiFi reconnect triggered"};
                    });

    // wifi_reset: clear credentials and open provisioning portal
    registerCommand("wifi_reset", "Clear WiFi creds and open provisioning portal",
                    [](const std::vector<String>&) -> CommandResult {
                        auto& wifi = NetworkManager::getInstance();
                        if (wifi.isOTAInProgress()) {
                            return CommandResult{false, "OTA in progress, aborting wifi_reset"};
                        }
                        wifi.clearCredentials();
                        bool ok = wifi.startProvisioningPortal();
                        return ok ? CommandResult{true, "Portal opened, configure WiFi"}
                                  : CommandResult{false, "Portal failed or timeout"};
                    });

    // wifi_scan: list nearby networks (SSID/RSSI)
    registerCommand("wifi_scan", "Scan WiFi networks (SSID/RSSI)",
                    [](const std::vector<String>&) -> CommandResult {
                        int n = WiFi.scanNetworks();
                        if (n == WIFI_SCAN_FAILED) {
                            return CommandResult{false, "Scan failed"};
                        }
                        if (n == 0) {
                            return CommandResult{true, "No networks found"};
                        }
                        String out;
                        out.reserve(256);
                        auto sig_icon = [](int rssi) -> const char* {
                            if (rssi >= -50)
                                return "[####]";
                            if (rssi >= -60)
                                return "[### ]";
                            if (rssi >= -70)
                                return "[##  ]";
                            if (rssi >= -80)
                                return "[#   ]";
                            return "[.   ]";
                        };
                        for (int i = 0; i < n; i++) {
                            out += String(i) + ") " + WiFi.SSID(i) + " " + sig_icon(WiFi.RSSI(i)) +
                                   " (" + WiFi.RSSI(i) + " dBm)";
                            if (WiFi.encryptionType(i) != WIFI_AUTH_OPEN) {
                                out += " [sec]";
                            }
                            out += "\n";
                            if (i >= 9)
                                break; // limit output
                        }
                        return CommandResult{true, out};
                    });

    // log_level <0-4>
    registerCommand("log_level", "Set log level 0=DEBUG,1=INFO,2=WARN,3=ERROR,4=NONE",
                    [](const std::vector<String>& args) -> CommandResult {
                        if (args.empty()) {
                            int lvl = static_cast<int>(Logger::getInstance().getLogLevel());
                            return CommandResult{true, "Current log level: " + String(lvl)};
                        }
                        int lvl = args[0].toInt();
                        if (lvl < 0 || lvl > 4) {
                            return CommandResult{false, "Level must be 0-4"};
                        }
                        Logger::getInstance().setLogLevel(static_cast<LogLevel>(lvl));
                        return CommandResult{true, "Log level set to " + String(lvl)};
                    });

    // ntp_sync
    registerCommand("ntp_sync", "Force NTP synchronization now",
                    [](const std::vector<String>&) -> CommandResult {
                        auto& ntp = NTPManager::getInstance();
                        ntp.forceSync();
                        return CommandResult{true, "NTP sync triggered"};
                    });

    // heap info
    registerCommand("heap", "Show heap/PSRAM info",
                    [](const std::vector<String>&) -> CommandResult {
                        String out;
                        out.reserve(96);
                        out += "Heap free: ";
                        out += String(ESP.getFreeHeap());
                        out += " bytes\nHeap max alloc: ";
                        out += String(ESP.getMaxAllocHeap());
                        out += " bytes";
#ifdef BOARD_HAS_PSRAM
                        out += "\nPSRAM size: ";
                        out += String(ESP.getPsramSize());
                        out += " bytes\nPSRAM free: ";
                        out += String(ESP.getFreePsram());
                        out += " bytes";
#endif
                        return CommandResult{true, out};
                    });

    // tcp_clients [drop]
    registerCommand("tcp_clients", "List TCP clients (add 'drop' to disconnect all)",
                    [](const std::vector<String>& args) -> CommandResult {
                        auto& tcp = TCPServer::getInstance();
                        if (!args.empty() && args[0].equalsIgnoreCase("drop")) {
                            tcp.disconnect_all_clients();
                            return CommandResult{true, "All TCP clients disconnected"};
                        }
                        return CommandResult{true, tcp.describe_clients()};
                    });
}
