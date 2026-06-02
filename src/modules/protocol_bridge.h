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

#include <array>
#include <functional>
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
    uint32_t hit_count;                       // Number of times used as fallback
    uint32_t last_access_ms;                  // Timestamp of last access (for LRU)

    ReadCacheEntry() : timestamp_ms(0), hit_count(0), last_access_ms(0) {}

    // Get age of entry in milliseconds
    uint32_t get_age(uint32_t now_ms) const { return now_ms - timestamp_ms; }

    // Update last access time for LRU tracking
    void update_access_time() { last_access_ms = millis(); }

    // Increment hit counter
    void increment_hit_count() { hit_count++; }

    // Check if this entry is older by last access time (LRU comparison)
    bool is_older_than(const ReadCacheEntry& other) const {
        return last_access_ms < other.last_access_ms;
    }
};

enum class BridgeWorkerState : uint8_t {
    IDLE = 0,
    QUEUED,
    RS485_SEND,
    RS485_RETRY,
    WAIT_RESPONSE,
    COEXISTENCE_BACKOFF,
    CACHE_FALLBACK,
    RESPOND_TCP,
    DONE,
    FAILED,
};

/**
 * @brief Protocol Bridge - Coordinator between TCP and RS485
 *
 * Handles the translation between:
 * - TCP Protocol (A1 1A format) from Home Assistant via TCP
 * - RS485 Protocol (Modbus-like) to/from Inverter
 */

struct BridgeRequest {
    // Stable handle: AsyncClient* is heap-allocated and persists until
    // TCPServer::destroy_client() is called. The TCPClient struct itself
    // lives in a std::vector that may move; resolve through
    // TCPServer::resolve_client() at the point of use instead of caching
    // a TCPClient*.
    AsyncClient* client_handle = nullptr;
    String client_ip; // Snapshot for logging, even if client goes away
    TcpParseResult wifi_request;
    uint32_t timestamp = 0;
    uint32_t id = 0;
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
    bool is_busy() const { return has_active_request_ || request_queue_count_ > 0; }
    void set_pause(bool paused);
    uint32_t get_total_requests() const { return total_requests_; }
    uint32_t get_successful_requests() const { return successful_requests_; }
    uint32_t get_failed_requests() const { return failed_requests_; }
    const char* get_worker_state_name() const { return worker_state_name(worker_state_); }
    size_t get_queue_size() const { return request_queue_count_; }
    size_t get_queue_capacity() const { return REQUEST_QUEUE_MAX_DEPTH; }
    uint32_t get_active_request_id() const { return has_active_request_ ? current_request_.id : 0; }
    uint32_t get_queued_requests() const { return queued_requests_; }
    uint32_t get_queue_drops() const { return queue_drops_; }
    uint32_t get_client_gone_count() const { return client_gone_count_; }
    uint32_t get_last_finished_request_id() const { return last_finished_request_id_; }
    const char* get_last_terminal_state_name() const {
        return worker_state_name(last_terminal_state_);
    }
    uint32_t get_last_finished_elapsed_ms() const { return last_finished_elapsed_ms_; }
    uint32_t get_coexistence_backoff_remaining_ms() const;
    uint32_t get_coexistence_pressure_remaining_ms() const;
    uint32_t get_coexistence_event_count() const { return coexistence_events_; }
    uint32_t get_coexistence_cache_hits() const { return coexistence_cache_hits_; }
    uint32_t get_coexistence_cache_stale_count() const { return coexistence_cache_stale_; }
    uint32_t get_coexistence_cache_miss_count() const { return coexistence_cache_misses_; }
    uint8_t get_consecutive_contention_events() const { return consecutive_contention_events_; }

    // ========== Cache Status Methods ==========
    size_t get_cache_size() const { return fallback_cache_.size(); }
    size_t get_cache_capacity() const { return MAX_CACHE_ENTRIES; }
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
    void clear_fallback_cache();
    void print_cache_entries(std::function<void(const String&)> callback) const;

  private:
    ProtocolBridge() = default;
    ~ProtocolBridge() = default;
    ProtocolBridge(const ProtocolBridge&) = delete;
    ProtocolBridge& operator=(const ProtocolBridge&) = delete;

    bool enqueue_request(BridgeRequest&& request);
    bool dequeue_request(BridgeRequest& request);
    bool queue_empty() const { return request_queue_count_ == 0; }
    bool queue_full() const { return request_queue_count_ >= REQUEST_QUEUE_MAX_DEPTH; }
    void drop_queued_requests(const char* reason);
    void start_next_request();
    void start_current_request();
    void process_rs485_response();
    void process_pending_rs485_send();
    bool send_current_request_to_rs485();
    void defer_current_request_retry(const char* reason);
    void finish_deferred_send_failure(const char* reason);
    void finish_current_request(BridgeWorkerState terminal_state);
    void set_current_state(BridgeWorkerState state);
    static const char* worker_state_name(BridgeWorkerState state);
    static bool validate_response_match(const ParseResult& result, const TcpParseResult& request);
    bool is_coexistence_backoff_active() const;
    bool is_coexistence_pressure_active() const;
    void note_rs485_contention(const char* reason, bool immediate_backoff = false);
    void reset_rs485_contention();
    bool is_coexistence_error(const String& error) const;
    bool try_coexistence_backoff_for_current_request(const char* reason);
    bool try_coexistence_cache_for_current_request(const char* reason, bool required);
    bool send_wifi_response(const ParseResult& rs485_result);
    void send_error_response(const String& error);
    bool send_gateway_target_failed_response(const String& reason);
    // Resolve the current request's client handle to a live TCPClient*, or
    // nullptr if it has been disconnected/removed in the meantime.
    TCPClient* resolve_current_client();

    // ========== Fallback Cache Methods ==========
    void cache_read_response(const TcpParseResult& request,
                             const std::vector<uint8_t>& tcp_response);
    void cache_response_for_fallback(const ReadCacheKey& key,
                                     const std::vector<uint8_t>& tcp_response);
    bool get_cached_response(const ReadCacheKey& key, std::vector<uint8_t>& out_response,
                             uint32_t max_age_ms, uint32_t* out_age_ms = nullptr,
                             bool* out_found = nullptr, bool count_stats = true);
    bool get_fallback_response(const ReadCacheKey& key, std::vector<uint8_t>& out_response);
    bool try_fallback_cache_for_current_request(const char* reason);
    bool send_response_to_client(const std::vector<uint8_t>& response, size_t response_size);
    void evict_oldest_cache_entry();

    // ========== RS485 Response Handling ==========
    BridgeWorkerState handle_rs485_success(const ParseResult& rs485_result, unsigned long elapsed);
    BridgeWorkerState handle_rs485_error(const ParseResult& rs485_result, unsigned long elapsed);
    void build_value_summary(const ParseResult& rs485_result, char* buffer, size_t buffer_size);
    bool try_fallback_cache_on_error(const ParseResult& rs485_result);

    TCPServer* tcp_server_ = nullptr;
    RS485Manager* rs485_ = nullptr;
    String dongle_serial_;

    static constexpr size_t REQUEST_QUEUE_MAX_DEPTH = 4;

    std::array<BridgeRequest, REQUEST_QUEUE_MAX_DEPTH> request_queue_;
    size_t request_queue_head_ = 0;
    size_t request_queue_tail_ = 0;
    size_t request_queue_count_ = 0;
    BridgeRequest current_request_;
    bool has_active_request_ = false;
    BridgeWorkerState worker_state_ = BridgeWorkerState::IDLE;
    BridgeWorkerState last_terminal_state_ = BridgeWorkerState::IDLE;
    bool waiting_rs485_response_ = false;
    bool pending_rs485_send_retry_ = false;
    uint32_t last_request_time_ = 0;
    uint32_t last_send_attempt_time_ = 0;
    uint32_t current_retry_delay_ms_ = RS485_SEND_RETRY_DELAY_MS;
    bool paused_ = false;

    // ========== Dual-dongle coexistence ==========
    uint32_t coexistence_backoff_until_ms_ = 0;
    uint32_t last_coexistence_event_ms_ = 0;
    uint32_t coexistence_events_ = 0;
    uint32_t coexistence_cache_hits_ = 0;
    uint32_t coexistence_cache_stale_ = 0;
    uint32_t coexistence_cache_misses_ = 0;
    uint8_t consecutive_contention_events_ = 0;

    // ========== Fallback Cache ==========
    std::map<ReadCacheKey, ReadCacheEntry> fallback_cache_;
    static constexpr size_t MAX_CACHE_ENTRIES = 14;

    // Cache statistics
    uint32_t cache_hits_ = 0;
    uint32_t cache_misses_ = 0;
    uint32_t cache_invalidations_ = 0;

    // Statistics
    uint32_t queued_requests_ = 0;
    uint32_t queue_drops_ = 0;
    uint32_t client_gone_count_ = 0;
    uint32_t last_finished_request_id_ = 0;
    uint32_t last_finished_elapsed_ms_ = 0;
    uint32_t total_requests_ = 0;
    uint32_t successful_requests_ = 0;
    uint32_t failed_requests_ = 0;

    static constexpr uint32_t REQUEST_TIMEOUT_MS = 2000;
    static constexpr uint32_t RS485_SEND_RETRY_DELAY_MS = 120;
    static constexpr uint32_t RS485_SEND_RETRY_JITTER_MS = 80;
    static constexpr uint32_t RS485_SEND_RETRY_WINDOW_MS = 1600;
    static constexpr uint8_t RS485_SEND_MAX_RETRIES = 14;
    static constexpr uint32_t FALLBACK_CACHE_MAX_AGE_MS = 45 * 1000;
};
