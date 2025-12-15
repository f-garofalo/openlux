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
#include "system_manager.h"

#include <Esp.h>

static const char* TAG = "net";

NetworkManager& NetworkManager::getInstance() {
    static NetworkManager instance;
    return instance;
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

    use_ethernet_ = (OPENLUX_USE_ETHERNET != 0);

    ssid_ = ssid;
    password_ = password;
    hostname_ = hostname;

    LOGI(TAG, "Initializing Network Manager (%s)", use_ethernet_ ? "Ethernet" : "WiFi");
    LOGI(TAG, "  Hostname: %s", hostname_);
    LOGI(TAG, "  NET mode: %s", use_ethernet_ ? "ETH" : "WIFI");

#if OPENLUX_USE_ETHERNET
    if (use_ethernet_) {
        ETH.onEvent(
            [this](WiFiEvent_t event, WiFiEventInfo_t info) {
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
            },
            ARDUINO_EVENT_MAX);

        if (!ETH.begin(ETH_PHY_ADDR, ETH_PHY_POWER_PIN, ETH_PHY_MDC_PIN, ETH_PHY_MDIO_PIN,
                       ETH_PHY_TYPE, ETH_PHY_CLK_MODE)) {
            LOGE(TAG, "ETH begin failed");
        } else {
            LOGI(TAG, "ETH init requested");
        }
        return;
    }
#endif

#if !OPENLUX_USE_ETHERNET
    LOGI(TAG, "  SSID: %s", ssid_);

    // Set hostname early
    if (hostname_) {
        WiFi.setHostname(hostname_);
    }
    WiFi.mode(WIFI_STA);

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
#endif
}

void NetworkManager::loop() {
    checkConnection();

    if (ota_enabled_ && !ota_in_progress_) {
        handleOTA();
    }
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
        ota_in_progress_ = true;
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
        LOGI(TAG, "OTA Update Started: %s", type.c_str());
    });

    ArduinoOTA.onEnd([this]() {
        LOGI(TAG, "OTA Update Finished");
        ota_in_progress_ = false;
    });

    ArduinoOTA.onProgress([this](unsigned int progress, unsigned int total) {
        static uint32_t last_log = 0;
        uint32_t percent = (progress * 100) / total;

        // Log every 10%
        if (millis() - last_log > 1000 || percent == 100) {
            LOGI(TAG, "OTA Progress: %u%%", percent);
            last_log = millis();
        }

        if (on_ota_progress_) {
            on_ota_progress_(progress, total);
        }
    });

    ArduinoOTA.onError([](ota_error_t error) {
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
    LOGI(TAG, "WiFi soft reconnect");
    WiFi.disconnect(false);
    delay(200);
    WiFi.reconnect(); // uses stored credentials
}

void NetworkManager::restartInterface() {
    LOGI(TAG, "WiFi interface restart (STA)");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(200);
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
        prefs_.putUChar("boot_fail", 0);
        LOGI(TAG, "Boot marked successful, counter reset");
    }
}

#if !OPENLUX_USE_ETHERNET
bool NetworkManager::startProvisioningPortal() {
    LOGI(TAG, "Starting provisioning portal (AP)");
    portal_opened_once_ = true;

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
    return res;
}
#else
bool NetworkManager::startProvisioningPortal() {
    LOGW(TAG, "Provisioning portal skipped: Ethernet mode");
    return false;
}
#endif

void NetworkManager::connectWiFi() {
#if OPENLUX_USE_ETHERNET
    return;
#else
    LOGI(TAG, "Connecting to WiFi...");
    WiFi.begin(ssid_, password_);
    last_connect_attempt_ = millis();
#endif
}

void NetworkManager::checkConnection() {
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

            if (on_connected_) {
                on_connected_();
            }
        } else {
            LOGW(TAG, "Network Disconnected");

            if (on_disconnected_) {
                on_disconnected_();
            }
        }

        was_connected_ = connected;
    }

    // Retry connection if disconnected (WiFi only)
#if !OPENLUX_USE_ETHERNET
    if (!connected && (millis() - last_connect_attempt_ > CONNECT_RETRY_DELAY)) {
        LOGW(TAG, "Attempting to reconnect...");
        connectWiFi();
    }

    // Periodic status log
    if (connected && (millis() - last_status_log_ > STATUS_LOG_INTERVAL)) {
        LOGD(TAG, "WiFi Status: IP=%s, RSSI=%d dBm", WiFi.localIP().toString().c_str(),
             WiFi.RSSI());
        last_status_log_ = millis();
    }
#else
    // Ethernet: nothing more to do
    return;
#endif

    // Connectivity watchdog: escalate reconnect → restart interface → reboot
#if !OPENLUX_USE_ETHERNET
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
#endif
}

void NetworkManager::handleOTA() {
    ArduinoOTA.handle();
}
