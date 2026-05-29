/**
 * @file protocol_bridge.cpp
 * @brief Protocol bridge implementation
 *
 * @license GPL-3.0
 * @author OpenLux Contributors
 */
#include "protocol_bridge.h"

#include "../config.h"
#include "logger.h"

#include <utility>

#include <WiFi.h>

static const char* TAG = "bridge";
static constexpr uint8_t MODBUS_EXCEPTION_GATEWAY_TARGET_FAILED = 0x0B;

ProtocolBridge& ProtocolBridge::getInstance() {
    static ProtocolBridge instance;
    return instance;
}

void ProtocolBridge::begin(const String& dongle_serial) {
    dongle_serial_ = dongle_serial;

    LOGI(TAG, "Initializing Protocol Bridge");
    LOGI(TAG, "  Dongle Serial: %s", dongle_serial_.c_str());
    LOGI(TAG, "  RS485 worker queue: %u request(s)", (unsigned) REQUEST_QUEUE_MAX_DEPTH);
}

void ProtocolBridge::loop() {
    if (!is_ready()) {
        return;
    }

    if (!waiting_rs485_response_ && !pending_rs485_send_retry_ && !has_active_request_) {
        start_next_request();
    }

    if (pending_rs485_send_retry_) {
        process_pending_rs485_send();
    }

    // Check for RS485 response if we're waiting
    if (waiting_rs485_response_) {
        process_rs485_response();

        const uint32_t timeout_ms = REQUEST_TIMEOUT_MS;

        // Check timeout
        if (waiting_rs485_response_ && millis() - last_request_time_ > timeout_ms) {
            // Don't log as error if it's a network issue
            if (WiFi.status() != WL_CONNECTED) {
                LOGW(TAG, "Request timeout during WiFi disconnection (%lu ms)", timeout_ms);
            } else {
                LOGW(TAG, "Request timeout (%lu ms)", timeout_ms);
            }
            send_error_response("Request timeout");
            waiting_rs485_response_ = false;
            failed_requests_++;
            finish_current_request(BridgeWorkerState::FAILED);
        }
    }

    if (!waiting_rs485_response_ && !pending_rs485_send_retry_ && !has_active_request_) {
        start_next_request();
    }
}

TCPClient* ProtocolBridge::resolve_current_client() {
    if (!tcp_server_ || !current_request_.client_handle) {
        return nullptr;
    }
    return tcp_server_->resolve_client(current_request_.client_handle);
}

uint32_t ProtocolBridge::get_coexistence_backoff_remaining_ms() const {
#if !RS485_COEXISTENCE_ENABLED
    return 0;
#else
    const uint32_t now = millis();
    if ((int32_t) (coexistence_backoff_until_ms_ - now) <= 0) {
        return 0;
    }
    return coexistence_backoff_until_ms_ - now;
#endif
}

bool ProtocolBridge::is_coexistence_backoff_active() const {
#if !RS485_COEXISTENCE_ENABLED
    return false;
#else
    return get_coexistence_backoff_remaining_ms() > 0;
#endif
}

uint32_t ProtocolBridge::get_coexistence_pressure_remaining_ms() const {
#if !RS485_COEXISTENCE_ENABLED
    return 0;
#else
    const uint32_t backoff_ms = get_coexistence_backoff_remaining_ms();
    if (backoff_ms > 0) {
        return backoff_ms;
    }

    if (last_coexistence_event_ms_ == 0) {
        return 0;
    }

    const uint32_t elapsed_ms = millis() - last_coexistence_event_ms_;
    if (elapsed_ms >= RS485_COEXISTENCE_PRESSURE_WINDOW_MS) {
        return 0;
    }
    return RS485_COEXISTENCE_PRESSURE_WINDOW_MS - elapsed_ms;
#endif
}

bool ProtocolBridge::is_coexistence_pressure_active() const {
    return get_coexistence_pressure_remaining_ms() > 0;
}

bool ProtocolBridge::is_coexistence_error(const String& error) const {
#if !RS485_COEXISTENCE_ENABLED
    return false;
#else
    return error == "Timeout" || error.indexOf("other master") >= 0 ||
           error.indexOf("collision") >= 0 || error.indexOf("Invalid response frame") >= 0 ||
           error.indexOf("No valid frames") >= 0;
#endif
}

void ProtocolBridge::note_rs485_contention(const char* reason, bool immediate_backoff) {
#if !RS485_COEXISTENCE_ENABLED
    (void) reason;
    (void) immediate_backoff;
    return;
#else
    last_coexistence_event_ms_ = millis();
    coexistence_events_++;
    if (consecutive_contention_events_ < 255) {
        consecutive_contention_events_++;
    }

    if (!immediate_backoff && consecutive_contention_events_ < RS485_COEXISTENCE_TRIGGER_EVENTS) {
        LOGI(TAG, "RS485 contention marker %u/%u: %s", consecutive_contention_events_,
             RS485_COEXISTENCE_TRIGGER_EVENTS, reason ? reason : "unknown");
        return;
    }

    const uint32_t now = millis();
    const uint32_t until = now + RS485_COEXISTENCE_BACKOFF_MS;
    const bool was_active = is_coexistence_backoff_active();
    coexistence_backoff_until_ms_ = until;

    LOGW(TAG, "%scoexistence backoff for %lums after RS485 contention: %s",
         was_active ? "Extending " : "Entering ", RS485_COEXISTENCE_BACKOFF_MS,
         reason ? reason : "unknown");
#endif
}

void ProtocolBridge::reset_rs485_contention() {
    if (consecutive_contention_events_ > 0 && !is_coexistence_backoff_active()) {
        LOGI(TAG, "RS485 contention cleared after successful fresh response");
    }
    consecutive_contention_events_ = 0;
}

bool ProtocolBridge::try_coexistence_backoff_for_current_request(const char* reason) {
    const uint32_t remaining_ms = get_coexistence_backoff_remaining_ms();
    if (remaining_ms == 0) {
        return false;
    }

    set_current_state(BridgeWorkerState::COEXISTENCE_BACKOFF);

    if (try_coexistence_cache_for_current_request(reason, true)) {
        return true;
    }

    LOGW(TAG, "[REQ#%u] Coexistence backoff active (%lums left) but no fresh cache is available",
         current_request_.id, remaining_ms);
    send_error_response("RS485 coexistence backoff active");
    failed_requests_++;
    finish_current_request(BridgeWorkerState::FAILED);
    return true;
}

bool ProtocolBridge::try_coexistence_cache_for_current_request(const char* reason, bool required) {
#if !RS485_COEXISTENCE_ENABLED
    (void) reason;
    (void) required;
    return false;
#else
    if (current_request_.wifi_request.is_write_operation) {
        return false;
    }

    ReadCacheKey cache_key{current_request_.wifi_request.function_code,
                           current_request_.wifi_request.start_register,
                           current_request_.wifi_request.register_count};

    std::vector<uint8_t> cached_response;
    uint32_t age_ms = 0;
    bool found = false;
    if (!get_cached_response(cache_key, cached_response, RS485_COEXISTENCE_CACHE_MAX_AGE_MS,
                             &age_ms, &found, true)) {
        if (found) {
            coexistence_cache_stale_++;
            if (required) {
                LOGW(TAG, "[REQ#%u] Coexistence cache stale for %s (age=%lums > %lums)",
                     current_request_.id, cache_key.format().c_str(), age_ms,
                     RS485_COEXISTENCE_CACHE_MAX_AGE_MS);
            } else {
                LOGD(TAG, "[REQ#%u] Coexistence cache stale for %s (age=%lums)",
                     current_request_.id, cache_key.format().c_str(), age_ms);
            }
        } else {
            coexistence_cache_misses_++;
            if (required) {
                LOGW(TAG, "[REQ#%u] No coexistence cache entry for %s", current_request_.id,
                     cache_key.format().c_str());
            }
        }
        return false;
    }

    set_current_state(BridgeWorkerState::CACHE_FALLBACK);
    if (!send_response_to_client(cached_response, cached_response.size())) {
        failed_requests_++;
        finish_current_request(BridgeWorkerState::FAILED);
        return true;
    }

    coexistence_cache_hits_++;
    successful_requests_++;
    LOGI(TAG, "[REQ#%u] Served coexistence cache for %s (%s, age=%lums)", current_request_.id,
         cache_key.format().c_str(), reason ? reason : "coexistence", age_ms);
    finish_current_request(BridgeWorkerState::CACHE_FALLBACK);
    return true;
#endif
}

void ProtocolBridge::process_wifi_request(const uint8_t* data, size_t length, TCPClient* client) {
    if (!is_ready()) {
        LOGW(TAG, "Bridge not ready (tcp_server=%p, rs485=%p)", tcp_server_, rs485_);
        return;
    }

    // Snapshot a stable handle immediately. The TCPClient* is only valid for
    // the duration of this synchronous call; after that, the backing vector
    // can move. We keep the AsyncClient* (heap-stable) and the IP string.
    AsyncClient* client_handle = client ? client->client : nullptr;
    String client_ip = client ? client->remote_ip : String("unknown");

    // Helper lambda: send error using the currently-passed client pointer,
    // which is still live within this call.
    auto send_err = [&](const String& err) {
        if (client && client->is_connected() && client->client) {
            LOGW(TAG, "Error to %s: %s", client_ip.c_str(), err.c_str());
            tcp_server_->request_client_close(client->client, err.c_str());
        }
    };

    // Check if bridge is manually paused
    if (paused_) {
        LOGW(TAG, "Bridge paused by user, rejecting request");
        send_err("Bridge paused (maintenance mode)");
        failed_requests_++;
        return;
    }

    // Check if any blocking operation (other than TCP) is in progress
    // WiFi scan, OTA, and Network validation can interfere with TCP processing
    auto& guard_mgr = OperationGuardManager::getInstance();
    if (!guard_mgr.canPerformOperation(OperationGuard::OperationType::TCP_CLIENT_PROCESSING)) {
        OperationGuard::OperationType active_op = guard_mgr.getActiveOperation();
        const char* op_name = OperationGuardManager::getOperationTypeName(active_op);
        LOGW(TAG, "Bridge paused (%s), rejecting request, operation in progress: ", op_name);
        send_err("Bridge paused");
        failed_requests_++;
        return;
    }

    total_requests_++;

    // Use static buffers instead of String to avoid memory fragmentation
    char req_tag[20];
    snprintf(req_tag, sizeof(req_tag), "[REQ#%u] ", total_requests_);
    LOGD(TAG, "%sWiFi raw (first 40b): %s", req_tag,
         TcpProtocol::format_hex(data, min(length, (size_t) 40)).c_str());

    TcpParseResult parse_result = TcpProtocol::parse_request(data, length);

    if (!parse_result.success) {
        LOGE(TAG, "✗ Failed to parse WiFi request: %s", parse_result.error_message.c_str());
        send_err(parse_result.error_message);
        failed_requests_++;
        return;
    }

    // Build operation description using static buffers
    const char* op_type = "UNKNOWN";
    char op_details[64];

    if (parse_result.is_write_operation) {
        if (parse_result.write_values.size() == 1) {
            op_type = "WRITE_SINGLE";
            snprintf(op_details, sizeof(op_details), "reg=%u val=0x%X", parse_result.start_register,
                     parse_result.write_values[0]);
        } else {
            op_type = "WRITE_MULTI";
            snprintf(op_details, sizeof(op_details), "regs=%u-%u (%zu vals)",
                     parse_result.start_register,
                     parse_result.start_register + parse_result.write_values.size() - 1,
                     parse_result.write_values.size());
        }
    } else {
        if (parse_result.function_code == 0x03) {
            op_type = "READ_HOLD";
        } else if (parse_result.function_code == 0x04) {
            op_type = "READ_INPUT";
        } else {
            op_type = "READ";
        }
        snprintf(op_details, sizeof(op_details), "regs=%u-%u (%u regs)",
                 parse_result.start_register,
                 parse_result.start_register + parse_result.register_count - 1,
                 parse_result.register_count);
    }

    LOGI(TAG, "━━━ Request #%u: %s %s from %s ━━━", total_requests_, op_type, op_details,
         client_ip.c_str());
    LOGD(TAG, "%sInverter SN: %s", req_tag,
         TcpProtocol::format_serial(parse_result.inverter_serial).c_str());

    if (queue_full()) {
        LOGW(TAG, "Bridge queue full (%u/%u), rejecting request #%u from %s",
             (unsigned) request_queue_count_, (unsigned) REQUEST_QUEUE_MAX_DEPTH, total_requests_,
             client_ip.c_str());
        queue_drops_++;
        send_err("Bridge queue full");
        failed_requests_++;
        return;
    }

    BridgeRequest request;
    request.client_handle = client_handle;
    request.client_ip = client_ip;
    request.wifi_request = parse_result;
    request.timestamp = millis();
    request.retry_count = 0;
    request.id = total_requests_;

    if (!enqueue_request(std::move(request))) {
        LOGW(TAG, "Bridge queue full during enqueue, rejecting request #%u from %s",
             total_requests_, client_ip.c_str());
        queue_drops_++;
        send_err("Bridge queue full");
        failed_requests_++;
        return;
    }
    queued_requests_++;

    if (!has_active_request_ && !waiting_rs485_response_ && !pending_rs485_send_retry_) {
        set_current_state(BridgeWorkerState::QUEUED);
    }

    LOGI(TAG, "[REQ#%u] Queued for RS485 worker (%u/%u, worker=%s)", total_requests_,
         (unsigned) request_queue_count_, (unsigned) REQUEST_QUEUE_MAX_DEPTH,
         worker_state_name(worker_state_));
}

const char* ProtocolBridge::worker_state_name(BridgeWorkerState state) {
    switch (state) {
        case BridgeWorkerState::IDLE:
            return "IDLE";
        case BridgeWorkerState::QUEUED:
            return "QUEUED";
        case BridgeWorkerState::RS485_SEND:
            return "RS485_SEND";
        case BridgeWorkerState::RS485_RETRY:
            return "RS485_RETRY";
        case BridgeWorkerState::WAIT_RESPONSE:
            return "WAIT_RESPONSE";
        case BridgeWorkerState::COEXISTENCE_BACKOFF:
            return "COEXISTENCE_BACKOFF";
        case BridgeWorkerState::CACHE_FALLBACK:
            return "CACHE_FALLBACK";
        case BridgeWorkerState::RESPOND_TCP:
            return "RESPOND_TCP";
        case BridgeWorkerState::DONE:
            return "DONE";
        case BridgeWorkerState::FAILED:
            return "FAILED";
        default:
            return "UNKNOWN";
    }
}

void ProtocolBridge::set_pause(bool paused) {
    paused_ = paused;
    if (paused_) {
        drop_queued_requests("bridge paused");
    }
}

void ProtocolBridge::set_current_state(BridgeWorkerState state) {
    worker_state_ = state;
}

bool ProtocolBridge::enqueue_request(BridgeRequest&& request) {
    if (queue_full()) {
        return false;
    }

    request_queue_[request_queue_tail_] = std::move(request);
    request_queue_tail_ = (request_queue_tail_ + 1) % REQUEST_QUEUE_MAX_DEPTH;
    request_queue_count_++;
    return true;
}

bool ProtocolBridge::dequeue_request(BridgeRequest& request) {
    if (queue_empty()) {
        return false;
    }

    request = std::move(request_queue_[request_queue_head_]);
    request_queue_[request_queue_head_] = BridgeRequest();
    request_queue_head_ = (request_queue_head_ + 1) % REQUEST_QUEUE_MAX_DEPTH;
    request_queue_count_--;
    return true;
}

void ProtocolBridge::drop_queued_requests(const char* reason) {
    BridgeRequest dropped;
    while (dequeue_request(dropped)) {
        queue_drops_++;
        failed_requests_++;

        TCPClient* client = tcp_server_ && dropped.client_handle
                                ? tcp_server_->resolve_client(dropped.client_handle)
                                : nullptr;
        if (client && client->client) {
            tcp_server_->request_client_close(client->client, reason);
        }

        LOGW(TAG, "[REQ#%u] Dropped queued request from %s: %s", dropped.id,
             dropped.client_ip.c_str(), reason ? reason : "unknown");
    }

    if (!has_active_request_ && queue_empty()) {
        set_current_state(BridgeWorkerState::IDLE);
    }
}

void ProtocolBridge::start_next_request() {
    if (has_active_request_ || queue_empty()) {
        return;
    }

    while (dequeue_request(current_request_)) {
        has_active_request_ = true;
        set_current_state(BridgeWorkerState::RS485_SEND);

        if (!resolve_current_client()) {
            LOGW(TAG, "[REQ#%u] Queued client %s disconnected before RS485 send",
                 current_request_.id, current_request_.client_ip.c_str());
            client_gone_count_++;
            failed_requests_++;
            finish_current_request(BridgeWorkerState::FAILED);
            continue;
        }

        start_current_request();
        return;
    }
}

void ProtocolBridge::start_current_request() {
    if (!has_active_request_) {
        return;
    }

    set_current_state(BridgeWorkerState::RS485_SEND);

    if (try_coexistence_backoff_for_current_request("coexistence backoff")) {
        return;
    }

    if (is_coexistence_pressure_active() &&
        try_coexistence_cache_for_current_request("recent RS485 contention", false)) {
        return;
    }

    if (rs485_) {
        const uint32_t bus_busy_ms = rs485_->get_bus_busy_remaining_ms();
        if (bus_busy_ms > 0) {
            note_rs485_contention("external RS485 traffic detected", true);
            if (try_coexistence_backoff_for_current_request("external RS485 traffic")) {
                return;
            }
        }
    }

    LOGD(TAG, "[REQ#%u] RS485 packet: %s", current_request_.id,
         TcpProtocol::format_hex(current_request_.wifi_request.rs485_packet.data(),
                                 current_request_.wifi_request.rs485_packet.size())
             .c_str());

    auto& guard_mgr = OperationGuardManager::getInstance();
    if (!guard_mgr.canPerformOperation(OperationGuard::OperationType::TCP_CLIENT_PROCESSING)) {
        defer_current_request_retry("operation guard busy before RS485 send");
        return;
    }

    auto guard = guard_mgr.acquireGuard(OperationGuard::OperationType::TCP_CLIENT_PROCESSING,
                                        "rs485_worker_send");

    if (!send_current_request_to_rs485()) {
        defer_current_request_retry("initial RS485 send failed");
        return;
    }

    waiting_rs485_response_ = true;
    last_request_time_ = millis();
    set_current_state(BridgeWorkerState::WAIT_RESPONSE);
}

void ProtocolBridge::finish_current_request(BridgeWorkerState terminal_state) {
    if (!has_active_request_) {
        set_current_state(BridgeWorkerState::IDLE);
        return;
    }

    const uint32_t elapsed = millis() - current_request_.timestamp;
    LOGD(TAG, "[REQ#%u] Worker finished state=%s elapsed=%lums queue=%u/%u", current_request_.id,
         worker_state_name(terminal_state), elapsed, (unsigned) request_queue_count_,
         (unsigned) REQUEST_QUEUE_MAX_DEPTH);

    last_finished_request_id_ = current_request_.id;
    last_finished_elapsed_ms_ = elapsed;
    last_terminal_state_ = terminal_state;

    waiting_rs485_response_ = false;
    pending_rs485_send_retry_ = false;
    current_request_ = BridgeRequest();
    has_active_request_ = false;
    set_current_state(queue_empty() ? BridgeWorkerState::IDLE : BridgeWorkerState::QUEUED);
}

bool ProtocolBridge::send_current_request_to_rs485() {
    const TcpParseResult& request = current_request_.wifi_request;

    if (request.is_write_operation) {
        return rs485_->send_write_request(request.start_register, request.write_values);
    }

    ModbusFunctionCode func = static_cast<ModbusFunctionCode>(request.function_code);
    return rs485_->send_read_request(func, request.start_register, request.register_count);
}

void ProtocolBridge::defer_current_request_retry(const char* reason) {
    pending_rs485_send_retry_ = true;
    waiting_rs485_response_ = false;
    last_send_attempt_time_ = millis();
    current_request_.retry_count = 0;
    set_current_state(BridgeWorkerState::RS485_RETRY);

    LOGD(TAG, "%s; keeping TCP client open and retrying for %lums", reason,
         RS485_SEND_RETRY_WINDOW_MS);
}

void ProtocolBridge::finish_deferred_send_failure(const char* reason) {
    pending_rs485_send_retry_ = false;

    if (try_fallback_cache_for_current_request(reason)) {
        failed_requests_++;
        finish_current_request(BridgeWorkerState::CACHE_FALLBACK);
        return;
    }

    LOGE(TAG, "✗ %s (no fallback cache)", reason);
    send_error_response(reason);
    failed_requests_++;
    finish_current_request(BridgeWorkerState::FAILED);
}

void ProtocolBridge::process_pending_rs485_send() {
    TCPClient* client = resolve_current_client();
    if (!client) {
        LOGW(TAG, "Deferred RS485 send abandoned: client %s disconnected",
             current_request_.client_ip.c_str());
        client_gone_count_++;
        failed_requests_++;
        finish_current_request(BridgeWorkerState::FAILED);
        return;
    }

    const uint32_t now = millis();
    const uint32_t age_ms = now - current_request_.timestamp;

    if (try_coexistence_backoff_for_current_request("coexistence backoff during RS485 retry")) {
        return;
    }

    if (is_coexistence_pressure_active() &&
        try_coexistence_cache_for_current_request("recent RS485 contention during retry", false)) {
        return;
    }

    if (rs485_) {
        const uint32_t bus_busy_ms = rs485_->get_bus_busy_remaining_ms();
        if (bus_busy_ms > 0) {
            note_rs485_contention("external RS485 traffic detected during retry", true);
            if (try_coexistence_backoff_for_current_request("external RS485 traffic")) {
                return;
            }
        }
    }

    if (age_ms >= RS485_SEND_RETRY_WINDOW_MS ||
        current_request_.retry_count >= RS485_SEND_MAX_RETRIES) {
        finish_deferred_send_failure("RS485 send retry window expired");
        return;
    }

    if (now - last_send_attempt_time_ < RS485_SEND_RETRY_DELAY_MS) {
        return;
    }

    auto& guard_mgr = OperationGuardManager::getInstance();
    if (!guard_mgr.canPerformOperation(OperationGuard::OperationType::TCP_CLIENT_PROCESSING)) {
        return;
    }

    auto guard = guard_mgr.acquireGuard(OperationGuard::OperationType::TCP_CLIENT_PROCESSING,
                                        "retry_rs485_send");

    current_request_.retry_count++;
    last_send_attempt_time_ = now;

    if (!send_current_request_to_rs485()) {
        LOGD(TAG, "RS485 send retry %u still busy/failed for %s", current_request_.retry_count,
             current_request_.client_ip.c_str());
        return;
    }

    pending_rs485_send_retry_ = false;
    waiting_rs485_response_ = true;
    last_request_time_ = millis();
    set_current_state(BridgeWorkerState::WAIT_RESPONSE);

    LOGI(TAG, "RS485 send succeeded after %u retry attempt(s), age=%lums",
         current_request_.retry_count, age_ms);
}

bool ProtocolBridge::validate_response_match(const ParseResult& result,
                                             const TcpParseResult& request) {
    // Check function code
    if (static_cast<uint8_t>(result.function_code) != request.function_code) {
        return false;
    }

    // Check start address
    if (result.start_address != request.start_register) {
        return false;
    }

    // Check register count only if it's a successful response (exceptions don't have count)
    if (result.success) {
        if (request.is_write_operation) {
            if (result.register_count != request.write_values.size()) {
                return false;
            }
        } else {
            if (result.register_count != request.register_count) {
                return false;
            }
        }
    }

    return true;
}

void ProtocolBridge::process_rs485_response() {
    if (!rs485_->is_waiting_response()) {
        // Response received
        const ParseResult& rs485_result = rs485_->get_last_result();
        unsigned long elapsed = millis() - last_request_time_;
        BridgeWorkerState terminal_state = BridgeWorkerState::FAILED;

        if (rs485_result.success) {
            terminal_state = handle_rs485_success(rs485_result, elapsed);
        } else {
            terminal_state = handle_rs485_error(rs485_result, elapsed);
        }

        waiting_rs485_response_ = false;
        finish_current_request(terminal_state);
    }
}

bool ProtocolBridge::send_wifi_response(const ParseResult& rs485_result) {
    set_current_state(BridgeWorkerState::RESPOND_TCP);

    // Resolve the client fresh at the point of use. The TCPClient entry may
    // have been removed or moved in the vector since process_wifi_request ran.
    TCPClient* client = resolve_current_client();
    if (!client) {
        LOGW(TAG, "⚠ Client %s no longer connected, dropping response",
             current_request_.client_ip.c_str());
        client_gone_count_++;
        return false;
    }

    // Prefer forwarding the raw RS485 packet (inverter echo) to preserve exact format
    const std::vector<uint8_t>& raw_response = rs485_->get_last_raw_response();
    if (raw_response.empty()) {
        LOGE(TAG, "✗ No raw RS485 response available");
        send_error_response("No RS485 response available");
        return false;
    }

    LOGD(TAG, "[REQ#%u] Wrapping raw RS485 response in TCP (A1 1A)...", current_request_.id);
    std::vector<uint8_t> wifi_response;
    uint8_t dongle_serial[10];
    TcpProtocol::copy_serial(dongle_serial_, dongle_serial);

    bool built = TcpProtocol::build_response(wifi_response, raw_response.data(),
                                             raw_response.size(), dongle_serial);

    if (!built) {
        LOGE(TAG, "✗ Failed to build WiFi response");
        send_error_response("Response build failed");
        return false;
    }

    // ========== CACHE FOR FALLBACK ==========
    // Store successful response in cache for potential fallback use
    cache_read_response(current_request_.wifi_request, wifi_response);
    // ========== END CACHE FOR FALLBACK ==========

    LOGI(TAG, "WiFi response built: %d bytes", wifi_response.size());
    LOGD(TAG, "  WiFi packet (first 60 bytes): %s",
         TcpProtocol::format_hex(wifi_response.data(), min(wifi_response.size(), (size_t) 60))
             .c_str());

    // Send to TCP client — re-resolve in case something changed during the
    // response build (unlikely but cheap to check).
    client = resolve_current_client();
    if (!client || !client->client) {
        LOGW(TAG, "⚠ Client %s disconnected during response build",
             current_request_.client_ip.c_str());
        client_gone_count_++;
        return false;
    }

    LOGI(TAG, "→ Sending to TCP client %s...", client->remote_ip.c_str());
    size_t written = client->client->write(reinterpret_cast<const char*>(wifi_response.data()),
                                           wifi_response.size());
    if (written == wifi_response.size()) {
        client->last_activity = millis();
        LOGI(TAG, "✓ Response sent successfully (%d bytes)", written);
        return true;
    }

    LOGW(TAG, "⚠ Partial write: %d/%d bytes", written, wifi_response.size());
    return false;
}

void ProtocolBridge::send_error_response(const String& error) {
    TCPClient* client = resolve_current_client();
    if (!client) {
        LOGD(TAG, "Client gone, dropping error: %s", error.c_str());
        return;
    }

    LOGW(TAG, "Sending error response to client: %s", error.c_str());

    // Get the last RS485 raw response if available
    const std::vector<uint8_t>& raw_response = rs485_->get_last_raw_response();
    const ParseResult& last_result = rs485_->get_last_result();

    if (!raw_response.empty() &&
        validate_response_match(last_result, current_request_.wifi_request)) {
        // We have the raw exception response from inverter - forward it to client
        std::vector<uint8_t> wifi_response;
        uint8_t dongle_serial[10];
        TcpProtocol::copy_serial(dongle_serial_, dongle_serial);

        bool built = TcpProtocol::build_response(wifi_response, raw_response.data(),
                                                 raw_response.size(), dongle_serial);

        // Re-resolve after build (cheap; handles race with cleanup).
        client = resolve_current_client();
        if (built && client && client->client) {
            size_t written = client->client->write(
                reinterpret_cast<const char*>(wifi_response.data()), wifi_response.size());
            if (written == wifi_response.size()) {
                client->last_activity = millis();
            }
            LOGI(TAG, "✓ Exception response forwarded to client (%d bytes)", written);
            return;
        }
    } else if (!raw_response.empty()) {
        LOGW(TAG, "Raw RS485 error does not match current request; not forwarding stale data");
    }

    if (send_gateway_target_failed_response(error)) {
        return;
    }

    // Last resort: close connection if we can't build a protocol-compatible response.
    LOGW(TAG, "⚠ Cannot build gateway exception response, closing connection");
    client = resolve_current_client();
    if (client && client->client) {
        tcp_server_->request_client_close(client->client, "cannot build gateway exception");
    }
}

bool ProtocolBridge::send_gateway_target_failed_response(const String& reason) {
    TCPClient* client = resolve_current_client();
    if (!client || !client->client) {
        return false;
    }

    const TcpParseResult& request = current_request_.wifi_request;
    std::vector<uint8_t> exception_response(MODBUS_MIN_EXCEPTION_SIZE, 0);

    exception_response[InverterProtocolOffsets::ADDR] = MODBUS_DEVICE_ADDR_RESPONSE;
    exception_response[InverterProtocolOffsets::FUNC] = request.function_code | 0x80;
    memcpy(&exception_response[InverterProtocolOffsets::SERIAL_NUM], request.inverter_serial,
           MODBUS_SERIAL_NUMBER_LENGTH);
    InverterProtocol::write_little_endian_uint16(
        exception_response.data(), InverterProtocolOffsets::START_REG, request.start_register);
    exception_response[InverterProtocolOffsets::EXCEPTION_CODE] =
        MODBUS_EXCEPTION_GATEWAY_TARGET_FAILED;

    const size_t crc_offset = MODBUS_MIN_EXCEPTION_SIZE - 2;
    const uint16_t crc = InverterProtocol::calculate_crc16(exception_response.data(), crc_offset);
    InverterProtocol::write_little_endian_uint16(exception_response.data(), crc_offset, crc);

    std::vector<uint8_t> wifi_response;
    uint8_t dongle_serial[10];
    TcpProtocol::copy_serial(dongle_serial_, dongle_serial);

    if (!TcpProtocol::build_response(wifi_response, exception_response.data(),
                                     exception_response.size(), dongle_serial)) {
        LOGW(TAG, "Failed to build synthetic gateway exception for: %s", reason.c_str());
        return false;
    }

    set_current_state(BridgeWorkerState::RESPOND_TCP);
    client = resolve_current_client();
    if (!client || !client->client) {
        LOGD(TAG, "Client gone before gateway exception could be sent");
        return false;
    }

    size_t written = client->client->write(reinterpret_cast<const char*>(wifi_response.data()),
                                           wifi_response.size());
    if (written != wifi_response.size()) {
        LOGW(TAG, "⚠ Partial gateway exception write: %d/%d bytes", written, wifi_response.size());
        return false;
    }

    client->last_activity = millis();
    LOGW(TAG,
         "Sent synthetic Modbus exception 0x%02X "
         "(Gateway Target Device Failed to Respond) for func=0x%02X start=%u: %s",
         MODBUS_EXCEPTION_GATEWAY_TARGET_FAILED, request.function_code, request.start_register,
         reason.c_str());
    return true;
}

// ============================================================================
// RS485 Response Handling
// ============================================================================

BridgeWorkerState ProtocolBridge::handle_rs485_success(const ParseResult& rs485_result,
                                                       unsigned long elapsed) {
    // Validate response matches request to avoid processing snooped packets
    if (!validate_response_match(rs485_result, current_request_.wifi_request)) {
        const uint16_t expected_count = current_request_.wifi_request.is_write_operation
                                            ? current_request_.wifi_request.write_values.size()
                                            : current_request_.wifi_request.register_count;
        LOGW(TAG,
             "⚠ Response mismatch! Expected func=0x%02X start=%d count=%d, Got func=0x%02X "
             "start=%d count=%d",
             current_request_.wifi_request.function_code,
             current_request_.wifi_request.start_register, expected_count,
             static_cast<uint8_t>(rs485_result.function_code), rs485_result.start_address,
             rs485_result.register_count);
        note_rs485_contention("response mismatch/not ours", true);

        // Fix #6: on a mismatch (typically dual-master collision), don't hard-fail.
        // A cached read response is better than an error from HA's perspective.
        if (try_fallback_cache_on_error(rs485_result)) {
            LOGI(TAG, "Mismatch recovered via fallback cache");
            failed_requests_++;
            return BridgeWorkerState::CACHE_FALLBACK;
        }

        send_error_response("Response mismatch (collision?)");
        failed_requests_++;
        return BridgeWorkerState::FAILED;
    }

    // Build and log value summary
    char value_summary[128] = "";
    build_value_summary(rs485_result, value_summary, sizeof(value_summary));

    LOGI(TAG, "[REQ#%u] OK func=0x%02X regs=%d start=%d time=%lums%s", current_request_.id,
         static_cast<uint8_t>(rs485_result.function_code), rs485_result.register_count,
         rs485_result.start_address, elapsed, value_summary);
    reset_rs485_contention();

    if (send_wifi_response(rs485_result)) {
        successful_requests_++;
        LOGI(TAG, "[REQ#%u] ✓ Completed (success: %d/%d = %.1f%%)", current_request_.id,
             successful_requests_, total_requests_,
             (100.0f * successful_requests_) / total_requests_);
        return BridgeWorkerState::DONE;
    }

    failed_requests_++;
    return BridgeWorkerState::FAILED;
}

BridgeWorkerState ProtocolBridge::handle_rs485_error(const ParseResult& rs485_result,
                                                     unsigned long elapsed) {
    if (is_coexistence_error(rs485_result.error_message)) {
        note_rs485_contention(rs485_result.error_message.c_str(), false);
    }

    // ========== TRY FALLBACK CACHE FIRST ==========
    // This is critical for handling collisions/mismatches
    // Even if response is mismatch, cache might have valid data
    if (try_fallback_cache_on_error(rs485_result)) {
        LOGI(TAG, "RS485 error, using FALLBACK CACHE despite any mismatch");
        failed_requests_++;
        return BridgeWorkerState::CACHE_FALLBACK;
    }
    // ========== END FALLBACK ATTEMPT ==========

    // If cache miss, validate exception response
    if (rs485_result.error_message.startsWith("Modbus Exception")) {
        if (!validate_response_match(rs485_result, current_request_.wifi_request)) {
            const uint16_t expected_count = current_request_.wifi_request.is_write_operation
                                                ? current_request_.wifi_request.write_values.size()
                                                : current_request_.wifi_request.register_count;
            LOGW(TAG,
                 "Exception response mismatch AND no fallback cache! Expected func=0x%02X "
                 "start=%d count=%d, Got func=0x%02X start=%d count=%d",
                 current_request_.wifi_request.function_code,
                 current_request_.wifi_request.start_register, expected_count,
                 static_cast<uint8_t>(rs485_result.function_code), rs485_result.start_address,
                 rs485_result.register_count);

            send_error_response("Response mismatch (collision?)");
            failed_requests_++;
            return BridgeWorkerState::FAILED;
        }
    }

    // Log the error
    LOGE(TAG, "✗ RS485 FAIL: %s (after %lums)", rs485_result.error_message.c_str(), elapsed);

    // No fallback available - send error
    send_error_response(rs485_result.error_message);

    const std::vector<uint8_t>& raw = rs485_->get_last_raw_response();
    if (!raw.empty()) {
        LOGD(TAG, "[REQ#%u] Raw RS485 resp: %s", current_request_.id,
             TcpProtocol::format_hex(raw.data(), raw.size()).c_str());
    }

    failed_requests_++;
    LOGE(TAG, "[REQ#%u] ✗ Failed (failures: %d/%d = %.1f%%)", current_request_.id, failed_requests_,
         total_requests_, (100.0f * failed_requests_) / total_requests_);
    return BridgeWorkerState::FAILED;
}

void ProtocolBridge::build_value_summary(const ParseResult& rs485_result, char* buffer,
                                         size_t buffer_size) {
    if (rs485_result.register_count == 1 && !rs485_result.register_values.empty()) {
        snprintf(buffer, buffer_size, " val=0x%X", rs485_result.register_values[0]);
    } else if (rs485_result.register_count > 0 && !rs485_result.register_values.empty()) {
        char* pos = buffer;
        int remaining = buffer_size;

        int written = snprintf(pos, remaining, " [0x%X", rs485_result.register_values[0]);
        pos += written;
        remaining -= written;

        for (size_t i = 1; i < min((size_t) 3, rs485_result.register_values.size()); i++) {
            written = snprintf(pos, remaining, ", 0x%X", rs485_result.register_values[i]);
            pos += written;
            remaining -= written;
        }

        if (rs485_result.register_count > 3) {
            snprintf(pos, remaining, "...]");
        } else {
            snprintf(pos, remaining, "]");
        }
    }
}

bool ProtocolBridge::try_fallback_cache_on_error(const ParseResult& rs485_result) {
    (void) rs485_result;
    return try_fallback_cache_for_current_request("RS485 error");
}

// ============================================================================
// Fallback Cache Implementation
// ============================================================================

void ProtocolBridge::cache_read_response(const TcpParseResult& request,
                                         const std::vector<uint8_t>& tcp_response) {
    // Only cache READ operations (not WRITE)
    if (request.is_write_operation) {
        return;
    }

    ReadCacheKey cache_key{request.function_code, request.start_register, request.register_count};

    cache_response_for_fallback(cache_key, tcp_response);
}

void ProtocolBridge::cache_response_for_fallback(const ReadCacheKey& key,
                                                 const std::vector<uint8_t>& tcp_response) {
    // First, check if this key already exists and remove it (to replace with fresh response)
    auto existing = fallback_cache_.find(key);
    if (existing != fallback_cache_.end()) {
        LOGD(TAG, "Replacing existing cache entry: %s", key.format().c_str());
        fallback_cache_.erase(existing);
    }

    evict_oldest_cache_entry();

    // Store new entry with fresh timestamp
    ReadCacheEntry entry;
    entry.key = key;
    entry.tcp_response_packet = tcp_response;
    entry.timestamp_ms = millis();
    entry.last_access_ms = millis();
    entry.hit_count = 0;

    fallback_cache_[key] = entry;

    LOGD(TAG, "Fallback cache: stored %s (size=%zu/%zu)", key.format().c_str(),
         fallback_cache_.size(), MAX_CACHE_ENTRIES);
}

void ProtocolBridge::clear_fallback_cache() {
    if (!fallback_cache_.empty()) {
        cache_invalidations_ += fallback_cache_.size();
        fallback_cache_.clear();
    }
}

bool ProtocolBridge::try_fallback_cache_for_current_request(const char* reason) {
    // Only try fallback for READ operations
    if (current_request_.wifi_request.is_write_operation) {
        return false;
    }

    ReadCacheKey cache_key{current_request_.wifi_request.function_code,
                           current_request_.wifi_request.start_register,
                           current_request_.wifi_request.register_count};

    std::vector<uint8_t> fallback_response;
    if (get_fallback_response(cache_key, fallback_response)) {
        // Fallback cache found - use it instead of error
        LOGI(TAG, "%s, using FALLBACK CACHE for %s", reason, cache_key.format().c_str());

        // Send cached response to client
        set_current_state(BridgeWorkerState::CACHE_FALLBACK);
        return send_response_to_client(fallback_response, fallback_response.size());
    }

    LOGW(TAG, "⚠ No fallback cache available for this request (%u-%u, %u)",
         current_request_.wifi_request.function_code, current_request_.wifi_request.start_register,
         current_request_.wifi_request.register_count);
    return false; // ← Failed, no cache available
}

bool ProtocolBridge::get_cached_response(const ReadCacheKey& key,
                                         std::vector<uint8_t>& out_response, uint32_t max_age_ms,
                                         uint32_t* out_age_ms, bool* out_found, bool count_stats) {
    if (out_found) {
        *out_found = false;
    }
    if (out_age_ms) {
        *out_age_ms = 0;
    }

    auto it = fallback_cache_.find(key);
    if (it == fallback_cache_.end()) {
        if (count_stats) {
            cache_misses_++;
        }
        return false;
    }

    if (out_found) {
        *out_found = true;
    }

    ReadCacheEntry& entry = it->second;
    const uint32_t age_ms = entry.get_age(millis());
    if (out_age_ms) {
        *out_age_ms = age_ms;
    }

    if (max_age_ms > 0 && age_ms > max_age_ms) {
        if (count_stats) {
            cache_misses_++;
        }
        return false;
    }

    if (count_stats) {
        cache_hits_++;
    }
    out_response = entry.tcp_response_packet;
    entry.increment_hit_count();
    entry.update_access_time();

    LOGI(TAG, "Fallback cache HIT: %s (hits=%u, age=%lums)", key.format().c_str(), entry.hit_count,
         age_ms);

    return true;
}

void ProtocolBridge::evict_oldest_cache_entry() {
    if (fallback_cache_.empty()) {
        return;
    }

    uint32_t now_ms = millis();
    // First pass: Remove entries older (TTL-based)
    for (auto it = fallback_cache_.begin(); it != fallback_cache_.end();) {
        if (it->second.get_age(now_ms) > FALLBACK_CACHE_MAX_AGE_MS) {
            LOGD(TAG, "Evicting stale cache entry: %s (age=%lums)", it->first.format().c_str(),
                 it->second.get_age(now_ms));
            it = fallback_cache_.erase(it);
            cache_invalidations_++;
        } else {
            ++it;
        }
    }

    // Second pass: If still full, remove oldest entry by timestamp
    if (fallback_cache_.size() >= MAX_CACHE_ENTRIES) {
        auto oldest = fallback_cache_.begin();
        for (auto it = fallback_cache_.begin(); it != fallback_cache_.end(); ++it) {
            if (it->second.is_older_than(oldest->second)) {
                oldest = it;
            }
        }

        LOGD(TAG, "Fallback cache full, evicting oldest: %s (age=%lums)",
             oldest->first.format().c_str(), oldest->second.get_age(now_ms));
        fallback_cache_.erase(oldest);
        cache_invalidations_++;
    }
}

void ProtocolBridge::print_cache_entries(std::function<void(const String&)> callback) const {
    if (fallback_cache_.empty()) {
        callback(String("  [empty]"));
        return;
    }

    uint32_t now_ms = millis();
    int index = 1;

    for (const auto& entry : fallback_cache_) {
        String line;
        line.reserve(150);

        line += "  [";
        line += index++;
        line += "] ";
        line += entry.first.format();
        line += " | ";
        line += "packet=";
        line += entry.second.tcp_response_packet.size();
        line += "B age=";
        line += entry.second.get_age(now_ms);
        line += "ms hits=";
        line += entry.second.hit_count;

        callback(line);
    }
}

bool ProtocolBridge::get_fallback_response(const ReadCacheKey& key,
                                           std::vector<uint8_t>& out_response) {
    return get_cached_response(key, out_response, FALLBACK_CACHE_MAX_AGE_MS);
}

bool ProtocolBridge::send_response_to_client(const std::vector<uint8_t>& response,
                                             size_t response_size) {
    TCPClient* client = resolve_current_client();
    if (!client || !client->client) {
        LOGW(TAG, "⚠ Client %s no longer connected, dropping cached response",
             current_request_.client_ip.c_str());
        client_gone_count_++;
        return false;
    }

    size_t written =
        client->client->write(reinterpret_cast<const char*>(response.data()), response_size);
    if (written == response_size) {
        client->last_activity = millis();
    }
    LOGI(TAG, "✓ Response sent to client: %u bytes", written);
    return written == response_size;
}
