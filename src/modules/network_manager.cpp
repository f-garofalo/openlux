/**
 * @file network_manager.cpp
 * @brief Network connectivity implementation
 *
 * @license GPL-3.0
 * @author OpenLux Contributors
 */
#include "network_manager.h"

#include "../config.h"
#include "logger.h"
#include "operation_guard.h"
#include "protocol_bridge.h"
#include "system_manager.h"

#include <Esp.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char* TAG = "net";

NetworkManager& NetworkManager::getInstance() {
    static NetworkManager instance;
    return instance;
}

void NetworkManager::taskTrampoline(void* arg) {
    NetworkManager* nm = static_cast<NetworkManager*>(arg);
    while (true) {
        nm->runTask();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void NetworkManager::runTask() {
    auto& guard_mgr = OperationGuardManager::getInstance();

    if (!guard_mgr.isOTAInProgress()) {
        checkConnection();
    }

    if (ota_enabled_ && !guard_mgr.isOTAInProgress()) {
        handleOTA();
    }
}

void NetworkManager::begin(const char* ssid, const char* password, const char* hostname) {
    // Track boot attempts to detect repeated failed boots
    prefs_.begin("openlux", false);

    boot_failures_ = prefs_.getUChar("boot_fail", 0);
    boot_failures_loaded_ = true;
    boot_failures_++;
    prefs_.putUChar("boot_fail", boot_failures_);
    LOGI(TAG, "Boot failure counter: %u", boot_failures_);
    if (boot_failures_ >= BOOT_FAIL_RESET_THRESHOLD) {
#if OPENLUX_USE_ETHERNET
        LOGW(TAG, "Exceeded boot fail threshold (%d); Ethernet mode, skipping portal",
             BOOT_FAIL_RESET_THRESHOLD);
#else
        LOGW(TAG, "Exceeded boot fail threshold (%d), clearing WiFi credentials and opening portal",
             BOOT_FAIL_RESET_THRESHOLD);
        clearCredentials();
        prefs_.putUChar("boot_fail", 0);
        startProvisioningPortal();
        return;
#endif
    }
    prefs_.end();

    ssid_ = ssid;
    password_ = password;
    hostname_ = hostname;

    LOGI(TAG, "Initializing Network Manager (%s)", use_ethernet_ ? "Ethernet" : "WiFi");
    LOGI(TAG, "  Hostname: %s", hostname_);
    LOGI(TAG, "  NET mode: %s", use_ethernet_ ? "ETH" : "WIFI");

#if OPENLUX_USE_ETHERNET
    WiFi.onEvent(
        [this](WiFiEvent_t event, WiFiEventInfo_t info) { handleEthernetEvent(event, info); },
        ARDUINO_EVENT_MAX);

    if (!ETH.begin(ETH_PHY_ADDR, ETH_PHY_POWER_PIN, ETH_PHY_MDC_PIN, ETH_PHY_MDIO_PIN, ETH_PHY_TYPE,
                   ETH_PHY_CLK_MODE)) {
        LOGE(TAG, "ETH begin failed");
    } else {
        LOGI(TAG, "ETH init requested");
    }
    BaseType_t core = NETWORK_TASK_CORE;
    if (core < 0)
        core = tskNO_AFFINITY;
    xTaskCreatePinnedToCore(taskTrampoline, "NetMgrTask", 4096, this, 1, NULL, core);
    return;

#endif

#if !OPENLUX_USE_ETHERNET
    LOGI(TAG, "  SSID: %s", ssid_);

    WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) { handleWiFiEvent(event, info); });

    // Set hostname early
    if (hostname_) {
        WiFi.setHostname(hostname_);
    }
    WiFi.mode(WIFI_STA);

#ifdef WIFI_TX_POWER
    WiFi.setTxPower(WIFI_TX_POWER);
#endif


    // Provisioning path: if no SSID provided, start captive portal
    bool has_ssid = (ssid_ && strlen(ssid_) > 0);
    if (!has_ssid) {
        LOGW(TAG, "No WiFi SSID provided, starting setup portal...");
        WiFiManager wm;
        wm.setDebugOutput(false);
        wm.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT_S);
        wm.setHostname(hostname_ ? hostname_ : WIFI_HOSTNAME);

        // Static IP (if configured)
        if (use_static_ip_) {
            LOGI(TAG, "Using static IP: %s", static_ip_.toString().c_str());
            wm.setSTAStaticIPConfig(static_ip_, gateway_, subnet_, dns1_);
        }

        bool res = wm.autoConnect(WIFI_PORTAL_SSID, WIFI_PORTAL_PASS);
        if (!res) {
            LOGE(TAG, "WiFi portal timeout or failed connection");
        } else {
            LOGI(TAG, "✓ WiFi connected via portal");
            LOGI(TAG, "  IP: %s", WiFi.localIP().toString().c_str());
        }
        BaseType_t core = NETWORK_TASK_CORE;
        if (core < 0)
            core = tskNO_AFFINITY;
        xTaskCreatePinnedToCore(taskTrampoline, "NetMgrTask", 4096, this, 1, NULL, core);
        return;
    }

    // Direct connection with provided credentials
    if (use_static_ip_) {
        LOGI(TAG, "Using static IP: %s", static_ip_.toString().c_str());
        if (!WiFi.config(static_ip_, gateway_, subnet_, dns1_)) {
            LOGE(TAG, "Failed to configure static IP");
        }
    }

    // Start connection
    connectWiFi();
    BaseType_t core = NETWORK_TASK_CORE;
    if (core < 0)
        core = tskNO_AFFINITY;
    xTaskCreatePinnedToCore(taskTrampoline, "NetMgrTask", 4096, this, 1, nullptr, core);
#endif
}

void NetworkManager::loop() {
    // Logic moved to runTask() executed by FreeRTOS task on Core 0
}

void NetworkManager::setStaticIP(IPAddress ip, IPAddress gateway, IPAddress subnet,
                                 IPAddress dns1) {
    use_static_ip_ = true;
    static_ip_ = ip;
    gateway_ = gateway;
    subnet_ = subnet;
    dns1_ = dns1;
}

void NetworkManager::setHostname(const char* hostname) {
    hostname_ = hostname;
#if OPENLUX_USE_ETHERNET
    ETH.setHostname(hostname);
#else
    WiFi.setHostname(hostname);
#endif
}

void NetworkManager::setupOTA(const char* hostname, const char* password, uint16_t port) {
    LOGI(TAG, "Setting up OTA");
    LOGI(TAG, "  Hostname: %s", hostname);
    LOGI(TAG, "  Port: %d", port);

    ArduinoOTA.setHostname(hostname);
    ArduinoOTA.setPassword(password);
    ArduinoOTA.setPort(port);

    ArduinoOTA.onStart([this]() {
        auto& guard_mgr = OperationGuardManager::getInstance();
        ota_guard_ =
            guard_mgr.acquireGuard(OperationGuard::OperationType::OTA_OPERATION, "ArduinoOTA");

        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
        LOGI(TAG, "OTA Update Started: %s", type.c_str());

        // Call user callback
        if (on_ota_start_) {
            on_ota_start_();
        }
    });

    ArduinoOTA.onEnd([this]() {
        Preferences local_prefs;
        local_prefs.begin("openlux", false);
        local_prefs.putString("reboot_reason", "OTA");
        local_prefs.end();
        LOGI(TAG, "OTA Update Finished");
        ota_guard_.release(); // Release the OTA guard

        // Call user callback
        if (on_ota_end_) {
            on_ota_end_();
        }
    });

    ArduinoOTA.onProgress([this](unsigned int progress, unsigned int total) {
        static uint32_t last_log = 0;
        const uint32_t now = millis();
        uint32_t percent = (progress * 100) / total;

        // Log every 10%
        if (now - last_log > 1000 || percent == 100) {
            LOGI(TAG, "OTA Progress: %u%%", percent);
            last_log = now;
        }

        if (on_ota_progress_) {
            on_ota_progress_(progress, total);
        }
    });

    ArduinoOTA.onError([this](ota_error_t error) {
        LOGE(TAG, "OTA Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR)
            LOGE(TAG, "Auth Failed");
        else if (error == OTA_BEGIN_ERROR)
            LOGE(TAG, "Begin Failed");
        else if (error == OTA_CONNECT_ERROR)
            LOGE(TAG, "Connect Failed");
        else if (error == OTA_RECEIVE_ERROR)
            LOGE(TAG, "Receive Failed");
        else if (error == OTA_END_ERROR)
            LOGE(TAG, "End Failed");

        ota_guard_.release(); // Release the OTA guard on error

        // Call user callback
        if (on_ota_error_) {
            on_ota_error_();
        }
    });

    ArduinoOTA.begin();
    ota_enabled_ = true;

    LOGI(TAG, "OTA Ready");
}

void NetworkManager::setupMDNS(const char* hostname) {
    if (!MDNS.begin(hostname)) {
        LOGE(TAG, "mDNS failed to start");
        return;
    }

    MDNS.addService("http", "tcp", 80);
    MDNS.addService("telnet", "tcp", 23);

    LOGI(TAG, "mDNS started: %s.local", hostname);
}

#if !OPENLUX_USE_ETHERNET
void NetworkManager::softReconnect() {
    if (isScanning()) {
        LOGW(TAG, "softReconnect ignored: scan in progress");
        return;
    }
    LOGI(TAG, "WiFi soft reconnect");
    WiFi.disconnect(false);
    vTaskDelay(pdMS_TO_TICKS(200));
    WiFi.reconnect(); // uses stored credentials
}

void NetworkManager::restartInterface() {
    if (isScanning()) {
        LOGW(TAG, "restartInterface ignored: scan in progress");
        return;
    }
    LOGI(TAG, "WiFi interface restart (STA)");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    vTaskDelay(pdMS_TO_TICKS(200));
    WiFi.mode(WIFI_STA);
    WiFi.begin(); // uses stored credentials
}
#else
void NetworkManager::softReconnect() {
    LOGW(TAG, "softReconnect ignored: Ethernet mode");
}

void NetworkManager::restartInterface() {
    LOGW(TAG, "restartInterface ignored: Ethernet mode");
}
#endif

void NetworkManager::rebootDevice(const char* reason) {
    SystemManager::getInstance().reboot(reason);
}

#if OPENLUX_USE_ETHERNET
void NetworkManager::clearCredentials() {
    LOGW(TAG, "clearCredentials skipped: Ethernet mode");
}
#else
void NetworkManager::clearCredentials() {
    LOGW(TAG, "Clearing stored WiFi credentials (NVS)");
    WiFi.disconnect(true, true); // erase persistent credentials
}
#endif

void NetworkManager::markBootSuccessful() {
    if (!boot_failures_loaded_)
        return;
    if (boot_failures_ != 0) {
        boot_failures_ = 0;
        prefs_.begin("openlux", false);
        prefs_.putUChar("boot_fail", 0);
        prefs_.end();
        LOGI(TAG, "Boot marked successful, counter reset");
    }
}

#if !OPENLUX_USE_ETHERNET
bool NetworkManager::startProvisioningPortal() {
    LOGI(TAG, "Starting provisioning portal (AP)");
    portal_opened_once_ = true;

    // Disable watchdog during blocking portal
    SystemManager::getInstance().disableWatchdog();

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, true);

    WiFiManager wm;
    wm.setDebugOutput(false);
    wm.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT_S);
    wm.setHostname(hostname_ ? hostname_ : WIFI_HOSTNAME);

    if (use_static_ip_) {
        LOGI(TAG, "Using static IP for portal STA: %s", static_ip_.toString().c_str());
        wm.setSTAStaticIPConfig(static_ip_, gateway_, subnet_, dns1_);
    }

    bool res = wm.startConfigPortal(WIFI_PORTAL_SSID, WIFI_PORTAL_PASS);
    if (res) {
        LOGI(TAG, "✓ WiFi configured via portal");
        LOGI(TAG, "  IP: %s", WiFi.localIP().toString().c_str());
    } else {
        LOGE(TAG, "WiFi portal timeout or failed connection");
    }

    // Re-enable watchdog
    SystemManager::getInstance().enableWatchdog();

    return res;
}
#else
bool NetworkManager::startProvisioningPortal() {
    LOGW(TAG, "Provisioning portal skipped: Ethernet mode");
    return false;
}
#endif

int NetworkManager::scanAndFindBestAP(int& bestRSSI) {
    // NOTE: Assumes ScanGuard is already held by caller

    // Scan for networks (synchronous scan)
    int n = WiFi.scanNetworks();
    int bestNetworkIndex = -1;
    bestRSSI = -1000;

    if (n == 0) {
        LOGW(TAG, "No networks found during scan");
    } else {
        LOGD(TAG, "Scan done, %d networks found", n);
        for (int i = 0; i < n; ++i) {
            String currentSSID = WiFi.SSID(i);
            // Check if SSID matches (case sensitive)
            if (currentSSID.equals(ssid_)) {
                int rssi = WiFi.RSSI(i);
                LOGD(TAG, "  Found AP: %s, RSSI: %d, BSSID: %s, Channel: %d", currentSSID.c_str(),
                     rssi, WiFi.BSSIDstr(i).c_str(), WiFi.channel(i));

                if (rssi > bestRSSI) {
                    bestRSSI = rssi;
                    bestNetworkIndex = i;
                }
            }
        }

        if (bestNetworkIndex == -1) {
            LOGW(TAG, "Preferred AP not found in scan results");
        } else {
            LOGI(TAG, "Best AP found: %s, RSSI: %d", WiFi.SSID(bestNetworkIndex).c_str(), bestRSSI);
        }
    }

    return bestNetworkIndex;
}

void NetworkManager::forceScanAndConnect() {
    LOGI(TAG, "Forcing WiFi scan and connect...");
    connectWiFi(true);
}

void NetworkManager::connectWiFi(bool force_scan) {
#if OPENLUX_USE_ETHERNET
    return;
#else
    auto& guard_mgr = OperationGuardManager::getInstance();
    auto guard = guard_mgr.acquireGuard(OperationGuard::OperationType::WIFI_SCAN, "connectWiFi");
    if (!guard) {
        LOGW(TAG, "Cannot scan: another operation is in progress");
        return;
    }

    bool should_scan = true;
#if defined(WIFI_FAST_CONNECT) && (WIFI_FAST_CONNECT == 1)
    should_scan = false;
#endif
    if (force_scan)
        should_scan = true;

    if (!should_scan) {
        LOGI(TAG, "Fast Connect enabled: skipping scan");
        WiFi.begin(ssid_, password_);
        last_connect_attempt_ = millis();
    } else {
        LOGI(TAG, "Scanning for best AP for SSID: %s", ssid_);

        int bestRSSI;
        int bestNetworkIndex = scanAndFindBestAP(bestRSSI);

        if (bestNetworkIndex >= 0) {
            uint8_t* bssid = WiFi.BSSID(bestNetworkIndex);
            int32_t channel = WiFi.channel(bestNetworkIndex);
            LOGI(TAG, "Connecting to best AP: %s (RSSI: %d, Channel: %d)",
                 WiFi.BSSIDstr(bestNetworkIndex).c_str(), bestRSSI, channel);

            // Connect using specific BSSID and channel for faster and more reliable connection
            WiFi.begin(ssid_, password_, channel, bssid);
        } else {
            LOGW(TAG,
                 "Target SSID not found in scan or scan failed, using default connection method");
            WiFi.begin(ssid_, password_);
        }

        WiFi.scanDelete();
        ;
        last_connect_attempt_ = millis();
    }
#endif
}

bool NetworkManager::validateConnection() {
    LOGI(TAG, "Starting validation connection check...");
#if OPENLUX_USE_ETHERNET
    if (!eth_connected_)
        return false;
    IPAddress gateway = ETH.gatewayIP();
#else
    if (WiFi.status() != WL_CONNECTED)
        return false;
    IPAddress gateway = WiFi.gatewayIP();
#endif

    if (gateway == IPAddress(0, 0, 0, 0)) {
        LOGW(TAG, "Gateway IP is 0.0.0.0");
        return true;
    }

    // Check if TCP operations are in progress - skip validation if they are
    auto& guard_mgr = OperationGuardManager::getInstance();
    if (!guard_mgr.canPerformOperation(OperationGuard::OperationType::NETWORK_VALIDATION)) {
        LOGD(TAG, "Skipping connection validation: blocking operation in progress");
        return gateway_reachable_; // Return last known state
    }

    {
        auto guard = guard_mgr.acquireGuard(OperationGuard::OperationType::NETWORK_VALIDATION);

        {
            WiFiClient client;
            client.setNoDelay(true);
            client.setTimeout(100); // Reduced timeout
            if (client.connect(gateway, 53, 100)) {
                client.stop();
                LOGD(TAG, "Connection validated via Gateway:53");
                return true;
            }
        }

#if defined(MQTT_HOST)
        {
            WiFiClient client;
            client.setNoDelay(true);
            client.setTimeout(50); // Reduced timeout
            if (client.connect(MQTT_HOST, MQTT_PORT, 50)) {
                client.stop();
                LOGD(TAG, "Connection validated via MQTT broker");
                return true;
            }
            LOGW(TAG, "Failed to connect to gateway %s (port 53) and MQTT %s:%d",
                 gateway.toString().c_str(), MQTT_HOST, MQTT_PORT);
        }
#else
        {
            WiFiClient client;
            client.setNoDelay(true);
            client.setTimeout(50); // Reduced timeout
            if (client.connect(gateway, 80, 50)) {
                client.stop();
                LOGD(TAG, "Connection validated via Gateway:80");
                return true;
            }
            LOGW(TAG, "Failed to connect to gateway %s (ports 53, 80)", gateway.toString().c_str());
        }
#endif
    }

    return false;
}

bool NetworkManager::isConnected() {
    if (isScanning()) {
        return was_connected_;
    }

    bool link_up = false;
#if OPENLUX_USE_ETHERNET
    link_up = eth_connected_ && ETH.linkUp();
#else
    link_up = (WiFi.status() == WL_CONNECTED);
#endif

    if (!link_up) {
        // If physical link is down, we are not connected.
        // We reset the gateway reachable flag so that when link comes back,
        // we assume it's good until proven otherwise (or until next check).
        gateway_reachable_ = true;
        return false;
    }

    const uint32_t now = millis();
    uint32_t validation_interval = VALIDATION_INTERVAL_MS;
    if (!gateway_reachable_) {
        validation_interval = VALIDATION_INTERVAL_MS * 3;
    }

    if (now - last_validation_ms_ > validation_interval) {
        last_validation_ms_ = now;
        const bool reachable = validateConnection();
        if (reachable != gateway_reachable_) {
            if (!reachable) {
                LOGW(TAG, "Active connection check failed! Gateway unreachable.");
                auto& sys = SystemManager::getInstance();
            } else {
                LOGI(TAG, "Active connection check passed (recovered).");
            }
            gateway_reachable_ = reachable;
        } else if (reachable) {
            LOGD(TAG, "Active connection check passed.");
        }
    }

    return gateway_reachable_;
}

void NetworkManager::checkConnection() {
    if (isScanning()) {
        return;
    }

    bool connected = isConnected();
    bool has_credentials = (ssid_ && strlen(ssid_) > 0);

    // Connection state changed
    if (connected != was_connected_) {
        if (connected) {
            LOGI(TAG, "%s Connected!", use_ethernet_ ? "ETH" : "WiFi");
#if OPENLUX_USE_ETHERNET
            LOGI(TAG, "  NET=ETH IP=%s", ETH.localIP().toString().c_str());
            LOGI(TAG, "  Gateway: %s", ETH.gatewayIP().toString().c_str());
            LOGI(TAG, "  DNS: %s", ETH.dnsIP().toString().c_str());
            LOGI(TAG, "  MAC: %s", ETH.macAddress().c_str());
#else
            LOGI(TAG, "  NET=WIFI IP=%s", WiFi.localIP().toString().c_str());
            LOGI(TAG, "  Gateway: %s", WiFi.gatewayIP().toString().c_str());
            LOGI(TAG, "  DNS: %s", WiFi.dnsIP().toString().c_str());
            LOGI(TAG, "  RSSI: %d dBm", WiFi.RSSI());
            LOGI(TAG, "  MAC: %s", WiFi.macAddress().c_str());
#endif
            markBootSuccessful();
            if (on_connected_)
                on_connected_();
        } else {
            LOGW(TAG, "Network Disconnected");
            if (on_disconnected_)
                on_disconnected_();
        }
        was_connected_ = connected;
    }

#if OPENLUX_USE_ETHERNET
    return;
#endif

    if (!connected && (millis() - last_connect_attempt_ > CONNECT_RETRY_DELAY)) {
        LOGW(TAG, "Attempting to reconnect...");
        connectWiFi();
    }

    const uint32_t now_status = millis();
    if (connected && (now_status - last_status_log_ > STATUS_LOG_INTERVAL)) {
        LOGD(TAG, "WiFi Status: IP=%s, RSSI=%d dBm", WiFi.localIP().toString().c_str(),
             WiFi.RSSI());
        last_status_log_ = now_status;
    }

    if (connected) {
        roamingIfNeeded();
    }

    // Connectivity watchdog: escalate reconnect → restart interface → reboot
    bool can_recover = has_credentials || WiFi.SSID().length() > 0;
    if (!connected) {
        if (disconnected_since_ == 0) {
            disconnected_since_ = millis();
            watchdog_reconnect_done_ = false;
            watchdog_restart_done_ = false;
        }

        uint32_t down_ms = millis() - disconnected_since_;

        if (can_recover && !watchdog_reconnect_done_ &&
            down_ms >= WIFI_WATCHDOG_RECONNECT_DELAY_MS) {
            LOGW(TAG, "WiFi watchdog: reconnect after %lu ms of downtime", down_ms);
            softReconnect();
            watchdog_reconnect_done_ = true;
        }

        if (can_recover && !watchdog_restart_done_ && down_ms >= WIFI_WATCHDOG_RESTART_DELAY_MS) {
            LOGW(TAG, "WiFi watchdog: restart interface after %lu ms of downtime", down_ms);
            restartInterface();
            watchdog_restart_done_ = true;
        }

        if (can_recover && !portal_opened_once_ && down_ms >= WIFI_WATCHDOG_PORTAL_DELAY_MS &&
            down_ms < WIFI_WATCHDOG_REBOOT_DELAY_MS) {
            LOGW(TAG, "WiFi watchdog: opening provisioning portal after %lu ms downtime", down_ms);
            startProvisioningPortal();
        }

        if (can_recover && down_ms >= WIFI_WATCHDOG_REBOOT_DELAY_MS) {
            LOGE(TAG, "WiFi watchdog: rebooting after prolonged disconnect (%lu ms)", down_ms);
            rebootDevice("WiFi watchdog");
        }
    } else {
        // Reset watchdog state on successful connection
        disconnected_since_ = 0;
        watchdog_reconnect_done_ = false;
        watchdog_restart_done_ = false;
        portal_opened_once_ = false;
    }
}

void NetworkManager::roamingIfNeeded() {
#if WIFI_ROAMING_ENABLED
    const uint32_t now_scan = millis();
    const bool interval_elapsed = (now_scan - last_scan_ms_) > WIFI_ROAMING_INTERVAL_MS;
    const int currentRSSI = getRSSI();

    if (interval_elapsed && currentRSSI <= WIFI_ROAMING_RSSI_THRESHOLD_DBM) {
        LOGW(TAG, "WiFi roaming: RSSI %d dBm <= threshold %d dBm, scanning for better AP...",
             currentRSSI, WIFI_ROAMING_RSSI_THRESHOLD_DBM);
        last_scan_ms_ = now_scan;

        auto& guard_mgr = OperationGuardManager::getInstance();
        const auto guard =
            guard_mgr.acquireGuard(OperationGuard::OperationType::WIFI_SCAN, "roaming");
        if (!guard) {
            LOGW(TAG, "Skipping roaming scan: blocking operation in progress");
            return;
        }

        int bestRSSI;
        int bestNetworkIndex = scanAndFindBestAP(bestRSSI);

        if (bestNetworkIndex >= 0) {
            String currentBSSID = WiFi.BSSIDstr();
            if (bestRSSI > currentRSSI) {
                String newBSSID = WiFi.BSSIDstr(bestNetworkIndex);
                if (!newBSSID.equalsIgnoreCase(currentBSSID)) {
                    LOGW(TAG, "Roaming to AP %s (%d dBm) from %s (%d dBm)", newBSSID.c_str(),
                         bestRSSI, currentBSSID.c_str(), currentRSSI);

                    uint8_t* bssid = WiFi.BSSID(bestNetworkIndex);
                    int32_t channel = WiFi.channel(bestNetworkIndex);

                    WiFi.disconnect();
                    vTaskDelay(pdMS_TO_TICKS(100));
                    WiFi.begin(ssid_, password_, channel, bssid);
                    was_connected_ = false;
                } else {
                    LOGW(TAG, "Already connected to strongest AP (%d dBm)", currentRSSI);
                }
            } else {
                LOGW(TAG, "No AP stronger than current RSSI %d dBm", currentRSSI);
            }
        } else {
            LOGW(TAG, "WiFi roaming scan failed or configured SSID not found");
        }
        WiFi.scanDelete();
    }
#endif
}

void NetworkManager::handleOTA() {
    ArduinoOTA.handle();
}

#if OPENLUX_USE_ETHERNET
void NetworkManager::handleEthernetEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    (void) info; // Unused parameter
    switch (event) {
        case ARDUINO_EVENT_ETH_START:
            ETH.setHostname(hostname_ ? hostname_ : WIFI_HOSTNAME);
            LOGI(TAG, "ETH start");
            break;
        case ARDUINO_EVENT_ETH_CONNECTED:
            LOGI(TAG, "ETH link up");
            break;
        case ARDUINO_EVENT_ETH_GOT_IP:
            eth_connected_ = true;
            LOGI(TAG, "ETH got IP: %s", ETH.localIP().toString().c_str());
            LOGI(TAG, "  Gateway: %s", ETH.gatewayIP().toString().c_str());
            LOGI(TAG, "  DNS: %s", ETH.dnsIP().toString().c_str());
            LOGI(TAG, "  MAC: %s", ETH.macAddress().c_str());
            markBootSuccessful();
            if (on_connected_)
                on_connected_();
            break;
        case ARDUINO_EVENT_ETH_DISCONNECTED:
        case ARDUINO_EVENT_ETH_STOP:
            if (eth_connected_ && on_disconnected_)
                on_disconnected_();
            eth_connected_ = false;
            LOGW(TAG, "ETH disconnected");
            break;
        default:
            break;
    }
}
#endif

#if !OPENLUX_USE_ETHERNET
void NetworkManager::handleWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_SCAN_DONE:
            logHeapStatus("scan_done");
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            logHeapStatus("sta_disconnected");
            LOGW(TAG, "WiFi disconnect reason=%d", info.wifi_sta_disconnected.reason);
            gateway_reachable_ = false;
            break;
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            logHeapStatus("sta_connected");
            break;
        default:
            break;
    }
}
#endif

void NetworkManager::logHeapStatus(const char* context) {
    auto& sys = SystemManager::getInstance();
    const uint32_t free_heap = sys.getFreeHeap();
    const uint32_t min_heap = sys.getMinFreeHeap();
    const uint32_t max_alloc = sys.getMaxAllocHeap();
    const bool ok = heap_caps_check_integrity_all(true);
    LOGD(TAG, "heap(%s): free=%u min=%u max_alloc=%u integrity=%s", context, free_heap, min_heap,
         max_alloc, ok ? "OK" : "FAIL");
}
