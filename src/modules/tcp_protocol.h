/**
 * @file tcp_protocol.h
 * @brief WiFi protocol (A1 1A format) parser/builder
 *
 * @license GPL-3.0
 * @author OpenLux Contributors
 */
#pragma once

#include <Arduino.h>

#include <cstring>
#include <vector>

/**
 * @brief TCP Protocol (A1 1A format)
 *
 * Protocol used by dongle for TCP communication (port 8000).
 * This wraps the RS485 Modbus-like protocol in a TCP packet.
 *
 * Works over any TCP/IP transport:
 * - WiFi (ESP32 standard)
 * - Ethernet (ESP32 with W5500, LAN8720, etc.)
 * - Any network interface supporting TCP
 */

// Protocol constants
static constexpr uint8_t TCP_PROTO_PREFIX[2] = {0xA1, 0x1A}; // Magic header
static constexpr uint16_t TCP_PROTO_VERSION_REQUEST = 2;     // Requests use protocol 2!
static constexpr uint16_t TCP_PROTO_VERSION_RESPONSE = 5;    // Responses use protocol 5!
static constexpr uint8_t TCP_PROTO_RESERVED = 1;
static constexpr uint8_t TCP_PROTO_FUNC_TRANSLATED = 194; // 0xC2
static constexpr size_t TCP_PROTO_DONGLE_SERIAL_LEN = 10;
static constexpr size_t TCP_PROTO_MIN_REQUEST_SIZE = 38;
static constexpr size_t TCP_PROTO_MIN_RESPONSE_SIZE = 37;
static constexpr uint16_t TCP_PROTO_REQUEST_FRAME_LENGTH = 32;
static constexpr uint16_t TCP_PROTO_REQUEST_DATA_LENGTH = 18;
static constexpr size_t TCP_PROTO_MAX_REGISTERS = 127; // Max registers per request (new inverters)

// TCP Packet structure offsets
// Format:
// [prefix:2][protocol:2][frame_len:2][reserved:1][tcp_func:1][dongle_serial:10][data_len:2][data_frame:N][crc:2]
namespace TcpProtocolOffsets {
static constexpr size_t PREFIX = 0;            // A1 1A magic header (2 bytes)
static constexpr size_t PROTOCOL = 2;          // Protocol version (2 bytes, LE)
static constexpr size_t FRAME_LEN = 4;         // Frame length (2 bytes, LE)
static constexpr size_t RESERVED = 6;          // Reserved byte (1 byte)
static constexpr size_t TCP_FUNC = 7;          // TCP function (1 byte, 0xC2)
static constexpr size_t DONGLE_SERIAL_NUM = 8; // Dongle serial number (10 bytes)
static constexpr size_t DATA_LEN = 18;         // Data frame length (2 bytes, LE)
static constexpr size_t DATA_FRAME = 20;       // Start of data frame

// Data frame offsets (relative to DATA_FRAME offset)
static constexpr size_t ACTION = 0;      // Action byte (relative to data frame)
static constexpr size_t MODBUS_FUNC = 1; // Modbus function code (relative to data frame)
static constexpr size_t INVERTER_SERIAL_NUM =
    2;                                     // Inverter serial (10 bytes, relative to data frame)
static constexpr size_t START_REG = 12;    // Start register (2 bytes, LE, relative to data frame)
static constexpr size_t COUNT_VALUE = 14;  // Count or value (2 bytes, LE, relative to data frame)
static constexpr size_t BYTE_COUNT = 16;   // Byte count for write multi (relative to data frame)
static constexpr size_t VALUES_START = 17; // Values start for write multi (relative to data frame)

// Absolute offsets (from packet start) for common access
static constexpr size_t ABS_ACTION = DATA_FRAME + ACTION;                           // 20
static constexpr size_t ABS_MODBUS_FUNC = DATA_FRAME + MODBUS_FUNC;                 // 21
static constexpr size_t ABS_INVERTER_SERIAL_NUM = DATA_FRAME + INVERTER_SERIAL_NUM; // 22
static constexpr size_t ABS_START_REG = DATA_FRAME + START_REG;                     // 32
static constexpr size_t ABS_COUNT_VALUE = DATA_FRAME + COUNT_VALUE;                 // 34
static constexpr size_t ABS_BYTE_COUNT = DATA_FRAME + BYTE_COUNT;                   // 36
static constexpr size_t ABS_VALUES_START = DATA_FRAME + VALUES_START;               // 37
} // namespace TcpProtocolOffsets


/**
 * @brief TCP Protocol Request Structure
 *
 * Total: 38 bytes (not 40!)
 *
 * Breakdown:
 * [0-1]   Prefix (A1 1A)                     = 2 bytes
 * [2-3]   Protocol (little-endian)           = 2 bytes
 * [4-5]   Frame Length (little-endian)       = 2 bytes
 * [6]     Reserved                           = 1 byte
 * [7]     TCP Function                       = 1 byte
 * [8-17]  Dongle Serial                      = 10 bytes
 * [18-19] Data Length (little-endian)        = 2 bytes
 * [20]    Action                             = 1 byte
 * [21]    Function Code                      = 1 byte
 * [22-31] Inverter Serial                    = 10 bytes
 * [32-33] Start Register (little-endian)     = 2 bytes
 * [34-35] Register Count (little-endian)     = 2 bytes
 * [36-37] CRC16 (little-endian)              = 2 bytes
 * ────────────────────────────────────────────────────
 * TOTAL                                      = 38 bytes
 */
struct TcpProtocolRequest {
    uint8_t prefix[2];         // [0-1]   = 0xA1 0x1A
    uint16_t protocol;         // [2-3]   = 1 (little-endian)
    uint16_t frame_length;     // [4-5]   = 32 (little-endian)
    uint8_t reserved;          // [6]     = 1
    uint8_t tcp_function;      // [7]     = 194 (TRANSLATED_DATA)
    uint8_t dongle_serial[10]; // [8-17]  = Dongle serial number
    uint16_t data_length;      // [18-19] = 18 (little-endian)
    // Data frame [20-37] (16 bytes):
    uint8_t action;              // [20]    = 0 (write to inverter)
    uint8_t function_code;       // [21]    = 0x03/0x04
    uint8_t inverter_serial[10]; // [22-31] = Inverter serial
    uint16_t start_register;     // [32-33] = Start register (little-endian)
    uint16_t register_count;     // [34-35] = Register count (little-endian)
    uint16_t crc;                // [38-39] = CRC16 of data frame (little-endian)
} __attribute__((packed));

/**
 * @brief TCP Protocol Response Header
 *
 * Variable length: minimum 37 bytes + register data
 */
struct TcpProtocolResponseHeader {
    uint8_t prefix[2];         // [0-1]   = 0xA1 0x1A
    uint16_t protocol;         // [2-3]   = 1
    uint16_t frame_length;     // [4-5]   = variable
    uint8_t reserved;          // [6]     = 1
    uint8_t tcp_function;      // [7]     = 194
    uint8_t dongle_serial[10]; // [8-17]  = Dongle serial
    uint16_t data_length;      // [18-19] = variable
    // Data frame starts at [20]
} __attribute__((packed));

/**
 * @brief Parse result for WiFi protocol packets
 */
struct TcpParseResult {
    bool success = false;
    String error_message;

    // Request fields
    uint8_t dongle_serial[TCP_PROTO_DONGLE_SERIAL_LEN];
    uint8_t inverter_serial[TCP_PROTO_DONGLE_SERIAL_LEN];
    uint8_t function_code = 0;
    uint16_t start_register = 0;
    uint16_t register_count = 0;

    // For write operations (0x06 and 0x10)
    bool is_write_operation = false;
    std::vector<uint16_t> write_values; // Values to write (for 0x06: 1 value, for 0x10: multiple)

    // For building RS485 packet
    std::vector<uint8_t> rs485_packet;

    TcpParseResult() {
        memset(dongle_serial, 0, TCP_PROTO_DONGLE_SERIAL_LEN);
        memset(inverter_serial, 0, TCP_PROTO_DONGLE_SERIAL_LEN);
    }
};

/**
 * @brief TCP Protocol Parser and Builder
 *
 * Handles conversion between TCP protocol (A1 1A) and RS485 protocol.
 * Works over WiFi, Ethernet, or any TCP/IP transport.
 */
class TcpProtocol {
  public:
    // Parse WiFi request packet and extract RS485 data
    static TcpParseResult parse_request(const uint8_t* data, size_t length);

    // Build WiFi response packet from RS485 response
    static bool build_response(std::vector<uint8_t>& wifi_packet, const uint8_t* rs485_response,
                               size_t rs485_length, const uint8_t* dongle_serial);

    // Validation
    static bool is_valid_request(const uint8_t* data, size_t length);
    static bool is_valid_response(const uint8_t* data, size_t length);

    // CRC calculation (Modbus CRC16)
    static uint16_t calculate_crc(const uint8_t* data, size_t length);

    // Helper functions
    static String format_serial(const uint8_t* serial);
    static void copy_serial(const String& str, uint8_t* serial);
    static String format_hex(const uint8_t* data, size_t length);

    // Byte order helpers (little-endian)
    static uint16_t parse_little_endian_uint16(const uint8_t* data, size_t offset);
    static void write_little_endian_uint16(uint8_t* data, size_t offset, uint16_t value);
};
