/**
 * @file network_manager.h
 * @brief Network connectivity manager (WiFi/Ethernet) with OTA support
 *
 * @license GPL-3.0
 * @author OpenLux Contributors
 */

#pragma once

#include "../config.h"

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>

#include <functional>

#include <IPAddress.h>
#include <Preferences.h>
#if OPENLUX_USE_ETHERNET
#include <ETH.h>
#else
#include <WiFi.h>
#include <WiFiManager.h>
#endif

// Callback types
using NetworkConnectedCallback = std::function<void()>;
using NetworkDisconnectedCallback = std::function<void()>;
using OTAProgressCallback = std::function<void(unsigned int progress, unsigned int total)>;
using OTAStartCallback = std::function<void()>;
using OTAEndCallback = std::function<void()>;
using OTAErrorCallback = std::function<void()>;

/**
 * @brief WiFi and OTA Management
 *
 * Features:
 * - Automatic WiFi connection with retry
 * - Static IP or DHCP
 * - Automatic reconnection
 * - OTA (Over-The-Air) updates
 * - mDNS for hostname resolution
 */
class NetworkManager {
  public:
    static NetworkManager& getInstance();

    class ScanGuard {
      public:
        ScanGuard(const ScanGuard&) = delete;
        ScanGuard& operator=(const ScanGuard&) = delete;
        ScanGuard(ScanGuard&& other) noexcept { moveFrom(std::move(other)); }
        ScanGuard& operator=(ScanGuard&& other) noexcept {
            if (this != &other) {
                release();
                moveFrom(std::move(other));
            }
            return *this;
        }
        ~ScanGuard() { release(); }
        explicit operator bool() const { return active_; }
        void release();

      private:
        friend class NetworkManager;
        ScanGuard(NetworkManager* owner, bool active, const char* reason)
            : owner_(owner), active_(active), reason_(reason) {}
        void moveFrom(ScanGuard&& other) {
            owner_ = other.owner_;
            active_ = other.active_;
            reason_ = other.reason_;
            other.owner_ = nullptr;
            other.active_ = false;
            other.reason_ = nullptr;
        }
        NetworkManager* owner_ = nullptr;
        bool active_ = false;
        const char* reason_ = nullptr;
    };

    ScanGuard acquireScanGuard(const char* reason = nullptr);
    bool isScanning() const { return scanning_in_progress_; }

    // Lifecycle
    void begin(const char* ssid, const char* password, const char* hostname);
    void loop(); // Call in main loop

    // WiFi configuration
    void setStaticIP(IPAddress ip, IPAddress gateway, IPAddress subnet, IPAddress dns1);
    void setHostname(const char* hostname);

    // WiFi status
    bool isConnected();
#if OPENLUX_USE_ETHERNET
    IPAddress getIP() const { return ETH.localIP(); }
    String getSSID() const { return String("ETH"); }
    int getRSSI() const { return 0; }
    String getMAC() const { return ETH.macAddress(); }
    int8_t getTxPower() const { return 0; }
    uint32_t getChannel() const { return 0; }
#else
    IPAddress getIP() const { return WiFi.localIP(); }
    String getSSID() const { return WiFi.SSID(); }
    int getRSSI() const { return WiFi.RSSI(); }
    String getMAC() const { return WiFi.macAddress(); }
    int8_t getTxPower() const { return WiFi.getTxPower(); }
    uint32_t getChannel() const { return WiFi.channel(); }
#endif

    // OTA configuration
    void setupOTA(const char* hostname, const char* password, uint16_t port = 3232);
    void enableOTA(bool enable) { ota_enabled_ = enable; }
    bool isOTAEnabled() const { return ota_enabled_; }
    bool isOTAInProgress() const { return ota_in_progress_; }

    // mDNS
    void setupMDNS(const char* hostname);

    // Utilities
    void softReconnect();                  // disconnect + reconnect without power cycling WiFi
    void restartInterface();               // power-cycle WiFi STA and reconnect with stored creds
    void forceScanAndConnect();            // Force a scan and connect to best AP
    void rebootDevice(const char* reason); // log and reboot
    bool startProvisioningPortal();        // start AP portal for WiFi config (blocking)
    void clearCredentials();               // wipe stored WiFi credentials (NVS)
    void markBootSuccessful();             // reset boot failure counter
    bool validateConnection();             // Active check (TCP connect to gateway)

    // Callbacks
    void onConnected(const NetworkConnectedCallback& callback) { on_connected_ = callback; }
    void onDisconnected(const NetworkDisconnectedCallback& callback) {
        on_disconnected_ = callback;
    }
    void onOTAProgress(const OTAProgressCallback& callback) { on_ota_progress_ = callback; }
    void onOTAStart(const OTAStartCallback& callback) { on_ota_start_ = callback; }
    void onOTAEnd(const OTAEndCallback& callback) { on_ota_end_ = callback; }
    void onOTAError(const OTAErrorCallback& callback) { on_ota_error_ = callback; }

  private:
    NetworkManager() = default;
    ~NetworkManager() = default;
    NetworkManager(const NetworkManager&) = delete;
    NetworkManager& operator=(const NetworkManager&) = delete;

    void connectWiFi(bool force_scan = false);

    // State
    bool connected_ = false;

    void checkConnection();
    void roamingIfNeeded();
    void roaming();
    void handleOTA();

    void runTask();
    static void taskTrampoline(void* arg);
    void startNetworkTask();

    int scanAndFindBestAP(int& bestRSSI);

    const char* ssid_ = nullptr;
    const char* password_ = nullptr;
    const char* hostname_ = nullptr;

    bool use_static_ip_ = false;
    IPAddress static_ip_;
    IPAddress gateway_;
    IPAddress subnet_;
    IPAddress dns1_;

    bool was_connected_ = false;
    bool ota_enabled_ = false;
    bool ota_in_progress_ = false;
    bool portal_opened_once_ = false;
    bool boot_failures_loaded_ = false;
    bool use_ethernet_ = false;
    bool eth_connected_ = false;
    uint32_t last_connect_attempt_ = 0;
    uint32_t last_status_log_ = 0;
    uint32_t disconnected_since_ = 0;
    bool scanning_in_progress_ = false;
    const char* scan_owner_ = nullptr;
    bool watchdog_reconnect_done_ = false;
    bool watchdog_restart_done_ = false;
    bool watchdog_portal_done_ = false;
    uint32_t last_scan_ms_ = 0;
    uint32_t last_validation_ms_ = 0;
    bool gateway_reachable_ = true;
    uint8_t boot_failures_ = 0;

    NetworkConnectedCallback on_connected_ = nullptr;
    NetworkDisconnectedCallback on_disconnected_ = nullptr;
    OTAProgressCallback on_ota_progress_ = nullptr;
    OTAStartCallback on_ota_start_ = nullptr;
    OTAEndCallback on_ota_end_ = nullptr;
    OTAErrorCallback on_ota_error_ = nullptr;

    Preferences prefs_;

    static constexpr uint32_t CONNECT_RETRY_DELAY = 5000; // 5 seconds
    static constexpr uint32_t STATUS_LOG_INTERVAL = 120 * 1000;
    static constexpr uint32_t VALIDATION_INTERVAL_MS = 120 * 1000; // 120 seconds

    void logHeapStatus(const char* context);
#if OPENLUX_USE_ETHERNET
    void handleEthernetEvent(WiFiEvent_t event, WiFiEventInfo_t info);
#endif
#if !OPENLUX_USE_ETHERNET
    void handleWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);
#endif

    bool beginScan(const char* reason);
    void endScan();
};
