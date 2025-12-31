/**
 * @file protocol_bridge.h
 * @brief Protocol bridge between WiFi and RS485
 *
 * @license GPL-3.0
 * @author OpenLux Contributors
 */
#pragma once

#include "operation_guard.h"
#include "rs485_manager.h"
#include "tcp_protocol.h"
#include "tcp_server.h"

#include <Arduino.h>

#include <map>

// ============================================================================
// Fallback Cache - Safety Net for RS485 Failures
// ============================================================================

/**
 * @brief Key for fallback cache lookup
 *
 * Uniquely identifies a read request by function code, start register, and register count
 */
struct ReadCacheKey {
    uint8_t function_code;
    uint16_t start_register;
    uint16_t register_count;

    bool operator<(const ReadCacheKey& other) const {
        if (function_code != other.function_code)
            return function_code < other.function_code;
        if (start_register != other.start_register)
            return start_register < other.start_register;
        return register_count < other.register_count;
    }

    bool operator==(const ReadCacheKey& other) const {
        return function_code == other.function_code && start_register == other.start_register &&
               register_count == other.register_count;
    }

    // Format key as string for logging
    String format() const {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "func=0x%02X start=%d count=%d", function_code,
                 start_register, register_count);
        return String(buffer);
    }
};

/**
 * @brief Entry stored in the fallback cache
 *
 * Contains the last successful response for a specific read request
 * Used when RS485 fails (timeout, error, collision)
 */
struct ReadCacheEntry {
    ReadCacheKey key;
    std::vector<uint8_t> tcp_response_packet; // WiFi response packet (A1 1A format)
    uint32_t timestamp_ms;                    // Timestamp when entry was cached
    uint8_t hit_count;                        // Number of times used as fallback
    uint32_t last_access_ms;                  // Timestamp of last access (for LRU)

    ReadCacheEntry() : timestamp_ms(0), hit_count(0), last_access_ms(0) {}

    // Check if entry has expired
    bool is_stale(uint32_t now_ms, uint32_t ttl_ms) const {
        return (now_ms - timestamp_ms) > ttl_ms;
    }

    // Get age of entry in milliseconds
    uint32_t get_age(uint32_t now_ms) const { return now_ms - timestamp_ms; }

    // Update last access time for LRU tracking
    void update_access_time() { last_access_ms = millis(); }

    // Increment hit counter
    void increment_hit_count() { hit_count++; }

    // Check if this entry is older (for LRU comparison)
    bool is_older_than(const ReadCacheEntry& other) const {
        return timestamp_ms < other.timestamp_ms;
    }
};

/**
 * @brief Protocol Bridge - Coordinator between TCP and RS485
 *
 * Handles the translation between:
 * - TCP Protocol (A1 1A format) from Home Assistant via TCP
 * - RS485 Protocol (Modbus-like) to/from Inverter
 */

struct BridgeRequest {
    TCPClient* client = nullptr;
    TcpParseResult wifi_request;
    uint32_t timestamp = 0;
    uint8_t retry_count = 0;

    BridgeRequest() = default;
};

class ProtocolBridge {
  public:
    static ProtocolBridge& getInstance();

    // Lifecycle
    void begin(const String& dongle_serial = "0000000000");
    void loop();

    // Configuration
    void set_tcp_server(TCPServer* server) { tcp_server_ = server; }
    void set_rs485_manager(RS485Manager* rs485) { rs485_ = rs485; }
    void set_dongle_serial(const String& serial) { dongle_serial_ = serial; }

    // Process incoming WiFi request from TCP
    void process_wifi_request(const uint8_t* data, size_t length, TCPClient* client);

    // Status
    bool is_ready() const { return tcp_server_ != nullptr && rs485_ != nullptr; }
    bool is_paused() const { return paused_; }
    void set_pause(bool paused) { paused_ = paused; }
    uint32_t get_total_requests() const { return total_requests_; }
    uint32_t get_successful_requests() const { return successful_requests_; }
    uint32_t get_failed_requests() const { return failed_requests_; }

    // ========== Cache Status Methods ==========
    size_t get_cache_size() const { return fallback_cache_.size(); }
    uint32_t get_cache_hits() const { return cache_hits_; }
    uint32_t get_cache_misses() const { return cache_misses_; }
    uint32_t get_cache_invalidations() const { return cache_invalidations_; }
    float get_cache_hit_ratio() const {
        uint32_t hits = get_cache_hits();
        uint32_t misses = get_cache_misses();
        uint32_t total = hits + misses;
        return total > 0 ? (100.0f * hits / total) : 0.0f;
    }

    // ========== Cache Utility Methods ==========
    void clear_fallback_cache() { fallback_cache_.clear(); }
    void print_cache_entries(std::function<void(const String&)> callback) const;

  private:
    ProtocolBridge() = default;
    ~ProtocolBridge() = default;
    ProtocolBridge(const ProtocolBridge&) = delete;
    ProtocolBridge& operator=(const ProtocolBridge&) = delete;

    void process_rs485_response();
    static bool validate_response_match(const ParseResult& result, const TcpParseResult& request);
    void send_wifi_response(TCPClient* client, const ParseResult& rs485_result);
    void send_error_response(TCPClient* client, const String& error);

    // ========== Fallback Cache Methods ==========
    void cache_read_response(const TcpParseResult& request,
                             const std::vector<uint8_t>& tcp_response);
    void cache_response_for_fallback(const ReadCacheKey& key,
                                     const std::vector<uint8_t>& tcp_response);
    bool get_fallback_response(const ReadCacheKey& key, std::vector<uint8_t>& out_response);
    void send_response_to_client(const std::vector<uint8_t>& response, size_t response_size);
    void evict_oldest_cache_entry();

    // ========== RS485 Response Handling ==========
    void handle_rs485_success(const ParseResult& rs485_result, unsigned long elapsed);
    void handle_rs485_error(const ParseResult& rs485_result, unsigned long elapsed);
    void build_value_summary(const ParseResult& rs485_result, char* buffer, size_t buffer_size);
    bool try_fallback_cache_on_error(const ParseResult& rs485_result);

    TCPServer* tcp_server_ = nullptr;
    RS485Manager* rs485_ = nullptr;
    String dongle_serial_;

    BridgeRequest current_request_;
    bool waiting_rs485_response_ = false;
    uint32_t last_request_time_ = 0;
    bool paused_ = false;

    // ========== Fallback Cache ==========
    std::map<ReadCacheKey, ReadCacheEntry> fallback_cache_;
    static constexpr size_t MAX_CACHE_ENTRIES = 10;

    // Cache statistics
    uint32_t cache_hits_ = 0;
    uint32_t cache_misses_ = 0;
    uint32_t cache_invalidations_ = 0;

    // Statistics
    uint32_t total_requests_ = 0;
    uint32_t successful_requests_ = 0;
    uint32_t failed_requests_ = 0;

    static constexpr uint32_t REQUEST_TIMEOUT_MS = 2000;
};
