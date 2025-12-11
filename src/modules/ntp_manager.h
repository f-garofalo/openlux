/**
 * @file ntp_manager.h
 * @brief NTP time synchronization manager
 *
 * @license GPL-3.0
 * @author OpenLux Contributors
 */
#pragma once

#include <Arduino.h>

#include <ctime>

/**
 * @brief NTP Time Synchronization Manager
 *
 * Features:
 * - Automatic time sync via NTP
 * - Multiple NTP servers (with fallback)
 * - Timezone support
 * - Time validation
 * - Sync status monitoring
 */
class NTPManager {
  public:
    static NTPManager& getInstance();

    // Lifecycle
    void begin(const char* ntp_server1 = "pool.ntp.org",
               const char* ntp_server2 = "time.google.com",
               const char* ntp_server3 = "time.cloudflare.com");

    void setTimezone(const char* timezone);
    void loop(); // Call in main loop for periodic sync

    // Status
    bool isSynced() const { return time_synced_; }
    time_t getEpochTime() const;
    String getFormattedTime(const char* format = "%Y-%m-%d %H:%M:%S") const;
    String getISOTime() const;

    // Utilities
    void forceSync();
    unsigned long getLastSyncTime() const { return last_sync_millis_; }
    unsigned long getTimeSinceSync() const;

  private:
    NTPManager() = default;
    ~NTPManager() = default;
    NTPManager(const NTPManager&) = delete;
    NTPManager& operator=(const NTPManager&) = delete;

    bool waitForSync(uint32_t timeout_ms = 10000);
    void updateSyncStatus();

    bool time_synced_ = false;
    unsigned long last_sync_millis_ = 0;
    unsigned long sync_interval_ = 3600000; // Re-sync every hour

    // Default NTP servers
    const char* ntp_server1_ = "pool.ntp.org";
    const char* ntp_server2_ = "time.google.com";
    const char* ntp_server3_ = "time.cloudflare.com";

    // Default timezone (Rome/Italy)
    const char* timezone_ = "CET-1CEST,M3.5.0,M10.5.0/3";
};
