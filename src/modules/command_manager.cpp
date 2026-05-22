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
#include "protocol_bridge.h"
#include "rs485_manager.h"
#include "system_manager.h"
#include "tcp_server.h"
#ifdef ENABLE_WEB_DASH
#include "web_server.h"
#endif

#include <Esp.h>

#include <cstdlib>
#include <numeric>

#include <WiFi.h>

static const char* CMD_TAG = "cmd";

static bool parseLogLevel(const String& value, int& out) {
    char* end = nullptr;
    const long parsed = strtol(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0' || parsed < 0 || parsed > 4) {
        return false;
    }
    out = static_cast<int>(parsed);
    return true;
}

static String formatLogLevel(LogLevel level) {
    const int raw = static_cast<int>(level);
    return String(raw) + " (" + Logger::logLevelName(level) + ")";
}

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
                        const auto& sys = SystemManager::getInstance();
                        const auto& tcp = TCPServer::getInstance();

                        String msg;
                        msg.reserve(900);
                        msg += "Link: ";
                        msg += rs.is_inverter_link_up() ? "UP" : "DOWN";
                        msg += " [WEB-REQ#";
                        msg += String(ProtocolBridge::getInstance().get_total_requests());
                        msg += " RS485-REQ#";
                        msg += String(rs.get_total_requests());
                        msg += " ERR#";
                        msg += String(rs.get_failed_responses());
                        msg += " TIMEOUT#";
                        msg += String(rs.get_timeout_count());
                        msg += " IGNORATED#";
                        msg += String(rs.get_ignored_packets());
                        msg += "]";
                        msg += "\nRS485 SN: ";
                        msg += rs.get_detected_inverter_serial();

                        msg += "\nNET: ";
                        msg += (OPENLUX_USE_ETHERNET ? "ETH" : "WIFI");
                        msg += " ";
                        msg += net.getIP().toString();
                        if (!OPENLUX_USE_ETHERNET) {
                            msg += " (";
                            msg += net.getSSID();
                            msg += ", BSSID ";
                            msg += net.getBSSID();
                            msg += ", Channel ";
                            msg += String(net.getChannel());
                            msg += ", RSSI ";
                            msg += net.getRSSI();
                            msg += " dBm, TX ";

                            const auto pwr = net.getTxPower();
                            switch (pwr) {
                                case 78:
                                    msg += "19.5";
                                    break;
                                case 76:
                                    msg += "19";
                                    break;
                                case 74:
                                    msg += "18.5";
                                    break;
                                case 68:
                                    msg += "17";
                                    break;
                                case 60:
                                    msg += "15";
                                    break;
                                case 59:
                                    msg += "14.75";
                                    break;
                                case 52:
                                    msg += "13";
                                    break;
                                case 44:
                                    msg += "11";
                                    break;
                                case 34:
                                case 32:
                                    msg += "8.5";
                                    break;
                                case 33:
                                    msg += "8.25";
                                    break;
                                case 28:
                                    msg += "7";
                                    break;
                                case 20:
                                    msg += "5";
                                    break;
                                case 8:
                                    msg += "2";
                                    break;
                                case -4:
                                    msg += "-1";
                                    break;
                                default:
                                    msg += String(pwr);
                                    break;
                            }
                            msg += " dBm, PS ";
                            msg += net.isWiFiPowerSaveDisabled() ? "off" : "on";
                            msg += ")";
                        }

                        msg += "\nWiFi: connects=";
                        msg += String(net.getWiFiConnectCount());
                        msg += " disconnects=";
                        msg += String(net.getWiFiDisconnectCount());
                        msg += " gateway=";
                        msg += net.isGatewayReachable() ? "ok" : "fail";
                        msg += " last_connect_age=";
                        const uint32_t connect_age_ms = net.getLastWiFiConnectAgeMs();
                        msg += connect_age_ms == 0 ? String("never")
                                                   : String(connect_age_ms / 1000) + "s";
                        msg += " last_disc=";
                        const uint8_t last_disc_reason = net.getLastWiFiDisconnectReason();
                        if (last_disc_reason == 0) {
                            msg += "none";
                        } else {
                            msg += net.getLastWiFiDisconnectReasonName();
                            msg += "(";
                            msg += String(last_disc_reason);
                            msg += ") age=";
                            msg += String(net.getLastWiFiDisconnectAgeMs() / 1000);
                            msg += "s";
                        }

                        msg += "\nTCP: ";
                        msg += tcp.is_running() ? "RUNNING" : "STOPPED";
                        msg += " accept=";
                        msg += tcp.is_accepting_connections() ? "yes" : "no";
                        msg += " clients=";
                        msg += String(tcp.get_client_count());
                        msg += " restarts=";
                        msg += String(tcp.get_listener_restart_count());
                        msg += " health=";
                        msg += String(tcp.get_listener_health_successes());
                        msg += "/";
                        msg += String(tcp.get_listener_health_checks());
                        msg += " fail_streak=";
                        msg += String(tcp.get_listener_health_failures());

#ifdef ENABLE_WEB_DASH
                        {
                            const auto& web = WebServerManager::getInstance();
                            msg += "\nWEB: status_req=";
                            msg += String(web.getStatusRequestCount());
                            msg += " cache_hits=";
                            msg += String(web.getStatusCacheHitCount());
                            msg += " ttl=";
                            msg += String(web.getStatusCacheTtlMs());
                            msg += "ms last_build=";
                            msg += String(web.getLastStatusBuildMs());
                            msg += "ms last_total=";
                            msg += String(web.getLastStatusTotalMs());
                            msg += "ms slow=";
                            msg += String(web.getStatusSlowCount());
                        }
#endif

                        msg += "\nHeap: ";
                        msg += String(sys.getFreeHeap());
                        msg += " bytes";

                        msg += "\nChip: ";
                        msg += sys.getChipModel();
                        msg += " (Rev ";
                        msg += String(sys.getChipRevision());
                        msg += ", ";
                        msg += String(sys.getChipCores());
                        msg += " cores)";

                        msg += "\nCPU Freq: ";
                        msg += String(sys.getCpuFreqMHz());
                        msg += " MHz";

                        msg += "\nFlash Size: ";
                        msg += String(sys.getFlashChipSize() / 1024);
                        msg += " KB";

                        msg += "\nSDK: ";
                        msg += sys.getSdkVersion();

                        msg += "\nReset HW: ";
                        msg += sys.getHardwareResetReason();
                        msg += " (code ";
                        msg += String(sys.getHardwareResetReasonCode());
                        msg += ")";

                        msg += "\nFeatures: ";
#ifdef BOARD_HAS_PSRAM
                        msg += "PSRAM ";
#endif
#ifdef USE_STATIC_IP
                        msg += "STATIC_IP ";
#endif
#ifdef ENABLE_NTP
                        msg += "NTP ";
#endif
#ifdef ENABLE_OTA
                        msg += "OTA ";
#endif
#ifdef ENABLE_TELNET
                        msg += "TELNET ";
#endif
#ifdef ENABLE_MQTT
                        msg += "MQTT ";
#endif
#if WIFI_ROAMING_ENABLED
                        msg += "ROAMING ";
#endif
#if WIFI_FAST_CONNECT
                        msg += "FAST_CONNECT ";
#endif


                        msg += "\nLast Reboot: ";
                        msg += sys.getLastRebootReason();

                        uint32_t up_sec = sys.getUptime();
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
        SystemManager::getInstance().reboot("User command");
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
    registerCommand("wifi_restart", "Restart WiFi interface",
                    [](const std::vector<String>&) -> CommandResult {
                        NetworkManager::getInstance().restartInterface();
                        return CommandResult{true, "WiFi interface restarting..."};
                    });

    registerCommand("wifi_roam", "Force WiFi scan and connect to best AP",
                    [](const std::vector<String>&) -> CommandResult {
                        NetworkManager::getInstance().forceScanAndConnect();
                        return CommandResult{true, "WiFi roam initiated"};
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
                        auto& guard_mgr = OperationGuardManager::getInstance();
                        auto guard = guard_mgr.acquireGuard(
                            OperationGuard::OperationType::WIFI_SCAN, "manual_scan");
                        if (!guard) {
                            return CommandResult{false, "Scan already running"};
                        }

                        const int n = WiFi.scanNetworks(false, false, false, 200);

                        if (n == WIFI_SCAN_FAILED) {
                            return CommandResult{false, "Scan failed"};
                        }
                        if (n == 0) {
                            return CommandResult{true, "No networks found"};
                        }
                        String out;
                        const int networks = min(n, 15); // Limit to 15
                        out.reserve(networks * 80 + 50); // ~80 char/network + header
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
                        for (int i = 0; i < networks; i++) {
                            out += i;
                            out += ") ";
                            out += WiFi.SSID(i);
                            out += " ";
                            out += sig_icon(WiFi.RSSI(i));
                            out += " (";
                            out += WiFi.RSSI(i);
                            out += " dBm)";
                            out += " Ch ";
                            out += WiFi.channel(i);
                            if (WiFi.encryptionType(i) != WIFI_AUTH_OPEN) {
                                out += " [sec]";
                            }
                            out += "\n";
                        }
                        return CommandResult{true, out};
                    });

    // log_level [0-4|reset|<tag> <0-4>|clear <tag>]
    registerCommand(
        "log_level",
        "Show/set log level: 0=DEBUG,1=INFO,2=WARN,3=ERROR,4=NONE; "
        "also reset, clear <tag>, <tag> <level>",
        [](const std::vector<String>& args) -> CommandResult {
            auto& logger = Logger::getInstance();
            if (args.empty()) {
                String out;
                out.reserve(140);
                out += "Global log level: ";
                out += formatLogLevel(logger.getGlobalLevel());
                out += "\nModule overrides: ";
                out += String(logger.getModuleOverrideCount());
#if OPENLUX_ENABLE_LOGGING
                out += "\nRuntime DEBUG/INFO/WARN logging: compiled in";
#else
                out += "\nRuntime DEBUG/INFO/WARN logging: compiled out";
#endif
                return CommandResult{true, out};
            }

            if (args[0].equalsIgnoreCase("reset")) {
                logger.resetLogLevels();
                return CommandResult{true, "Log levels reset to firmware defaults: global " +
                                               formatLogLevel(logger.getGlobalLevel()) +
                                               ", module overrides " +
                                               String(logger.getModuleOverrideCount())};
            }

            if (args[0].equalsIgnoreCase("clear")) {
                if (args.size() == 1) {
                    logger.clearModuleLevels();
                    return CommandResult{true, "All module log overrides cleared; global " +
                                                   formatLogLevel(logger.getGlobalLevel())};
                }
                logger.clearModuleLevel(args[1].c_str());
                return CommandResult{true, "Module log override cleared: " + args[1]};
            }

            int lvl = 0;
            if (args.size() == 1) {
                if (!parseLogLevel(args[0], lvl)) {
                    return CommandResult{false, "Level must be 0-4"};
                }
                logger.setAllLevels(static_cast<LogLevel>(lvl));
                String out = "Global log level set to " + formatLogLevel(logger.getGlobalLevel()) +
                             "; module overrides cleared";
#if !OPENLUX_ENABLE_LOGGING
                if (lvl < static_cast<int>(LogLevel::ERROR)) {
                    out += "\nWarning: DEBUG/INFO/WARN are compiled out in this firmware";
                }
#endif
                return CommandResult{true, out};
            }

            if (args.size() == 2) {
                if (!parseLogLevel(args[1], lvl)) {
                    return CommandResult{false, "Level must be 0-4"};
                }
                logger.setModuleLevel(args[0].c_str(), static_cast<LogLevel>(lvl));
                return CommandResult{true, "Module " + args[0] + " log level set to " +
                                               formatLogLevel(static_cast<LogLevel>(lvl))};
            }

            return CommandResult{false, "Usage: log_level [0-4|reset|clear [tag]|<tag> <0-4>]"};
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
                        const auto& sys = SystemManager::getInstance();
                        String out;
                        out.reserve(150); // Increased for PSRAM info
                        out += "Heap free: ";
                        out += sys.getFreeHeap();
                        out += " bytes\nHeap max alloc: ";
                        out += sys.getMaxAllocHeap();
                        out += " bytes";
#ifdef BOARD_HAS_PSRAM
                        out += "\nPSRAM size: ";
                        out += sys.getPsramSize();
                        out += " bytes\nPSRAM free: ";
                        out += sys.getFreePsram();
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

    // pause: block all RS485 communication (e.g., for official dongle firmware update)
    registerCommand("pause", "Pause RS485 communication (maintenance/firmware update)",
                    [](const std::vector<String>&) -> CommandResult {
                        auto& bridge = ProtocolBridge::getInstance();
                        if (bridge.is_paused()) {
                            return CommandResult{true, "Bridge already paused"};
                        }
                        bridge.set_pause(true);
                        LOGI(CMD_TAG, "Bridge paused - RS485 communication blocked");
                        return CommandResult{true, "Bridge paused - all requests will be rejected"};
                    });

    // resume: unblock RS485 communication
    registerCommand("resume", "Resume RS485 communication (end maintenance)",
                    [](const std::vector<String>&) -> CommandResult {
                        auto& bridge = ProtocolBridge::getInstance();
                        if (!bridge.is_paused()) {
                            return CommandResult{true, "Bridge already running"};
                        }
                        bridge.set_pause(false);
                        LOGI(CMD_TAG, "Bridge resumed - RS485 communication active");
                        return CommandResult{true, "Bridge resumed - requests are accepted again"};
                    });

    // pause_status: show pause state
    registerCommand("pause_status", "Show bridge pause state",
                    [](const std::vector<String>&) -> CommandResult {
                        auto& bridge = ProtocolBridge::getInstance();
                        String state = bridge.is_paused() ? "PAUSED" : "RUNNING";
                        return CommandResult{true, "Bridge state: " + state};
                    });

    // ========== Cache Commands ==========

    // cache_status: show fallback cache statistics
    registerCommand("cache_status", "Show fallback cache statistics (hits, misses, size)",
                    [](const std::vector<String>&) -> CommandResult {
                        auto& bridge = ProtocolBridge::getInstance();

                        String out;
                        out.reserve(200);
                        out += "Fallback Cache Status:\n";
                        out += "  Size: ";
                        out += String(bridge.get_cache_size());
                        out += " / 10 entries\n";
                        out += "  Hits: ";
                        out += String(bridge.get_cache_hits());
                        out += "\n  Misses: ";
                        out += String(bridge.get_cache_misses());
                        out += "\n  Hit Ratio: ";
                        out += String(bridge.get_cache_hit_ratio(), 1);
                        out += "%\n";
                        out += "  Evictions: ";
                        out += String(bridge.get_cache_invalidations());

                        uint32_t total = bridge.get_cache_hits() + bridge.get_cache_misses();
                        if (total == 0) {
                            out += "\n\n[No cache activity yet]";
                        }

                        return CommandResult{true, out};
                    });

    // cache_clear: clear all cache entries
    registerCommand("cache_clear", "Clear all fallback cache entries",
                    [](const std::vector<String>&) -> CommandResult {
                        auto& bridge = ProtocolBridge::getInstance();
                        bridge.clear_fallback_cache();
                        return CommandResult{true, "Fallback cache cleared"};
                    });

    // cache_info: show detailed cache entry info
    registerCommand("cache_info", "Show detailed fallback cache entries",
                    [](const std::vector<String>&) -> CommandResult {
                        auto& bridge = ProtocolBridge::getInstance();

                        size_t cache_size = bridge.get_cache_size();
                        if (cache_size == 0) {
                            return CommandResult{true, "Cache is empty"};
                        }

                        String out;
                        out.reserve(cache_size * 100);
                        out += "Cached Entries:\n";
                        bridge.print_cache_entries([&out](const String& line) {
                            out += line;
                            out += "\n";
                        });

                        return CommandResult{true, out};
                    });
}
