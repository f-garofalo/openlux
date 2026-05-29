/**
 * @file web_server.h
 * @brief Web dashboard and API server
 *
 * @license GPL-3.0
 * @author OpenLux Contributors
 */
#pragma once

#include "../config.h"

#ifdef ENABLE_WEB_DASH

#include <Arduino.h>

#include <WebServer.h>

class WebServerManager {
  public:
    static WebServerManager& getInstance();

    void begin();
    void loop();
    uint32_t getStatusRequestCount() const { return status_request_count_; }
    uint32_t getStatusCacheHitCount() const { return status_cache_hits_; }
    uint32_t getStatusSlowCount() const { return status_slow_count_; }
    uint32_t getLastStatusBuildMs() const { return last_status_build_ms_; }
    uint32_t getLastStatusTotalMs() const { return last_status_total_ms_; }
    uint32_t getStatusCacheTtlMs() const { return STATUS_CACHE_TTL_MS; }

  private:
    WebServerManager();
    ~WebServerManager() = default;
    WebServerManager(const WebServerManager&) = delete;
    WebServerManager& operator=(const WebServerManager&) = delete;

    void registerRoutes();
    bool requireAuth();
    void handleStatus();
    void handleCommand();
    void serveRoot();
    String buildStatusJson(uint16_t& http_status);

    WebServer server_;
    String cached_status_json_;
    uint32_t cached_status_ms_ = 0;
    uint32_t status_request_count_ = 0;
    uint32_t status_cache_hits_ = 0;
    uint32_t status_slow_count_ = 0;
    uint32_t last_status_build_ms_ = 0;
    uint32_t last_status_total_ms_ = 0;

    static constexpr uint32_t STATUS_CACHE_TTL_MS = 1500;
    static constexpr uint32_t STATUS_SLOW_THRESHOLD_MS = 500;
};

#endif // ENABLE_WEB_DASH
