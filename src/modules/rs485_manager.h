/**
 * @file rs485_manager.h
 * @brief RS485 communication with Luxpower inverter
 *
 * @license GPL-3.0
 * @author OpenLux Contributors
 */

#pragma once

#include <Arduino.h>

#include <cstring>
#include <vector>

#include <HardwareSerial.h>

/**
 * @brief Luxpower RS485 Protocol Constants
 *
 * Based on RS485 sniffing of actual Luxpower WiFi dongle communication.
 * Protocol is Modbus-like but NOT standard Modbus RTU!
 */

// Protocol constants
static constexpr uint8_t LUX_DEVICE_ADDR_REQUEST = 0x00;  // Requests use 0x00
static constexpr uint8_t LUX_DEVICE_ADDR_RESPONSE = 0x01; // Responses use 0x01
static constexpr size_t LUX_SERIAL_NUMBER_LENGTH = 10;
static constexpr uint16_t LUX_INVERTER_SN_START_REG = 115;
static constexpr uint8_t LUX_INVERTER_SN_REG_COUNT = 5;
static constexpr size_t LUX_MAX_REGISTERS = 127;
static constexpr size_t LUX_MIN_REQUEST_SIZE = 18;
static constexpr size_t LUX_MIN_RESPONSE_SIZE = 18;       // Normal response minimum
static constexpr size_t LUX_MIN_EXCEPTION_SIZE = 15;      // Exception response minimum
static constexpr uint32_t LUX_INTER_FRAME_DELAY_MS = 50;  // Time between frames
static constexpr uint32_t LUX_RESPONSE_TIMEOUT_MS = 1000; // Response timeout

// Packet structure offsets
// Request/Response format: [addr:1][func:1][serial:10][start:2][count/value:2][crc:2]
namespace LuxProtocolOffsets {
static constexpr size_t ADDR = 0;            // Device address (1 byte)
static constexpr size_t FUNC = 1;            // Function code (1 byte)
static constexpr size_t SERIAL_NUM = 2;      // Serial number start (10 bytes)
static constexpr size_t START_REG = 12;      // Start register address (2 bytes, LE)
static constexpr size_t COUNT_OR_VALUE = 14; // Register count or value (2 bytes, LE)
static constexpr size_t BYTE_COUNT = 16;     // Byte count for write multi (1 byte)
static constexpr size_t DATA_START = 17;     // Data start for write multi
static constexpr size_t CRC_MIN_PACKET = 16; // CRC position for 18-byte packets
static constexpr size_t EXCEPTION_CODE = 14; // Exception code position (exception responses)
} // namespace LuxProtocolOffsets

// Function codes (Modbus compatible)
enum class LuxFunctionCode : uint8_t {
    READ_HOLDING = 0x03, // Read Holding Registers
    READ_INPUT = 0x04,   // Read Input Registers
    WRITE_SINGLE = 0x06, // Write Single Register
    WRITE_MULTI = 0x10   // Write Multiple Registers
};

/**
 * @brief Parse result structure
 */
struct LuxParseResult {
    bool success = false;
    LuxFunctionCode function_code = LuxFunctionCode::READ_INPUT;
    uint8_t serial_number[LUX_SERIAL_NUMBER_LENGTH];
    uint16_t start_address = 0;
    uint8_t register_count = 0;
    std::vector<uint16_t> register_values;
    String error_message;

    LuxParseResult() { memset(serial_number, 0, LUX_SERIAL_NUMBER_LENGTH); }
};

/**
 * @brief Luxpower Protocol Helper Class
 *
 * Handles packet creation, parsing, and CRC calculation
 */
class LuxProtocol {
  public:
    // CRC16 Modbus calculation
    static uint16_t calculate_crc16(const uint8_t* data, size_t length);

    // Create request packets
    static bool create_read_request(std::vector<uint8_t>& packet, LuxFunctionCode func,
                                    uint16_t start_reg, uint16_t count,
                                    const String& serial_number = "");

    static bool create_write_request(std::vector<uint8_t>& packet, uint16_t start_reg,
                                     const std::vector<uint16_t>& values,
                                     const String& serial_number = "");

    // Parse response
    static LuxParseResult parse_response(const uint8_t* data, size_t length);

    // Validation
    static bool is_valid_request(const uint8_t* data, size_t length);
    static bool is_valid_response(const uint8_t* data, size_t length);
    static bool is_request(const uint8_t* data, size_t length);

    // Helper functions
    static String serial_to_string(const uint8_t* serial);
    static void string_to_serial(const String& str, uint8_t* serial);

    // Byte order helpers (little-endian)
    static uint16_t parse_little_endian_uint16(const uint8_t* data, size_t offset);
    static void write_little_endian_uint16(uint8_t* data, size_t offset, uint16_t value);

    // Debug helpers
    static String format_hex(const uint8_t* data, size_t length);
};

/**
 * @brief RS485 Interface Manager
 *
 * Handles UART communication with Luxpower inverter via RS485
 */
class RS485Manager {
  public:
    static RS485Manager& getInstance();

    // Lifecycle
    void begin(HardwareSerial& serial, int8_t tx_pin, int8_t rx_pin, int8_t de_pin = -1,
               uint32_t baud_rate = 19200);
    void loop();
    void probe_inverter_serial();

    // Communication
    bool send_read_request(LuxFunctionCode func, uint16_t start_reg, uint16_t count);
    bool send_write_request(uint16_t start_reg, const std::vector<uint16_t>& values);

    // Configuration
    void set_serial_number(const String& serial) { serial_number_ = serial; }
    void set_response_timeout(uint32_t timeout_ms) { response_timeout_ms_ = timeout_ms; }

    // Status
    bool is_initialized() const { return initialized_; }
    bool is_waiting_response() const { return waiting_response_; }
    const LuxParseResult& get_last_result() const { return last_result_; }
    const std::vector<uint8_t>& get_last_raw_response() const { return last_raw_response_; }
    const String& get_detected_inverter_serial() const { return inverter_serial_detected_; }
    bool is_inverter_link_up() const { return inverter_link_ok_; }

    // Statistics
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

    void request_inverter_serial_probe();
    void send_packet(const std::vector<uint8_t>& packet);
    void process_incoming_data();
    void handle_response(const std::vector<uint8_t>& data);
    void handle_timeout();
    bool should_ignore_packet(const std::vector<uint8_t>& data);

    // Helper function to convert function code to string
    static const char* function_code_to_string(LuxFunctionCode func);

    HardwareSerial* serial_ = nullptr;
    int8_t de_pin_ = -1; // DE/RE pin for RS485 transceiver (-1 if not used)
    bool initialized_ = false;

    String serial_number_;
    uint32_t response_timeout_ms_ = LUX_RESPONSE_TIMEOUT_MS;

    // State
    bool waiting_response_ = false;
    LuxFunctionCode expected_function_code_ = LuxFunctionCode::READ_INPUT;
    uint16_t expected_start_reg_ = 0;
    unsigned long last_tx_time_ = 0;

    unsigned long last_rx_time_ = 0;
    std::vector<uint8_t> rx_buffer_;
    std::vector<uint8_t> last_raw_response_; // Raw response for error forwarding
    LuxParseResult last_result_;
    String inverter_serial_detected_;
    bool serial_probe_pending_ = false;
    bool inverter_link_ok_ = false;
    uint32_t next_serial_probe_ms_ = 0;
    uint32_t serial_probe_backoff_ms_ = 0;

    // Statistics
    uint32_t total_requests_ = 0;
    uint32_t successful_responses_ = 0;
    uint32_t failed_responses_ = 0;
    uint32_t timeout_count_ = 0;
    uint32_t ignored_packets_ = 0;
};
