/**
 * @file config.h
 * @brief Main Configuration File for OpenLux WiFi Bridge
 *
 * ⭐ THIS IS THE ONLY FILE YOU NEED TO EDIT! ⭐
 *
 * All user-configurable parameters are in this single file.
 * Adjust these settings, then compile and upload.
 *
 * IMPORTANT:
 * - Copy secrets.h.example to secrets.h and fill in your WiFi credentials
 * - All other settings are configured below in this file
 */

#pragma once

// Include secrets file (WiFi password, OTA password)
#include "secrets.h"

// ============================================================================
// 🔧 HARDWARE CONFIGURATION
// ============================================================================

/**
 * @brief RS485 Pin Configuration
 *
 * Adjust these pins based on your ESP32 wiring to the RS485 module.
 *
 * Common RS485 modules: MAX485, SP485, XY-017, XY-K485
 * https://electronics.stackexchange.com/questions/244425/how-does-this-rs485-module-work
 *
 * Wiring:
 * - TX_PIN: ESP32 TX → RS485 DI (Data Input)
 * - RX_PIN: ESP32 RX → RS485 RO (Receiver Output)
 * - DE_PIN: ESP32 GPIO → RS485 DE/RE (Direction control)
 * - RS485 A/B: Connect to inverter RS485 A/B terminals
 *
 * 📌 DE_PIN (Direction Enable):
 * - Set to GPIO number if your module has DE/RE pins (most common)
 * - Set to -1 if your module has automatic direction control
 * - Some modules tie DE/RE to VCC or have internal auto-direction
 *
 * Examples:
 * - MAX485 with DE/RE: Use GPIO (e.g., GPIO 4)
 * - Auto-direction module: Set to -1
 * - Module without DE/RE pins: Set to -1
 *
 */
#define RS485_TX_PIN 17       ///< UART TX pin (to RS485 DI)
#define RS485_RX_PIN 16       ///< UART RX pin (from RS485 RO)
#define RS485_DE_PIN -1       ///< Direction control (-1 = auto/disabled, or GPIO number)
#define RS485_BAUD_RATE 19200 ///< Fixed baud rate (per Luxpower spec)

// Ethernet (adjust pins/phy for your board if OPENLUX_USE_ETHERNET=1)
#define ETH_PHY_TYPE ETH_PHY_LAN8720
#define ETH_PHY_ADDR 0
#define ETH_PHY_POWER_PIN -1
#define ETH_PHY_MDC_PIN 23
#define ETH_PHY_MDIO_PIN 18
#define ETH_PHY_CLK_MODE ETH_CLOCK_GPIO17_OUT // adjust for your hardware (e.g., ETH_CLOCK_GPIO0_IN)

// ============================================================================
// 🌐 NETWORK CONFIGURATION
// ============================================================================

/**
 * @brief WiFi and Network Settings
 */
#define WIFI_HOSTNAME "openlux"          ///< mDNS hostname (openlux.local)
#define OTA_HOSTNAME "openlux"           ///< OTA update hostname
#define OTA_PORT 3232                    ///< OTA update port
#define WIFI_PORTAL_SSID "OpenLux-Setup" ///< SSID for first-boot WiFi portal
#define WIFI_PORTAL_PASS "openlux123"    ///< Password for first-boot WiFi portal (8+ chars)
#define WIFI_PORTAL_TIMEOUT_S 300        ///< Portal timeout in seconds (5 minutes)
#define OPENLUX_USE_ETHERNET 0           ///< Set to 1 to use onboard Ethernet instead of WiFi

/**
 * @brief Network Task Core Pinning
 *
 * Controls which CPU core the network manager task runs on.
 * - 0: Core 0 (Protocol CPU) - Default for WiFi/Network tasks
 * - 1: Core 1 (App CPU) - Main application loop
 * - -1: No pinning (FreeRTOS decides)
 *
 * If you experience instability or watchdog resets, try changing this to 1 or -1.
 */
#define NETWORK_TASK_CORE 0

/**
 * @brief Log Persistence
 *
 * Number of log lines to keep in RTC memory to survive soft reboots.
 * Useful for debugging crashes.
 * Set to 0 to disable.
 */
#define LOG_PERSISTENCE_LINES 150

/**
 * @brief WiFi TX Power
 *
 * Adjust transmission power to improve stability with certain APs.
 * Options:
 * - WIFI_POWER_19_5dBm
 * - WIFI_POWER_19dBm
 * - WIFI_POWER_18_5dBm
 * - WIFI_POWER_17dBm
 * - WIFI_POWER_15dBm
 * - WIFI_POWER_13dBm
 * - WIFI_POWER_11dBm
 * - WIFI_POWER_8_5dBm
 * - WIFI_POWER_7dBm
 * - WIFI_POWER_5dBm
 * - WIFI_POWER_2dBm
 * - WIFI_POWER_MINUS_1dBm
 * Comment if unsure to use default.
 */
// #define WIFI_TX_POWER WIFI_POWER_8_5dBm

/**
 * @brief Periodic WiFi Scan
 *
 * If enabled, the device will periodically scan for the configured SSID
 * and reconnect if a stronger AP is found.
 * Useful in mesh networks where the device might stick to a distant AP.
 */
#define WIFI_PERIODIC_SCAN_ENABLED 1                    ///< Set to 1 to enable periodic scanning
#define WIFI_PERIODIC_SCAN_INTERVAL_MS (40 * 60 * 1000) ///< Scan interval in ms
#define WIFI_RSSI_THRESHOLD_DBM 5 ///< Minimum RSSI improvement to trigger reconnect

/**
 * @brief Fast WiFi Connection
 *
 * If enabled (1), skips the initial AP scan and connects to the first available AP.
 * Faster boot, but might connect to a weaker signal in mesh networks.
 */
#define WIFI_FAST_CONNECT 0 ///< Set to 1 to disable initial scan

// ============================================================================
// 📡 MQTT CONFIGURATION
// ============================================================================

/**
 * @brief MQTT Settings
 *
 * Configure your MQTT broker connection here.
 * Leave MQTT_HOST empty ("") to disable MQTT.
 */
#define MQTT_HOST ""                          ///< Broker IP or hostname (e.g., "192.168.1.10")
#define MQTT_PORT 1883                        ///< Broker port (default: 1883)
#define MQTT_USER ""                          ///< MQTT Username (optional)
#define MQTT_PASS ""                          ///< MQTT Password (optional)
#define MQTT_CLIENT_ID "openlux-bridge"       ///< Client ID (must be unique)
#define MQTT_TOPIC_PREFIX "openlux"           ///< Topic prefix (e.g., openlux/status)
#define MQTT_DISCOVERY_PREFIX "homeassistant" ///< Home Assistant discovery prefix
#define MQTT_STATUS_INTERVAL_MS 60000         ///< MQTT status update interval (ms)

// ============================================================================
// 🛠️ SERVICE CONFIGURATION
// ============================================================================

/**
 * @brief TCP Server Settings
 *
 * The TCP server listens for Home Assistant connections on port 8000,
 * emulating the Luxpower WiFi dongle protocol.
 */
#define TCP_SERVER_PORT 8000         ///< Luxpower WiFi dongle port (don't change!)
#define TCP_MAX_CLIENTS 5            ///< Maximum simultaneous clients
#define TCP_CLIENT_TIMEOUT_MS 300000 ///< Client timeout (5 minutes)

/**
 * @brief Web Dashboard Settings
 */
#define ENABLE_WEB_DASH         ///< Enable built-in web dashboard/API
#define WEB_DASH_PORT 80        ///< Web dashboard port
#define WEB_DASH_USER "admin"   ///< Basic auth user for web dashboard
#define WEB_DASH_PASS "openlux" ///< Basic auth password for web dashboard

/**
 * @brief Telnet Remote Logging
 *
 * Connect via: nc <ip_address> 23
 * or: telnet <ip_address> 23
 */
#define TELNET_PORT 23 ///< Telnet port for remote logs

/**
 * @brief NTP Server Configuration
 *
 * Configure NTP servers based on your location for best accuracy.
 * The ESP32 will try servers in order until one responds.
 *
 * Examples:
 * - Europe: pool.ntp.org, time.google.com
 * - Italy: ntp1.inrim.it, ntp2.inrim.it
 * - US: time.nist.gov, time.google.com
 * - Asia: asia.pool.ntp.org
 */
#define NTP_SERVER_1 "ntp1.inrim.it"   ///< Primary NTP server
#define NTP_SERVER_2 "ntp2.inrim.it"   ///< Secondary NTP server
#define NTP_SERVER_3 "time.google.com" ///< Fallback NTP server

/**
 * @brief Timezone Configuration (POSIX format)
 *
 * Set your timezone using POSIX TZ format.
 * This automatically handles daylight saving time (DST).
 *
 * Common timezones:
 * - CET-1CEST,M3.5.0,M10.5.0/3  (Central European Time - Italy, Germany, France)
 * - GMT0BST,M3.5.0/1,M10.5.0    (UK)
 * - EST5EDT,M3.2.0,M11.1.0      (US Eastern)
 * - PST8PDT,M3.2.0,M11.1.0      (US Pacific)
 * - JST-9                       (Japan)
 * - AEST-10AEDT,M10.1.0,M4.1.0  (Australia Eastern)
 *
 * Full list: https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
 */
#define TIMEZONE "CET-1CEST,M3.5.0,M10.5.0/3" ///< Timezone (Italy/Rome)

// ============================================================================
// ⚙️ SYSTEM & PERFORMANCE
// ============================================================================

/**
 * @brief Firmware Information
 */
#define FIRMWARE_VERSION "1.0.3"            ///< Semantic version
#define FIRMWARE_NAME "OpenLux WiFi Bridge" ///< Project name
#include "build_info.h"                     ///< Auto-generated build timestamp

/**
 * @brief Identifiers
 */
#define DONGLE_SERIAL "0123456789" ///< Emulated dongle serial

/**
 * @brief Log level (0=DEBUG, 1=INFO, 2=WARN, 3=ERROR, 4=NONE)
 *
 * Controls which log lines are emitted. Lower = more verbose.
 */
#define OPENLUX_LOG_LEVEL 1

/**
 * @brief Performance Tuning
 */
#define MAIN_LOOP_DELAY_MS 10        ///< Main loop delay (ms)
#define WATCHDOG_TIMEOUT_S 30        ///< Watchdog timeout (seconds)
#define STATUS_LOG_INTERVAL_MS 60000 ///< Status update interval (ms)
#define WIFI_WATCHDOG_RECONNECT_DELAY_MS \
    (2 * 60 * 1000) ///< After this downtime, try WiFi reconnect
#define WIFI_WATCHDOG_RESTART_DELAY_MS \
    (5 * 60 * 1000) ///< After this downtime, restart WiFi interface
#define WIFI_WATCHDOG_REBOOT_DELAY_MS (10 * 60 * 1000) ///< After this downtime, reboot device
#define WIFI_WATCHDOG_PORTAL_DELAY_MS \
    (20 * 60 * 1000) ///< After this downtime, open provisioning portal (AP) once
#define RS485_PROBE_BACKOFF_BASE_MS 5000           ///< Initial backoff for RS485 probe retry
#define RS485_PROBE_BACKOFF_MAX_MS (5 * 60 * 1000) ///< Max backoff for RS485 probe retry
#define COMMAND_DEBOUNCE_MS 10000   ///< Debounce window for reboot/wifi_restart commands
#define BOOT_FAIL_RESET_THRESHOLD 5 ///< After N failed boots, clear WiFi creds and open portal

// ============================================================================
// 🚩 FEATURE FLAGS
// ============================================================================

/**
 * @brief Feature Flags - Enable/Disable Optional Services
 *
 * Comment out (#define → //#define) to disable a service.
 * This saves memory and reduces code size if you don't need certain features.
 *
 * Examples:
 * - Don't need OTA updates? Comment out ENABLE_OTA
 * - Don't need time sync? Comment out ENABLE_NTP
 * - Don't need remote logs? Comment out ENABLE_TELNET
 */
#define ENABLE_NTP    ///< Enable time synchronization (NTP servers)
#define ENABLE_OTA    ///< Enable OTA updates (wireless firmware upload)
#define ENABLE_TELNET ///< Enable telnet logging (remote log access)
// #define ENABLE_MQTT   ///< Enable MQTT client

// ============================================================================
// 🔧 LOCAL CONFIGURATION OVERRIDES
// ============================================================================
// Include local configuration overrides if available.
// This file is ignored by git, so your changes won't be overwritten or committed.
// To avoid "macro redefined" warnings, it is best practice to #undef the
// variable before redefining it.
#if __has_include("config.local.h")
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#include "config.local.h"
#pragma GCC diagnostic pop
#endif
