/**
 * @file system_manager.cpp
 * @brief System abstraction layer implementation
 *
 * @license GPL-3.0
 * @author OpenLux Contributors
 */
#include "system_manager.h"

#include "logger.h"

#include <Esp.h>

static const char* TAG = "sys";

SystemManager& SystemManager::getInstance() {
    static SystemManager instance;
    return instance;
}

void SystemManager::begin() {
    prefs_.begin("openlux_sys", false);

    // Read last reboot reason
    last_reboot_reason_ = prefs_.getString("reboot_reason", "Power On / Reset");

    // Clear it for next time
    prefs_.remove("reboot_reason");

    LOGI(TAG, "System initialized. Last reboot reason: %s", last_reboot_reason_.c_str());
}

void SystemManager::reboot(const char* reason) {
    if (reason && strlen(reason) > 0) {
        LOGE(TAG, "Rebooting system: %s", reason);
        prefs_.putString("reboot_reason", reason);
    } else {
        LOGE(TAG, "Rebooting system (unknown reason)");
        prefs_.putString("reboot_reason", "Unknown");
    }

    // Ensure logs are flushed
    delay(100);

    ESP.restart();
}

uint32_t SystemManager::getFreeHeap() const {
    return ESP.getFreeHeap();
}

uint32_t SystemManager::getMinFreeHeap() const {
    return ESP.getMinFreeHeap();
}

uint32_t SystemManager::getUptime() const {
    return millis() / 1000;
}
