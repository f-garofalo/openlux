/**
 * @file tcp_protocol.cpp
 * @brief WiFi protocol implementation
 *
 * @license GPL-3.0
 * @author OpenLux Contributors
 */
#include "tcp_protocol.h"

#include "logger.h"
#include "rs485_manager.h"
#include "utils/crc16.h"
#include "utils/serial_utils.h"

static const char* TAG = "tcp_proto";

// ============================================================================
// CRC16 Modbus (same as RS485)
// ============================================================================

uint16_t TcpProtocol::calculate_crc(const uint8_t* data, size_t length) {
    return CRC16::calculate(data, length);
}

// Internal helpers to build RS485 packets from parsed TCP frames
namespace {
void build_rs485_write_single(TcpParseResult& result) {
    result.rs485_packet.resize(MODBUS_MIN_REQUEST_SIZE);
    auto& pkt = result.rs485_packet;

    pkt[InverterProtocolOffsets::ADDR] = MODBUS_DEVICE_ADDR_REQUEST;
    pkt[InverterProtocolOffsets::FUNC] = static_cast<uint8_t>(ModbusFunctionCode::WRITE_SINGLE);
    memcpy(&pkt[InverterProtocolOffsets::SERIAL_NUM], result.inverter_serial,
           TCP_PROTO_DONGLE_SERIAL_LEN);
    TcpProtocol::write_little_endian_uint16(&pkt[0], InverterProtocolOffsets::START_REG,
                                            result.start_register);
    TcpProtocol::write_little_endian_uint16(&pkt[0], InverterProtocolOffsets::COUNT_OR_VALUE,
                                            result.write_values[0]);

    uint16_t crc = TcpProtocol::calculate_crc(&pkt[0], InverterProtocolOffsets::CRC_MIN_PACKET);
    TcpProtocol::write_little_endian_uint16(&pkt[0], InverterProtocolOffsets::CRC_MIN_PACKET, crc);
}

void build_rs485_write_multi(TcpParseResult& result) {
    size_t rs485_size = InverterProtocolOffsets::DATA_START + (result.register_count * 2) + 2;
    result.rs485_packet.resize(rs485_size);
    auto& pkt = result.rs485_packet;

    pkt[InverterProtocolOffsets::ADDR] = MODBUS_DEVICE_ADDR_REQUEST;
    pkt[InverterProtocolOffsets::FUNC] = static_cast<uint8_t>(ModbusFunctionCode::WRITE_MULTI);
    memcpy(&pkt[InverterProtocolOffsets::SERIAL_NUM], result.inverter_serial,
           TCP_PROTO_DONGLE_SERIAL_LEN);
    TcpProtocol::write_little_endian_uint16(&pkt[0], InverterProtocolOffsets::START_REG,
                                            result.start_register);
    TcpProtocol::write_little_endian_uint16(&pkt[0], InverterProtocolOffsets::COUNT_OR_VALUE,
                                            result.register_count);
    pkt[InverterProtocolOffsets::BYTE_COUNT] = result.register_count * 2;

    for (size_t i = 0; i < result.write_values.size(); i++) {
        TcpProtocol::write_little_endian_uint16(
            &pkt[0], InverterProtocolOffsets::DATA_START + (i * 2), result.write_values[i]);
    }

    uint16_t crc = TcpProtocol::calculate_crc(&pkt[0], rs485_size - 2);
    TcpProtocol::write_little_endian_uint16(&pkt[0], rs485_size - 2, crc);
}

void build_rs485_read(TcpParseResult& result) {
    result.rs485_packet.resize(MODBUS_MIN_REQUEST_SIZE);
    auto& pkt = result.rs485_packet;

    pkt[InverterProtocolOffsets::ADDR] = MODBUS_DEVICE_ADDR_REQUEST;
    pkt[InverterProtocolOffsets::FUNC] = result.function_code;
    memcpy(&pkt[InverterProtocolOffsets::SERIAL_NUM], result.inverter_serial,
           TCP_PROTO_DONGLE_SERIAL_LEN);
    TcpProtocol::write_little_endian_uint16(&pkt[0], InverterProtocolOffsets::START_REG,
                                            result.start_register);
    TcpProtocol::write_little_endian_uint16(&pkt[0], InverterProtocolOffsets::COUNT_OR_VALUE,
                                            result.register_count);

    uint16_t crc = TcpProtocol::calculate_crc(&pkt[0], InverterProtocolOffsets::CRC_MIN_PACKET);
    TcpProtocol::write_little_endian_uint16(&pkt[0], InverterProtocolOffsets::CRC_MIN_PACKET, crc);
}
} // namespace

// ============================================================================
// Parse WiFi Request
// ============================================================================

TcpParseResult TcpProtocol::parse_request(const uint8_t* data, size_t length) {
    TcpParseResult result;

    // Check minimum size
    if (length < TCP_PROTO_MIN_REQUEST_SIZE) {
        result.error_message = "Packet too small";
        LOGW(TAG, "%s: got %d, expected %d", result.error_message.c_str(), length,
             TCP_PROTO_MIN_REQUEST_SIZE);
        return result;
    }

    // Check prefix (A1 1A)
    if (data[0] != TCP_PROTO_PREFIX[0] || data[1] != TCP_PROTO_PREFIX[1]) {
        result.error_message = "Invalid prefix (expected A1 1A)";
        LOGW(TAG, "%s: got %02X %02X", result.error_message.c_str(), data[0], data[1]);
        return result;
    }

    // Parse header
    uint16_t protocol = parse_little_endian_uint16(data, 2);
    uint16_t frame_length = parse_little_endian_uint16(data, 4);
    uint8_t tcp_function = data[7];

    LOGD(TAG, "Request: protocol=%d, frame_len=%d, tcp_func=%d", protocol, frame_length,
         tcp_function);

    // Check TCP function
    if (tcp_function != TCP_PROTO_FUNC_TRANSLATED) {
        result.error_message = "Unsupported TCP function";
        LOGW(TAG, "%s: got %d, expected %d", result.error_message.c_str(), tcp_function,
             TCP_PROTO_FUNC_TRANSLATED);
        return result;
    }

    // Extract dongle serial
    memcpy(result.dongle_serial, &data[8], TCP_PROTO_DONGLE_SERIAL_LEN);

    // Parse data frame (starts at byte 20)
    // uint8_t action = data[TcpProtocolOffsets::ABS_ACTION];
    result.function_code = data[TcpProtocolOffsets::ABS_MODBUS_FUNC];
    memcpy(result.inverter_serial, &data[TcpProtocolOffsets::ABS_INVERTER_SERIAL_NUM],
           TCP_PROTO_DONGLE_SERIAL_LEN);
    result.start_register = parse_little_endian_uint16(data, TcpProtocolOffsets::ABS_START_REG);

    // Determine if this is a write operation
    result.is_write_operation = (result.function_code == 0x06 || result.function_code == 0x10);

    uint16_t data_frame_size = 0;

    if (result.is_write_operation) {
        // Write operations have different structure
        if (result.function_code == 0x06) {
            // Write Single Register (0x06)
            // WiFi: header(20) + data_frame(18)
            data_frame_size = TCP_PROTO_REQUEST_DATA_LENGTH;
            size_t min_len = TcpProtocolOffsets::DATA_FRAME + data_frame_size;
            if (length < min_len) {
                result.error_message = "Write single packet too small";
                return result;
            }

            uint16_t register_value =
                parse_little_endian_uint16(data, TcpProtocolOffsets::ABS_COUNT_VALUE);
            result.write_values.push_back(register_value);
            result.register_count = 1;

            LOGD(TAG, "Write Single: reg=%d, value=0x%04X", result.start_register, register_value);

        } else if (result.function_code == 0x10) {
            // Write Multiple Registers (0x10)
            // WiFi: data_frame = 17 (header) + bytecount (data)
            result.register_count =
                parse_little_endian_uint16(data, TcpProtocolOffsets::ABS_COUNT_VALUE);
            uint8_t byte_count = data[TcpProtocolOffsets::ABS_BYTE_COUNT];
            data_frame_size = TcpProtocolOffsets::VALUES_START + byte_count; // fixed header + data

            // Validate register count (max 127 for new inverters)
            if (result.register_count == 0 || result.register_count > TCP_PROTO_MAX_REGISTERS) {
                result.error_message = "Invalid register count for write";
                LOGE(TAG, "%s: %d (max %d)", result.error_message.c_str(), result.register_count,
                     TCP_PROTO_MAX_REGISTERS);
                return result;
            }

            size_t min_len = TcpProtocolOffsets::DATA_FRAME + data_frame_size;
            if (length < min_len) {
                result.error_message = "Write multiple packet too small";
                return result;
            }

            // Parse register values (little-endian, 2 bytes each)
            for (uint16_t i = 0; i < result.register_count; i++) {
                uint16_t value = parse_little_endian_uint16(
                    data, TcpProtocolOffsets::ABS_VALUES_START + (i * 2));
                result.write_values.push_back(value);
            }

            LOGD(TAG, "Write Multiple: start=%d, count=%d, bytes=%d", result.start_register,
                 result.register_count, byte_count);
        }
    } else {
        // Read operations (0x03, 0x04)
        // WiFi Read: [action][func][serial:10][start:2][count:2][crc:2] = 18 bytes
        data_frame_size = 18;
        result.register_count =
            parse_little_endian_uint16(data, TcpProtocolOffsets::ABS_COUNT_VALUE);

        LOGD(TAG, "Read: func=0x%02X, start=%d, count=%d", result.function_code,
             result.start_register, result.register_count);
    }

    // Validate register count (max 127 for new inverters)
    if (result.register_count == 0 || result.register_count > TCP_PROTO_MAX_REGISTERS) {
        result.error_message = "Invalid register count";
        LOGE(TAG, "%s: %d (max %d)", result.error_message.c_str(), result.register_count,
             TCP_PROTO_MAX_REGISTERS);
        return result;
    }

    // Verify CRC of data frame
    uint16_t crc_offset =
        TcpProtocolOffsets::DATA_FRAME + data_frame_size - 2; // CRC is last 2 bytes of data frame
    uint16_t calculated_crc =
        calculate_crc(&data[TcpProtocolOffsets::DATA_FRAME], data_frame_size - 2);
    uint16_t received_crc = parse_little_endian_uint16(data, crc_offset);

    if (calculated_crc != received_crc) {
        result.error_message = "CRC mismatch";
        LOGW(TAG, "%s: calculated=0x%04X, received=0x%04X", result.error_message.c_str(),
             calculated_crc, received_crc);
        return result;
    }

    // Build RS485 packet from the data frame
    if (result.is_write_operation) {
        if (result.function_code == 0x06) {
            build_rs485_write_single(result);

        } else if (result.function_code == 0x10) {
            build_rs485_write_multi(result);
        }
    } else {
        // Read operations - existing code
        build_rs485_read(result);
    }

    result.success = true;

    if (result.is_write_operation) {
        LOGI(TAG, "✓ TCP write parsed: func=0x%02X reg=%d count=%d", result.function_code,
             result.start_register, result.register_count);
    } else {
        LOGI(TAG, "✓ TCP read parsed: func=0x%02X start=%d count=%d", result.function_code,
             result.start_register, result.register_count);
    }

    return result;
}

// ============================================================================
// Build WiFi Response
// ============================================================================

bool TcpProtocol::build_response(std::vector<uint8_t>& wifi_packet, const uint8_t* rs485_response,
                                 size_t rs485_length, const uint8_t* dongle_serial) {
    // Check if this is an exception response
    uint8_t func = rs485_response[1];
    bool is_exception = (func & 0x80) != 0;

    // Exception responses are 17 bytes, normal responses are 18+ bytes
    size_t min_size = is_exception ? 17 : 18;
    if (rs485_length < min_size) {
        LOGE(TAG, "RS485 response too small: %d bytes (expected %s %d)", rs485_length,
             is_exception ? "exception" : "at least", min_size);
        return false;
    }

    // RS485 response format:
    // Normal:    [0] addr, [1] func, [2-11] serial, [12-13] start, [14] byte_count, [15...] data,
    // [...] crc Exception: [0] addr, [1] func|0x80, [2-11] serial, [12-13] reg, [14]
    // exception_code, [15-16] crc

    uint16_t start_reg =
        parse_little_endian_uint16(rs485_response, InverterProtocolOffsets::START_REG);
    uint8_t byte_count = is_exception ? 0 : rs485_response[InverterProtocolOffsets::COUNT_OR_VALUE];

    if (is_exception) {
        uint8_t exception_code = rs485_response[InverterProtocolOffsets::EXCEPTION_CODE];
        LOGW(TAG, "Building TCP exception: func=0x%02X reg=%d code=0x%02X", func, start_reg,
             exception_code);
    }

    // Calculate frame length
    // Data frame = FULL RS485 packet (INCLUDING address!) excluding ONLY the CRC (last 2 bytes)
    size_t data_frame_size = rs485_length - 2;        // Exclude ONLY RS485 CRC, KEEP address!
    uint16_t frame_length = 14 + data_frame_size + 2; // Header(14) + Data Frame + WiFi CRC(2)

    // Build WiFi packet
    wifi_packet.clear();
    wifi_packet.resize(6 + frame_length); // Prefix(2) + Protocol(2) + FrameLen(2) + Frame(with CRC)

    size_t offset = 0;

    // Prefix (A1 1A)
    wifi_packet[offset++] = TCP_PROTO_PREFIX[0];
    wifi_packet[offset++] = TCP_PROTO_PREFIX[1];

    // Protocol (little-endian) - RESPONSE uses protocol 5!
    write_little_endian_uint16(&wifi_packet[0], offset, TCP_PROTO_VERSION_RESPONSE);
    offset += 2;

    // Frame length (little-endian)
    write_little_endian_uint16(&wifi_packet[0], offset, frame_length);
    offset += 2;

    // Reserved
    wifi_packet[offset++] = TCP_PROTO_RESERVED;

    // TCP function
    wifi_packet[offset++] = TCP_PROTO_FUNC_TRANSLATED;

    // Dongle serial
    memcpy(&wifi_packet[offset], dongle_serial, TCP_PROTO_DONGLE_SERIAL_LEN);
    offset += TCP_PROTO_DONGLE_SERIAL_LEN;

    // Data length (little-endian) - size of data frame (with address, without WiFi CRC)
    write_little_endian_uint16(&wifi_packet[0], offset, data_frame_size);
    offset += 2;

    // Data frame - Copy FULL RS485 response INCLUDING address, EXCLUDING RS485 CRC only
    // Home Assistant expects: [address][func][serial][reg][bytecount][data...]
    size_t data_frame_start = offset;
    memcpy(&wifi_packet[offset], &rs485_response[0], data_frame_size); // From [0], not [1]!
    offset += data_frame_size;

    // Calculate WiFi CRC of data frame (without RS485 CRC!)
    uint16_t crc = calculate_crc(&wifi_packet[data_frame_start], data_frame_size);
    write_little_endian_uint16(&wifi_packet[0], offset, crc);
    offset += 2;

    wifi_packet.resize(offset);

    if (is_exception) {
        uint8_t exception_code = rs485_response[InverterProtocolOffsets::EXCEPTION_CODE];
        LOGI(TAG, "✓ TCP exception resp: func=0x%02X reg=%d code=0x%02X size=%d", func, start_reg,
             exception_code, wifi_packet.size());
    } else {
        LOGI(TAG, "✓ TCP resp built: func=0x%02X start=%d bytes=%d size=%d", func, start_reg,
             byte_count, wifi_packet.size());
    }
    LOGD(TAG, "Response packet: %s", format_hex(wifi_packet.data(), wifi_packet.size()).c_str());

    return true;
}

// ============================================================================
// Validation
// ============================================================================

bool TcpProtocol::is_valid_request(const uint8_t* data, size_t length) {
    if (length < TCP_PROTO_MIN_REQUEST_SIZE) {
        return false;
    }

    // Check prefix
    if (data[0] != TCP_PROTO_PREFIX[0] || data[1] != TCP_PROTO_PREFIX[1]) {
        return false;
    }

    // Check TCP function
    if (data[7] != TCP_PROTO_FUNC_TRANSLATED) {
        return false;
    }

    return true;
}

bool TcpProtocol::is_valid_response(const uint8_t* data, size_t length) {
    if (length < TCP_PROTO_MIN_RESPONSE_SIZE) {
        return false;
    }

    // Check prefix
    if (data[0] != TCP_PROTO_PREFIX[0] || data[1] != TCP_PROTO_PREFIX[1]) {
        return false;
    }

    return true;
}

// ============================================================================
// Helper Functions
// ============================================================================

String TcpProtocol::format_serial(const uint8_t* serial) {
    return SerialUtils::format_serial(serial, TCP_PROTO_DONGLE_SERIAL_LEN);
}

void TcpProtocol::copy_serial(const String& str, uint8_t* serial) {
    SerialUtils::write_serial(serial, TCP_PROTO_DONGLE_SERIAL_LEN, str);
}

String TcpProtocol::format_hex(const uint8_t* data, size_t length) {
    String result;
    result.reserve(length * 3);
    for (size_t i = 0; i < length; i++) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X ", data[i]);
        result += buf;
    }
    return result;
}

uint16_t TcpProtocol::parse_little_endian_uint16(const uint8_t* data, size_t offset) {
    return data[offset] | (static_cast<uint16_t>(data[offset + 1]) << 8);
}

void TcpProtocol::write_little_endian_uint16(uint8_t* data, size_t offset, uint16_t value) {
    data[offset] = value & 0xFF;
    data[offset + 1] = (value >> 8) & 0xFF;
}
