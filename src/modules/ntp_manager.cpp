/**
 * @file ntp_manager.cpp
 * @brief NTP time synchronization implementation
 *
 * @license GPL-3.0
 * @author OpenLux Contributors
 */
#include "ntp_manager.h"

#include "logger.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char* TAG = "ntp";

NTPManager& NTPManager::getInstance() {
    static NTPManager instance;
    return instance;
}

void NTPManager::begin(const char* ntp_server1, const char* ntp_server2, const char* ntp_server3) {
    ntp_server1_ = ntp_server1;
    ntp_server2_ = ntp_server2;
    ntp_server3_ = ntp_server3;

    LOGI(TAG, "Initializing NTP Time Sync");
    LOGI(TAG, "  Primary NTP: %s", ntp_server1_);
    LOGI(TAG, "  Secondary NTP: %s", ntp_server2_);
    LOGI(TAG, "  Tertiary NTP: %s", ntp_server3_);

    // Configure NTP with multiple servers for redundancy
    configTime(0, 0, ntp_server1_, ntp_server2_, ntp_server3_);

    // Set default timezone (CET/CEST - Italy/Rome)
    setTimezone(timezone_);

    // Wait for time sync
    if (waitForSync()) {
        time_synced_ = true;
        last_sync_millis_ = millis();
        LOGI(TAG, "Time synchronized successfully!");
        LOGI(TAG, "Current time: %s", getFormattedTime().c_str());
    } else {
        LOGW(TAG, "Failed to sync time (timeout)");
        LOGW(TAG, "Will retry in background");
    }
}

void NTPManager::setTimezone(const char* timezone) {
    timezone_ = timezone;
    setenv("TZ", timezone_, 1);
    tzset();
    LOGI(TAG, "Timezone set: %s", timezone_);
}

void NTPManager::loop() {
    // Periodic re-sync check
    if (!time_synced_ || (millis() - last_sync_millis_ > sync_interval_)) {
        updateSyncStatus();

        if (!time_synced_) {
            LOGD(TAG, "Attempting time re-sync...");
            if (waitForSync(5000)) {
                time_synced_ = true;
                last_sync_millis_ = millis();
                LOGI(TAG, "Time re-synchronized: %s", getFormattedTime().c_str());
            }
        } else {
            last_sync_millis_ = millis();
        }
    }
}

bool NTPManager::waitForSync(uint32_t timeout_ms) {
    unsigned long start = millis();
    time_t now = 0;

    while (millis() - start < timeout_ms) {
        time(&now);
        if (now > 1000000000) { // Valid timestamp (after year 2001)
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    return false;
}

void NTPManager::updateSyncStatus() {
    time_t now;
    time(&now);
    time_synced_ = (now > 1000000000);
}

time_t NTPManager::getEpochTime() const {
    time_t now;
    time(&now);
    return now;
}

String NTPManager::getFormattedTime(const char* format) const {
    if (!time_synced_) {
        return "Time not synced";
    }

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    char buffer[80];
    strftime(buffer, sizeof(buffer), format, &timeinfo);
    return String(buffer);
}

String NTPManager::getISOTime() const {
    return getFormattedTime("%Y-%m-%dT%H:%M:%S%z");
}

void NTPManager::forceSync() {
    LOGI(TAG, "Forcing time synchronization...");

    // Reconfigure NTP
    configTime(0, 0, ntp_server1_, ntp_server2_, ntp_server3_);

    if (waitForSync()) {
        time_synced_ = true;
        last_sync_millis_ = millis();
        LOGI(TAG, "Time synchronized: %s", getFormattedTime().c_str());
    } else {
        LOGW(TAG, "Time sync failed");
    }
}

unsigned long NTPManager::getTimeSinceSync() const {
    if (!time_synced_) {
        return 0;
    }
    return millis() - last_sync_millis_;
}
