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
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <soc/rtc_cntl_reg.h>

static const char* TAG = "sys";
static const int WDT_TIMEOUT = 30; // 30 seconds watchdog

// Helper to convert ESP32 reset reason to string
static const char* getResetReasonString(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_UNKNOWN:
            return "Unknown";
        case ESP_RST_POWERON:
            return "Power-on";
        case ESP_RST_EXT:
            return "External pin";
        case ESP_RST_SW:
            return "Software (esp_restart)";
        case ESP_RST_PANIC:
            return "Exception/Panic";
        case ESP_RST_INT_WDT:
            return "Interrupt Watchdog";
        case ESP_RST_TASK_WDT:
            return "Task Watchdog";
        case ESP_RST_WDT:
            return "Other Watchdog";
        case ESP_RST_DEEPSLEEP:
            return "Deep Sleep wake";
        case ESP_RST_BROWNOUT:
            return "Brownout";
        case ESP_RST_SDIO:
            return "SDIO";
        default:
            return "Unknown";
    }
}

SystemManager& SystemManager::getInstance() {
    static SystemManager instance;
    return instance;
}

void SystemManager::begin() {
    prefs_.begin("openlux", false);

    // Log ESP32 hardware reset reason
    esp_reset_reason_t reset_reason = esp_reset_reason();
    LOGI(TAG, "ESP32 Reset Reason: %s (code: %d)", getResetReasonString(reset_reason),
         reset_reason);

    // Warn on abnormal resets
    if (reset_reason == ESP_RST_PANIC) {
        LOGE(TAG, "⚠ Previous boot crashed with PANIC!");
    } else if (reset_reason == ESP_RST_TASK_WDT || reset_reason == ESP_RST_INT_WDT ||
               reset_reason == ESP_RST_WDT) {
        LOGE(TAG, "⚠ Previous boot had a WATCHDOG TIMEOUT!");
    } else if (reset_reason == ESP_RST_BROWNOUT) {
        LOGE(TAG, "⚠ Previous boot had a BROWNOUT (power issue)!");
    }

    // Read last reboot reason (our software reason)
    last_reboot_reason_ = prefs_.getString("reboot_reason", "Power On / Reset");

    // Clear it for next time
    prefs_.remove("reboot_reason");
    prefs_.end();

    LOGI(TAG, "System initialized. Last software reboot reason: %s", last_reboot_reason_.c_str());
}

void SystemManager::enableWatchdog() {
    // Initialize Task Watchdog Timer
    esp_task_wdt_init(WDT_TIMEOUT, true); // panic = true (reset on timeout)
    esp_task_wdt_add(nullptr);            // Add current thread (loopTask) to WDT
    LOGI(TAG, "Watchdog enabled (timeout: %ds)", WDT_TIMEOUT);
}

void SystemManager::disableWatchdog() {
    esp_task_wdt_delete(nullptr);
    // esp_task_wdt_deinit(); // Optional, but removing the task is enough to stop it watching us
    LOGI(TAG, "Watchdog disabled");
}

void SystemManager::feedWatchdog() {
    esp_task_wdt_reset();
}

void SystemManager::loop() {
    // Feed the watchdog
    feedWatchdog();

    // Check heap health periodically
    uint32_t now = millis();
    if (now - last_heap_check_ >= HEAP_CHECK_INTERVAL) {
        last_heap_check_ = now;
        uint32_t free_heap = getFreeHeap();

        if (free_heap < MIN_SAFE_HEAP) {
            if (low_heap_start_time_ == 0) {
                low_heap_start_time_ = now;
                LOGW(TAG, "Low memory detected: %u bytes (Threshold: %u)", free_heap,
                     MIN_SAFE_HEAP);
            } else if (now - low_heap_start_time_ >= LOW_HEAP_TIMEOUT) {
                LOGE(TAG, "Memory critically low for too long. Rebooting...");
                reboot("OOM Protection");
            }
        } else {
            if (low_heap_start_time_ != 0) {
                LOGI(TAG, "Memory recovered: %u bytes", free_heap);
                low_heap_start_time_ = 0;
            }
        }
    }
}

void SystemManager::reboot(const char* reason) {
    prefs_.begin("openlux", false);
    if (reason && strlen(reason) > 0) {
        LOGE(TAG, "Rebooting system: %s", reason);
        prefs_.putString("reboot_reason", reason);
    } else {
        LOGE(TAG, "Rebooting system (unknown reason)");
        prefs_.putString("reboot_reason", "Unknown");
    }
    prefs_.end();

    // Ensure logs are flushed
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP.restart();
}

uint32_t SystemManager::getFreeHeap() const {
    return ESP.getFreeHeap();
}

uint32_t SystemManager::getMinFreeHeap() const {
    return ESP.getMinFreeHeap();
}

uint32_t SystemManager::getMaxAllocHeap() const {
    return ESP.getMaxAllocHeap();
}

uint32_t SystemManager::getPsramSize() const {
#ifdef BOARD_HAS_PSRAM
    return ESP.getPsramSize();
#else
    return 0;
#endif
}

uint32_t SystemManager::getFreePsram() const {
#ifdef BOARD_HAS_PSRAM
    return ESP.getFreePsram();
#else
    return 0;
#endif
}

uint32_t SystemManager::getCpuFreqMHz() const {
    return ESP.getCpuFreqMHz();
}

uint32_t SystemManager::getFlashChipSize() const {
    return ESP.getFlashChipSize();
}

const char* SystemManager::getSdkVersion() const {
    return ESP.getSdkVersion();
}

uint8_t SystemManager::getChipRevision() const {
    return ESP.getChipRevision();
}

const char* SystemManager::getChipModel() const {
    return ESP.getChipModel();
}

uint8_t SystemManager::getChipCores() const {
    return ESP.getChipCores();
}

uint32_t SystemManager::getUptime() const {
    return millis() / 1000;
}
