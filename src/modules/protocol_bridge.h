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

  private:
    ProtocolBridge() = default;
    ~ProtocolBridge() = default;
    ProtocolBridge(const ProtocolBridge&) = delete;
    ProtocolBridge& operator=(const ProtocolBridge&) = delete;

    void process_rs485_response();
    static bool validate_response_match(const ParseResult& result, const TcpParseResult& request);
    void send_wifi_response(TCPClient* client, const ParseResult& rs485_result);
    void send_error_response(TCPClient* client, const String& error);

    TCPServer* tcp_server_ = nullptr;
    RS485Manager* rs485_ = nullptr;
    String dongle_serial_;

    BridgeRequest current_request_;
    bool waiting_rs485_response_ = false;
    uint32_t last_request_time_ = 0;
    bool paused_ = false;

    // Statistics
    uint32_t total_requests_ = 0;
    uint32_t successful_requests_ = 0;
    uint32_t failed_requests_ = 0;

    static constexpr uint32_t REQUEST_TIMEOUT_MS = 5000;
};
