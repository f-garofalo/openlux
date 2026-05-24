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
#if !OPENLUX_USE_ETHERNET
#include <esp_wifi.h>
#endif
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
    const bool has_ssid = (ssid_ && strlen(ssid_) > 0);

    WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) { handleWiFiEvent(event, info); });
    WiFi.persistent(!has_ssid);
    LOGI(TAG, "  WiFi credentials source: %s",
         has_ssid ? "firmware config (persistent storage disabled)" : "provisioning portal");

    // Set hostname early
    if (hostname_) {
        WiFi.setHostname(hostname_);
    }
    WiFi.mode(WIFI_STA);
    configureWiFiPowerSave();
    applyWiFiTxPower("initial STA setup");


    // Provisioning path: if no SSID provided, start captive portal
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

void NetworkManager::configureWiFiPowerSave() {
#if !OPENLUX_USE_ETHERNET
    const bool sleep_call_ok = WiFi.setSleep(false);
    const esp_err_t set_result = esp_wifi_set_ps(WIFI_PS_NONE);
    wifi_ps_type_t ps_type = WIFI_PS_MAX_MODEM;
    const esp_err_t get_result = esp_wifi_get_ps(&ps_type);
    wifi_power_save_disabled_ =
        (set_result == ESP_OK) && (get_result == ESP_OK) && (ps_type == WIFI_PS_NONE);

    if (wifi_power_save_disabled_) {
        LOGI(TAG, "WiFi power save disabled");
    } else {
        LOGW(TAG, "WiFi power save disable incomplete (sleep_call=%s set_err=%d get_err=%d ps=%d)",
             sleep_call_ok ? "ok" : "fail", set_result, get_result, ps_type);
    }
#else
    wifi_power_save_disabled_ = true;
#endif
}

void NetworkManager::applyWiFiTxPower(const char* context) {
#if !OPENLUX_USE_ETHERNET
#ifdef WIFI_TX_POWER
    const bool applied = WiFi.setTxPower(WIFI_TX_POWER);
    const int8_t raw_power = WiFi.getTxPower();
    const int whole = raw_power / 4;
    const int fraction = abs((raw_power % 4) * 25);

    if (applied) {
        LOGI(TAG, "WiFi TX power applied (%s): %d.%02d dBm", context, whole, fraction);
    } else {
        LOGW(TAG, "WiFi TX power apply failed (%s), current raw=%d", context, raw_power);
    }
#else
    (void) context;
#endif
#else
    (void) context;
#endif
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
    if (ota_enabled_) {
        LOGD(TAG, "OTA already configured");
        return;
    }

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
    if (mdns_started_) {
        LOGD(TAG, "mDNS already started");
        return;
    }

    if (!MDNS.begin(hostname)) {
        LOGE(TAG, "mDNS failed to start");
        return;
    }

    MDNS.addService("http", "tcp", 80);
    MDNS.addService("telnet", "tcp", 23);
    mdns_started_ = true;

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
    connectWiFi(false);
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
    configureWiFiPowerSave();
    applyWiFiTxPower("interface restart");
    connectWiFi(false);
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

    // Disable watchdog during blocking portal
    SystemManager::getInstance().disableWatchdog();

    WiFi.mode(WIFI_STA);
    configureWiFiPowerSave();
    applyWiFiTxPower("provisioning portal");
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
    configureWiFiPowerSave();

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
        applyWiFiTxPower("fast connect begin");
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
            applyWiFiTxPower("scanned connect begin");
        } else {
            LOGW(TAG,
                 "Target SSID not found in scan or scan failed, using default connection method");
            WiFi.begin(ssid_, password_);
            applyWiFiTxPower("fallback connect begin");
        }

        WiFi.scanDelete();
        ;
        last_connect_attempt_ = millis();
    }
#endif
}

const char* NetworkManager::getLastWiFiDisconnectReasonName() const {
    switch (last_wifi_disconnect_reason_) {
        case 0:
            return "none";
        case 1:
            return "UNSPECIFIED";
        case 2:
            return "AUTH_EXPIRE";
        case 3:
            return "AUTH_LEAVE";
        case 4:
            return "ASSOC_EXPIRE";
        case 5:
            return "ASSOC_TOOMANY";
        case 6:
            return "NOT_AUTHED";
        case 7:
            return "NOT_ASSOCED";
        case 8:
            return "ASSOC_LEAVE";
        case 9:
            return "ASSOC_NOT_AUTHED";
        case 14:
            return "MIC_FAILURE";
        case 15:
            return "4WAY_HANDSHAKE_TIMEOUT";
        case 16:
            return "GROUP_KEY_UPDATE_TIMEOUT";
        case 17:
            return "IE_IN_4WAY_DIFFERS";
        case 18:
            return "GROUP_CIPHER_INVALID";
        case 19:
            return "PAIRWISE_CIPHER_INVALID";
        case 20:
            return "AKMP_INVALID";
        case 21:
            return "UNSUPP_RSN_IE_VERSION";
        case 22:
            return "INVALID_RSN_IE_CAP";
        case 23:
            return "802_1X_AUTH_FAILED";
        case 24:
            return "CIPHER_SUITE_REJECTED";
        case 200:
            return "BEACON_TIMEOUT";
        case 201:
            return "NO_AP_FOUND";
        case 202:
            return "AUTH_FAIL";
        case 203:
            return "ASSOC_FAIL";
        case 204:
            return "HANDSHAKE_TIMEOUT";
        default:
            return "UNKNOWN";
    }
}

uint32_t NetworkManager::getLastWiFiDisconnectAgeMs() const {
    if (last_wifi_disconnect_ms_ == 0)
        return 0;
    return millis() - last_wifi_disconnect_ms_;
}

uint32_t NetworkManager::getLastWiFiConnectAgeMs() const {
    if (last_wifi_connect_ms_ == 0)
        return 0;
    return millis() - last_wifi_connect_ms_;
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

    return link_up;
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

    // Connectivity watchdog: escalate reconnect -> restart interface -> reboot.
    // Home Assistant silence is intentionally ignored; only WiFi link loss can trip this.
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

        if (can_recover && down_ms >= WIFI_WATCHDOG_REBOOT_DELAY_MS) {
            LOGE(TAG, "WiFi watchdog: rebooting after prolonged disconnect (%lu ms)", down_ms);
            rebootDevice("WiFi watchdog");
        }
    } else {
        // Reset watchdog state on successful connection
        disconnected_since_ = 0;
        watchdog_reconnect_done_ = false;
        watchdog_restart_done_ = false;
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
                    applyWiFiTxPower("roaming begin");
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
            wifi_disconnect_count_++;
            last_wifi_disconnect_ms_ = millis();
            last_wifi_disconnect_reason_ = info.wifi_sta_disconnected.reason;
            LOGW(TAG, "WiFi disconnect reason=%d", info.wifi_sta_disconnected.reason);
            if (mdns_started_) {
                MDNS.end();
                mdns_started_ = false;
            }
            switch (info.wifi_sta_disconnected.reason) {
                case 2:   // AUTH_EXPIRE
                case 6:   // NOT_AUTHED
                case 14:  // MIC_FAILURE
                case 15:  // 4WAY_HANDSHAKE_TIMEOUT
                case 16:  // GROUP_KEY_UPDATE_TIMEOUT
                case 202: // AUTH_FAIL
                case 203: // ASSOC_FAIL
                case 204: // HANDSHAKE_TIMEOUT
                    last_connect_attempt_ = millis();
                    break;
                default:
                    break;
            }
            break;
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            logHeapStatus("sta_connected");
            wifi_connect_count_++;
            last_wifi_connect_ms_ = millis();
            configureWiFiPowerSave();
            applyWiFiTxPower("sta connected");
            LOGI(TAG, "WiFi connected to AP %s on channel %u", WiFi.BSSIDstr().c_str(),
                 WiFi.channel());
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
