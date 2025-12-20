/**
 * @file system_manager.h
 * @brief System abstraction layer for hardware operations (reboot, heap, etc.)
 *
 * @license GPL-3.0
 * @author OpenLux Contributors
 */
#pragma once

#include <Arduino.h>

#include <Preferences.h>

class SystemManager {
  public:
    static SystemManager& getInstance();

    // Lifecycle
    void begin();
    void loop(); // Health checks

    // Watchdog
    void enableWatchdog();
    void disableWatchdog();
    void feedWatchdog();

    // System operations
    void reboot(const char* reason);
    String getLastRebootReason() const { return last_reboot_reason_; }

    // Diagnostics
    uint32_t getFreeHeap() const;
    uint32_t getMinFreeHeap() const;
    uint32_t getMaxAllocHeap() const;
    uint32_t getPsramSize() const;
    uint32_t getFreePsram() const;
    uint32_t getCpuFreqMHz() const;
    uint32_t getFlashChipSize() const;
    const char* getSdkVersion() const;
    uint8_t getChipRevision() const;
    const char* getChipModel() const;
    uint8_t getChipCores() const;
    uint32_t getUptime() const; // in seconds

  private:
    SystemManager() = default;
    ~SystemManager() = default;
    SystemManager(const SystemManager&) = delete;
    SystemManager& operator=(const SystemManager&) = delete;

    Preferences prefs_;
    String last_reboot_reason_ = "Unknown";

    // Health monitoring
    uint32_t last_heap_check_ = 0;
    uint32_t low_heap_start_time_ = 0;
    static const uint32_t HEAP_CHECK_INTERVAL = 5000; // 5 seconds
    static const uint32_t MIN_SAFE_HEAP = 10240;      // 10 KB
    static const uint32_t LOW_HEAP_TIMEOUT = 60000;   // 1 minute under threshold -> reboot
};
