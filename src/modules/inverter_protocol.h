/**
 * @file inverter_protocol.h
 * @brief Inverter protocol definitions and packet handling
 *
 * This module defines:
 * - Protocol constants (addresses, sizes, offsets)
 * - Function codes and result structures
 * - Packet creation and parsing functions
 * - Multi-frame handling for shared RS485 bus
 *
 * Protocol is Modbus-like but NOT standard Modbus RTU!
 * Key differences:
 * - 10-byte serial number field after function code
 * - Little-endian byte order for registers
 * - Address 0x00 for requests, 0x01 for responses
 *
 * @license GPL-3.0
 * @author OpenLux Contributors
 */

#pragma once

#include <Arduino.h>

#include <cstring>
#include <vector>

// ============================================================================
// Protocol Constants
// ============================================================================

// Device addresses
static constexpr uint8_t MODBUS_DEVICE_ADDR_REQUEST = 0x00;  // Requests use 0x00
static constexpr uint8_t MODBUS_DEVICE_ADDR_RESPONSE = 0x01; // Responses use 0x01

// Serial number
static constexpr size_t MODBUS_SERIAL_NUMBER_LENGTH = 10;

// Inverter serial number register location
static constexpr uint16_t MODBUS_INVERTER_SN_START_REG = 115;
static constexpr uint8_t MODBUS_INVERTER_SN_REG_COUNT = 5;

// Protocol limits
static constexpr size_t MODBUS_MAX_REGISTERS = 127;
static constexpr size_t MODBUS_MIN_REQUEST_SIZE = 18;     // Minimum request packet size
static constexpr size_t MODBUS_MIN_RESPONSE_SIZE = 17;    // Minimum response size (no data)
static constexpr size_t MODBUS_MIN_EXCEPTION_SIZE = 17;   // Exception response size
static constexpr size_t MODBUS_MAX_RX_BUFFER_SIZE = 1024; // Maximum receive buffer

// Timing
static constexpr uint32_t MODBUS_RESPONSE_TIMEOUT_MS = 1000;
static constexpr uint32_t MODBUS_INTER_FRAME_DELAY_MS = 50;

// ============================================================================
// Protocol Offsets
// ============================================================================

/**
 * @brief Byte offsets within Inverter packets
 */
namespace InverterProtocolOffsets {
static constexpr size_t ADDR = 0;            // Device address
static constexpr size_t FUNC = 1;            // Function code
static constexpr size_t SERIAL_NUM = 2;      // Serial number (10 bytes)
static constexpr size_t START_REG = 12;      // Start register
static constexpr size_t COUNT_OR_VALUE = 14; // Count (read) or value (write single)
static constexpr size_t BYTE_COUNT = 16;     // Byte count (write multi)
static constexpr size_t DATA_START = 17;     // Data start (write multi)
static constexpr size_t CRC_MIN_PACKET = 16; // CRC offset for min packet
static constexpr size_t EXCEPTION_CODE = 14; // Exception code offset
} // namespace InverterProtocolOffsets

// ============================================================================
// Function Codes
// ============================================================================

/**
 * @brief Inverter/Modbus function codes
 */
enum class ModbusFunctionCode : uint8_t {
    READ_HOLDING = 0x03, // Read holding registers (R/W config)
    READ_INPUT = 0x04,   // Read input registers (R/O status)
    WRITE_SINGLE = 0x06, // Write single register
    WRITE_MULTI = 0x10   // Write multiple registers
};

// ============================================================================
// Result Structures
// ============================================================================

/**
 * @brief Result of parsing a Inverter response
 */
struct ParseResult {
    bool success = false;
    ModbusFunctionCode function_code = ModbusFunctionCode::READ_INPUT;
    uint16_t start_address = 0;
    uint16_t register_count = 0;
    uint8_t serial_number[MODBUS_SERIAL_NUMBER_LENGTH];
    std::vector<uint16_t> register_values;
    String error_message;

    ParseResult() { memset(serial_number, 0, MODBUS_SERIAL_NUMBER_LENGTH); }
};

/**
 * @brief Frame information for multi-frame parsing
 *
 * When the RS485 bus has multiple masters (OpenLux + WiFi dongle),
 * we may receive concatenated frames: [THEIR_REQ][THEIR_RESP][OUR_RESP]
 * This struct helps identify and extract the correct frame.
 */
struct FrameInfo {
    size_t offset;      // Offset within buffer
    size_t length;      // Frame length in bytes
    bool is_request;    // True if address=0x00
    ParseResult result; // Parsed result (for responses only)
};

// ============================================================================
// InverterProtocol Class
// ============================================================================

/**
 * @brief Protocol helper class for Inverter communication
 *
 * Provides static methods for:
 * - Packet creation (read/write requests)
 * - Response parsing
 * - CRC calculation
 * - Multi-frame handling
 */
class InverterProtocol {
  public:
    // ========== CRC ==========
    static uint16_t calculate_crc16(const uint8_t* data, size_t length);

    // ========== Request Creation ==========
    static bool create_read_request(std::vector<uint8_t>& packet, ModbusFunctionCode func,
                                    uint16_t start_reg, uint16_t count,
                                    const String& serial_number = "");

    static bool create_write_request(std::vector<uint8_t>& packet, uint16_t start_reg,
                                     const std::vector<uint16_t>& values,
                                     const String& serial_number = "");

    // ========== Response Parsing ==========
    static ParseResult parse_response(const uint8_t* data, size_t length);

    // ========== Validation ==========
    static bool is_request(const uint8_t* data, size_t length);
    static bool is_valid_response(const uint8_t* data, size_t length);

    // ========== Multi-Frame Handling ==========
    static size_t calculate_frame_length(const uint8_t* frame, size_t available);
    static std::vector<FrameInfo> parse_all_frames(const std::vector<uint8_t>& data);
    static int find_matching_response_index(const std::vector<FrameInfo>& frames,
                                            ModbusFunctionCode expected_func,
                                            uint16_t expected_start_reg);

    // ========== Serial Number Helpers ==========
    static String serial_to_string(const uint8_t* serial);
    static void string_to_serial(const String& str, uint8_t* serial);

    // ========== Byte Order Helpers ==========
    static uint16_t parse_little_endian_uint16(const uint8_t* data, size_t offset);
    static void write_little_endian_uint16(uint8_t* data, size_t offset, uint16_t value);

    // ========== Debug ==========
    static String format_hex(const uint8_t* data, size_t length);
};
