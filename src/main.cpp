/**
 * @file main.cpp
 * @brief OpenLux - Open Source Wi-Fi Bridge for Luxpower Inverters
 *
 * Complete Wi-Fi Bridge emulating Luxpower Wi-Fi dongle protocol.
 * Enables Home Assistant integration without proprietary hardware.
 *
 * Features:
 * - Wi-Fi connection with static IP or DHCP
 * - OTA (Over-The-Air) firmware updates
 * - Serial logging + Telnet logging (port 23)
 * - mDNS (openlux.local)
 * - NTP time synchronization
 * - RS485 communication with Luxpower inverter
 * - TCP Server (port 8000) for Home Assistant
 * - Protocol Bridge (Wi-Fi ↔ RS485 translation)
 *
 * @author f-garofalo
 * @date 2025-12-08
 * @license GPL-3.0
 */

#include "config.h"
#include "modules/command_manager.h"
#include "modules/logger.h"
#include "modules/network_manager.h"
#include "modules/ntp_manager.h"
#include "modules/protocol_bridge.h"
#include "modules/rs485_manager.h"
#include "modules/system_manager.h"
#include "modules/tcp_server.h"
#ifdef ENABLE_WEB_DASH
#include "modules/web_server.h"
#endif

#include <Arduino.h>
#include <Esp.h>

// Logging tag
static const char* TAG = "main";

// Global singleton instances
Logger& logger = Logger::getInstance();
SystemManager& sys = SystemManager::getInstance();
NetworkManager& network = NetworkManager::getInstance();
NTPManager& ntp = NTPManager::getInstance();
RS485Manager& rs485 = RS485Manager::getInstance();
TCPServer& tcp_server = TCPServer::getInstance();
ProtocolBridge& bridge = ProtocolBridge::getInstance();

// Forward declarations
void printWelcomeBanner();
void printSystemInfo();
void setupWiFi();
void setupOTA();
void setupTelnet();
void setupNTP();
void setupRS485();
void setupTCPServer();
void setupBridge();
#ifdef ENABLE_WEB_DASH
void setupWebServer();
#endif
#ifdef ENABLE_WEB_DASH
#define WEB_LOOP() WebServerManager::getInstance().loop()
#else
#define WEB_LOOP()
#endif

/**
 * @brief Initial setup - runs once at boot
 */
void setup() {
    // Initialize logger (serial + telnet)
    logger.begin(115200);

    // Initialize system manager (reads reboot reason)
    sys.begin();

    CommandManager::getInstance().registerCoreCommands();

    // Print welcome banner
    printWelcomeBanner();

    // Print system information
    printSystemInfo();

    // Setup RS485 (can start immediately, no network required)
    setupRS485();

    // Setup Wi-Fi (with callbacks for network services)
    setupWiFi();

#ifdef ENABLE_WEB_DASH
    setupWebServer();
#endif

    LOGI(TAG, "Setup completed - entering main loop...");
}

/**
 * @brief Main loop - runs continuously
 */
void loop() {
    // Update Wi-Fi manager (handles reconnections and OTA)
    network.loop();

#ifdef ENABLE_TELNET
    // Update logger (handles Telnet clients)
    logger.loop();
#endif

#ifdef ENABLE_NTP
    // Update NTP manager (handles periodic time sync)
    ntp.loop();
#endif

    // Update RS485 manager (handles timeouts and parsing)
    rs485.loop();

    // Update TCP server (handles client connections)
    tcp_server.loop();

    // Update protocol bridge (coordinates TCP ↔ RS485)
    bridge.loop();

#ifdef ENABLE_WEB_DASH
    WebServerManager::getInstance().loop();
#endif

    // Feed watchdog and add small delay
    yield(); // Feed watchdog timer
    delay(10);
}

/**
 * @brief Print welcome banner at startup
 */
void printWelcomeBanner() {
    Serial.println();
    Serial.println("  ===========================================");
    Serial.printf("      %s\n", FIRMWARE_NAME);
    Serial.println("        Open Source Home Assistant         ");
    Serial.println("             Integration                   ");
    Serial.println("  ===========================================");
    Serial.printf("  Version: %s\n", FIRMWARE_VERSION);
    Serial.printf("  Build: %s\n", BUILD_TIMESTAMP);
    Serial.println();
}

/**
 * @brief Print system information
 */
void printSystemInfo() {
    Serial.println("--- System Information ---");

    LOGI(TAG, "Chip: %s (Rev %d, %d cores)", sys.getChipModel(), sys.getChipRevision(),
         sys.getChipCores());
    LOGI(TAG, "CPU Frequency: %d MHz", sys.getCpuFreqMHz());
    LOGI(TAG, "Flash Size: %d KB", sys.getFlashChipSize() / 1024);
    LOGI(TAG, "Free Heap: %d KB", sys.getFreeHeap() / 1024);
    LOGI(TAG, "SDK Version: %s", sys.getSdkVersion());

#ifdef USE_STATIC_IP
    LOGI(TAG, "Network Mode: Static IP");
#else
    LOGI(TAG, "Network Mode: DHCP");
#endif

    Serial.println();
}

/**
 * @brief Configure Wi-Fi connection and network services
 */
void setupWiFi() {
    LOGI(TAG, "Configuring WiFi...");

// Configure static IP if enabled
#ifdef USE_STATIC_IP
    network.setStaticIP(STATIC_IP, GATEWAY, SUBNET, DNS1);
#endif

    // Callback when Wi-Fi connects successfully
    network.onConnected([]() {
        LOGI(TAG, "Network connected - initializing services...");

#ifdef ENABLE_NTP
        // Setup NTP time sync (must be before other services)
        setupNTP();
#endif

#ifdef ENABLE_OTA
        // Setup OTA after Wi-Fi connection
        setupOTA();
#endif

#ifdef ENABLE_TELNET
        // Setup Telnet after Wi-Fi connection
        setupTelnet();
#endif

        // Setup TCP Server for Home Assistant
        setupTCPServer();

        // Setup Protocol Bridge
        setupBridge();

        // Setup mDNS
        network.setupMDNS(WIFI_HOSTNAME);

        LOGI(TAG, "All services initialized!");
        Serial.println();

        Serial.println("============================================");
        Serial.println("         * BRIDGE READY AND ONLINE *       ");
        Serial.println("============================================");
        Serial.printf("  Web:     http://%s.local\n", WIFI_HOSTNAME);
#ifdef ENABLE_TELNET
        Serial.printf("  Telnet:  telnet %s.local\n", WIFI_HOSTNAME);
#endif
#ifdef ENABLE_OTA
        Serial.printf("  OTA:     Ready for updates\n");
#endif
        Serial.printf("  TCP:     Port %d (Home Assistant)\n", TCP_SERVER_PORT);
        Serial.printf("  RS485:   %d baud (Inverter)\n", RS485_BAUD_RATE);
#ifdef ENABLE_NTP
        Serial.printf("  Time:    %s\n", ntp.getFormattedTime().c_str());
#endif
        Serial.println("============================================");

        Serial.println();
    });

    // Callback when Wi-Fi disconnects
    network.onDisconnected([]() { LOGW(TAG, "WiFi disconnected - will attempt reconnection"); });

    // Start Wi-Fi connection
    network.begin(WIFI_SSID, WIFI_PASSWORD, WIFI_HOSTNAME);
}

/**
 * @brief Configure OTA (Over-The-Air updates)
 */
void setupOTA() {
#ifdef ENABLE_OTA
    LOGI(TAG, "Configuring OTA...");

    network.setupOTA(OTA_HOSTNAME, OTA_PASSWORD, OTA_PORT);

    // OTA progress callback
    network.onOTAProgress([](unsigned int progress, unsigned int total) {
        // Progress bar every 10%
        static uint8_t last_percent = 0;
        uint8_t percent = (progress * 100) / total;

        if (percent != last_percent && percent % 10 == 0) {
            Serial.printf("OTA Progress: [");
            for (int i = 0; i < 10; i++) {
                if (i < percent / 10) {
                    Serial.print("=");
                } else {
                    Serial.print(" ");
                }
            }
            Serial.printf("] %u%%\n", percent);
            last_percent = percent;
        }
    });

    LOGI(TAG, "OTA configured and ready");
#else
    LOGI(TAG, "OTA disabled in config.h");
#endif
}

/**
 * @brief Configure Telnet remote logging
 */
void setupTelnet() {
#ifdef ENABLE_TELNET
    LOGI(TAG, "Starting Telnet server...");
    logger.startTelnet(TELNET_PORT);
    LOGI(TAG, "Telnet server started on port %d", TELNET_PORT);
    LOGI(TAG, "Connect with: telnet %s %d", network.getIP().toString().c_str(), TELNET_PORT);
#else
    LOGI(TAG, "Telnet disabled in config.h");
#endif
}

/**
 * @brief Configure NTP (Network Time Protocol) for time synchronization
 */
void setupNTP() {
#ifdef ENABLE_NTP
    logger.printSeparator("Time Synchronization");
    LOGI(TAG, "Starting NTP time sync...");

    ntp.begin(NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);

    if (ntp.isSynced()) {
        LOGI(TAG, "✓ NTP synchronized");
        LOGI(TAG, "  Current time: %s", ntp.getFormattedTime().c_str());
        LOGI(TAG, "  Timezone: %s", TIMEZONE);
    } else {
        LOGW(TAG, "✗ NTP sync pending (will retry in background)");
    }

    Serial.println();
#else
    LOGI(TAG, "NTP disabled in config.h");
#endif
}

/**
 * @brief Configure RS485 communication with inverter
 */
void setupRS485() {
    logger.printSeparator("RS485 Communication");
    LOGI(TAG, "Initializing RS485...");

    // Initialize RS485 on Serial2
    rs485.begin(Serial2, RS485_TX_PIN, RS485_RX_PIN, RS485_DE_PIN, RS485_BAUD_RATE);

    // Read inverter serial from registers 115-119 to validate RS485 link
    rs485.probe_inverter_serial();

    LOGI(TAG, "✓ RS485 initialized");
    Serial.println();
}

/**
 * @brief Configure TCP Server for Home Assistant connections
 */
void setupTCPServer() {
    logger.printSeparator("TCP Server");
    LOGI(TAG, "Starting TCP Server...");

    tcp_server.begin(TCP_SERVER_PORT, TCP_MAX_CLIENTS);

    LOGI(TAG, "✓ TCP Server started");
    LOGI(TAG, "  Port: %d", TCP_SERVER_PORT);
    LOGI(TAG, "  Max clients: %d", TCP_MAX_CLIENTS);
    Serial.println();
}

/**
 * @brief Configure Protocol Bridge (WiFi ↔ RS485 translation)
 */
void setupBridge() {
    logger.printSeparator("Protocol Bridge");
    LOGI(TAG, "Initializing Protocol Bridge...");

    // Configure bridge
    bridge.begin(DONGLE_SERIAL);
    bridge.set_tcp_server(&tcp_server);
    bridge.set_rs485_manager(&rs485);

    // Configure TCP server to use the bridge
    tcp_server.set_bridge(&bridge);

    LOGI(TAG, "✓ Protocol Bridge initialized");
    LOGI(TAG, "  Dongle SN: %s", DONGLE_SERIAL);
    LOGI(TAG, "  Mode: WiFi ↔ RS485");
    Serial.println();
}

#ifdef ENABLE_WEB_DASH
/**
 * @brief Initialize web dashboard/api
 */
void setupWebServer() {
    WebServerManager::getInstance().begin();
}
#endif
