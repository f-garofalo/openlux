/**
 * @file rs485_manager.h
 * @brief RS485 communication manager for Inverter inverters
 *
 * This module handles the physical RS485 interface and communication
 * with Inverter inverters. Protocol details are in inverter_protocol.h.
 *
 * @license GPL-3.0
 * @author OpenLux Contributors
 */

#pragma once

#include "inverter_protocol.h"

#include <HardwareSerial.h>

// Note: RS485_PROBE_BACKOFF_BASE_MS and RS485_PROBE_BACKOFF_MAX_MS are defined in config.h

// ============================================================================
// RS485Manager Class
// ============================================================================

/**
 * @brief RS485 Interface Manager
 *
 * Handles UART communication with Inverter inverters via RS485.
 * Features:
 * - Automatic inverter detection via serial probe
 * - Multi-master support (coexistence with official WiFi dongle)
 * - Request/response handling with timeout
 * - Statistics tracking
 */
class RS485Manager {
  public:
    static RS485Manager& getInstance();

    // ========== Lifecycle ==========
    void begin(HardwareSerial& serial, int8_t tx_pin, int8_t rx_pin, int8_t de_pin = -1,
               uint32_t baud_rate = 19200);
    void loop();
    void probe_inverter_serial();

    // ========== Communication ==========
    bool send_read_request(ModbusFunctionCode func, uint16_t start_reg, uint16_t count);
    bool send_write_request(uint16_t start_reg, const std::vector<uint16_t>& values);

    // ========== Configuration ==========
    void set_serial_number(const String& serial) { serial_number_ = serial; }
    void set_response_timeout(uint32_t timeout_ms) { response_timeout_ms_ = timeout_ms; }

    // ========== Status ==========
    bool is_initialized() const { return initialized_; }
    bool is_waiting_response() const { return waiting_response_; }
    const ParseResult& get_last_result() const { return last_result_; }
    const std::vector<uint8_t>& get_last_raw_response() const { return last_raw_response_; }
    const String& get_detected_inverter_serial() const { return inverter_serial_detected_; }
    bool is_inverter_link_up() const { return inverter_link_ok_; }

    // ========== Statistics ==========
    uint32_t get_total_requests() const { return total_requests_; }
    uint32_t get_successful_responses() const { return successful_responses_; }
    uint32_t get_failed_responses() const { return failed_responses_; }
    uint32_t get_timeout_count() const { return timeout_count_; }
    uint32_t get_ignored_packets() const { return ignored_packets_; }

  private:
    RS485Manager() = default;
    ~RS485Manager() = default;
    RS485Manager(const RS485Manager&) = delete;
    RS485Manager& operator=(const RS485Manager&) = delete;

    // ========== Probe & Packet Sending ==========
    void request_inverter_serial_probe();
    void send_packet(const std::vector<uint8_t>& packet);

    // ========== Data Reception ==========
    void process_incoming_data();
    bool should_ignore_packet(const std::vector<uint8_t>& data);
    void handle_invalid_frame();

    // ========== Response Processing ==========
    void handle_response(const std::vector<uint8_t>& data);
    void handle_response_not_found(const std::vector<FrameInfo>& frames);
    void process_response_result(const std::vector<uint8_t>& data);
    void log_successful_response();
    void extract_inverter_serial(const std::vector<uint8_t>& data);

    // ========== Timeout & Error Handling ==========
    void handle_timeout();
    void handle_probe_failure(const char* reason);

    // ========== Utilities ==========
    static const char* function_code_to_string(ModbusFunctionCode func);
    bool is_bus_busy() const;

    // ========== Hardware ==========
    HardwareSerial* serial_ = nullptr;
    int8_t de_pin_ = -1;
    bool initialized_ = false;

    // ========== Configuration ==========
    String serial_number_;
    uint32_t response_timeout_ms_ = MODBUS_RESPONSE_TIMEOUT_MS;

    // ========== State ==========
    bool waiting_response_ = false;
    ModbusFunctionCode expected_function_code_ = ModbusFunctionCode::READ_INPUT;
    uint16_t expected_start_reg_ = 0;
    unsigned long last_tx_time_ = 0;
    unsigned long last_rx_time_ = 0;

    // ========== Buffers ==========
    std::vector<uint8_t> rx_buffer_;
    std::vector<uint8_t> last_raw_response_;
    ParseResult last_result_;

    // ========== Inverter State ==========
    String inverter_serial_detected_;
    bool serial_probe_pending_ = false;
    bool inverter_link_ok_ = false;
    uint32_t next_serial_probe_ms_ = 0;
    uint32_t serial_probe_backoff_ms_ = 0;

    // ========== Statistics ==========
    uint32_t total_requests_ = 0;
    uint32_t successful_responses_ = 0;
    uint32_t failed_responses_ = 0;
    uint32_t timeout_count_ = 0;
    uint32_t ignored_packets_ = 0;

    uint32_t external_requests_detected_ = 0;
    uint32_t bus_busy_until_ms_ = 0;
};
