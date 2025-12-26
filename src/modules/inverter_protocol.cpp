/**
 * @file inverter_protocol.cpp
 * @brief Inverter protocol implementation
 *
 * @license GPL-3.0
 * @author OpenLux Contributors
 */

#include "inverter_protocol.h"

#include "logger.h"
#include "utils/crc16.h"
#include "utils/serial_utils.h"

#include <algorithm>

static const char* TAG = "proto";

// ============================================================================
// SECTION 1: CRC Calculation
// ============================================================================

uint16_t InverterProtocol::calculate_crc16(const uint8_t* data, size_t length) {
    return CRC16::calculate(data, length);
}

// ============================================================================
// SECTION 2: Byte Order Helpers
// ============================================================================

uint16_t InverterProtocol::parse_little_endian_uint16(const uint8_t* data, size_t offset) {
    return data[offset] | (static_cast<uint16_t>(data[offset + 1]) << 8);
}

void InverterProtocol::write_little_endian_uint16(uint8_t* data, size_t offset, uint16_t value) {
    data[offset] = value & 0xFF;
    data[offset + 1] = (value >> 8) & 0xFF;
}

// ============================================================================
// SECTION 3: Serial Number Helpers
// ============================================================================

String InverterProtocol::serial_to_string(const uint8_t* serial) {
    return SerialUtils::format_serial(serial, MODBUS_SERIAL_NUMBER_LENGTH);
}

void InverterProtocol::string_to_serial(const String& str, uint8_t* serial) {
    SerialUtils::write_serial(serial, MODBUS_SERIAL_NUMBER_LENGTH, str);
}

// ============================================================================
// SECTION 4: Debug Helpers
// ============================================================================

String InverterProtocol::format_hex(const uint8_t* data, size_t length) {
    String result;
    result.reserve(length * 3);
    for (size_t i = 0; i < length; i++) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X ", data[i]);
        result += buf;
    }
    return result;
}

// ============================================================================
// SECTION 5: Request Creation - Read
// ============================================================================

/**
 * @brief Create a read request packet (function 0x03 or 0x04)
 *
 * Packet format (18 bytes):
 * [0]     Address (0x00 for request)
 * [1]     Function code (0x03=holding, 0x04=input)
 * [2-11]  Serial number (10 bytes ASCII)
 * [12-13] Start register (little-endian)
 * [14-15] Register count (little-endian)
 * [16-17] CRC16 (little-endian)
 */
bool InverterProtocol::create_read_request(std::vector<uint8_t>& packet, ModbusFunctionCode func,
                                           uint16_t start_reg, uint16_t count,
                                           const String& serial_number) {
    if (count == 0 || count > MODBUS_MAX_REGISTERS) {
        LOGE(TAG, "Invalid register count: %d (max %d)", count, MODBUS_MAX_REGISTERS);
        return false;
    }

    packet.clear();
    packet.resize(MODBUS_MIN_REQUEST_SIZE);

    // Build packet header
    packet[InverterProtocolOffsets::ADDR] = MODBUS_DEVICE_ADDR_REQUEST;
    packet[InverterProtocolOffsets::FUNC] = static_cast<uint8_t>(func);

    // Serial number (10 bytes) - zeros if not provided
    if (serial_number.length() == 0) {
        memset(&packet[InverterProtocolOffsets::SERIAL_NUM], 0x00, MODBUS_SERIAL_NUMBER_LENGTH);
    } else {
        SerialUtils::write_serial(&packet[InverterProtocolOffsets::SERIAL_NUM],
                                  MODBUS_SERIAL_NUMBER_LENGTH, serial_number);
    }

    // Register range
    write_little_endian_uint16(&packet[0], InverterProtocolOffsets::START_REG, start_reg);
    write_little_endian_uint16(&packet[0], InverterProtocolOffsets::COUNT_OR_VALUE, count);

    // CRC
    const uint16_t crc = calculate_crc16(&packet[0], InverterProtocolOffsets::CRC_MIN_PACKET);
    write_little_endian_uint16(&packet[0], InverterProtocolOffsets::CRC_MIN_PACKET, crc);

    return true;
}

// ============================================================================
// SECTION 6: Request Creation - Write
// ============================================================================

/**
 * @brief Create a write single register request (function 0x06)
 */
static bool create_write_single_request(std::vector<uint8_t>& packet, uint16_t start_reg,
                                        uint16_t value, const String& serial_number) {
    packet.resize(MODBUS_MIN_REQUEST_SIZE);

    packet[InverterProtocolOffsets::ADDR] = MODBUS_DEVICE_ADDR_REQUEST;
    packet[InverterProtocolOffsets::FUNC] = static_cast<uint8_t>(ModbusFunctionCode::WRITE_SINGLE);

    // Serial number
    if (serial_number.length() == 0) {
        memset(&packet[InverterProtocolOffsets::SERIAL_NUM], 0x00, MODBUS_SERIAL_NUMBER_LENGTH);
    } else {
        SerialUtils::write_serial(&packet[InverterProtocolOffsets::SERIAL_NUM],
                                  MODBUS_SERIAL_NUMBER_LENGTH, serial_number);
    }

    // Register and value
    InverterProtocol::write_little_endian_uint16(&packet[0], InverterProtocolOffsets::START_REG,
                                                 start_reg);
    InverterProtocol::write_little_endian_uint16(&packet[0],
                                                 InverterProtocolOffsets::COUNT_OR_VALUE, value);

    // CRC
    const uint16_t crc =
        InverterProtocol::calculate_crc16(&packet[0], InverterProtocolOffsets::CRC_MIN_PACKET);
    InverterProtocol::write_little_endian_uint16(&packet[0],
                                                 InverterProtocolOffsets::CRC_MIN_PACKET, crc);

    return true;
}

/**
 * @brief Create a write multiple registers request (function 0x10)
 */
static bool create_write_multi_request(std::vector<uint8_t>& packet, uint16_t start_reg,
                                       const std::vector<uint16_t>& values,
                                       const String& serial_number) {
    const size_t byte_count = values.size() * 2;
    const size_t packet_size = 17 + byte_count + 2; // Header(17) + data + CRC(2)

    packet.resize(packet_size);

    packet[InverterProtocolOffsets::ADDR] = MODBUS_DEVICE_ADDR_REQUEST;
    packet[InverterProtocolOffsets::FUNC] = static_cast<uint8_t>(ModbusFunctionCode::WRITE_MULTI);

    // Serial number
    if (serial_number.length() == 0) {
        memset(&packet[InverterProtocolOffsets::SERIAL_NUM], 0x00, MODBUS_SERIAL_NUMBER_LENGTH);
    } else {
        SerialUtils::write_serial(&packet[InverterProtocolOffsets::SERIAL_NUM],
                                  MODBUS_SERIAL_NUMBER_LENGTH, serial_number);
    }

    // Register range
    InverterProtocol::write_little_endian_uint16(&packet[0], InverterProtocolOffsets::START_REG,
                                                 start_reg);
    InverterProtocol::write_little_endian_uint16(
        &packet[0], InverterProtocolOffsets::COUNT_OR_VALUE, values.size());

    // Byte count and values
    packet[InverterProtocolOffsets::BYTE_COUNT] = byte_count & 0xFF;
    for (size_t i = 0; i < values.size(); i++) {
        InverterProtocol::write_little_endian_uint16(
            &packet[0], InverterProtocolOffsets::DATA_START + (i * 2), values[i]);
    }

    // CRC
    const uint16_t crc = InverterProtocol::calculate_crc16(&packet[0], packet_size - 2);
    InverterProtocol::write_little_endian_uint16(&packet[0], packet_size - 2, crc);

    return true;
}

bool InverterProtocol::create_write_request(std::vector<uint8_t>& packet, uint16_t start_reg,
                                            const std::vector<uint16_t>& values,
                                            const String& serial_number) {
    if (values.empty() || values.size() > MODBUS_MAX_REGISTERS) {
        LOGE(TAG, "Invalid register count: %d (max %d)", values.size(), MODBUS_MAX_REGISTERS);
        return false;
    }

    packet.clear();

    if (values.size() == 1) {
        return create_write_single_request(packet, start_reg, values[0], serial_number);
    } else {
        return create_write_multi_request(packet, start_reg, values, serial_number);
    }
}

// ============================================================================
// SECTION 7: Response Validation
// ============================================================================

bool InverterProtocol::is_request(const uint8_t* data, size_t length) {
    if (length < 1)
        return false;
    return data[0] == MODBUS_DEVICE_ADDR_REQUEST;
}

bool InverterProtocol::is_valid_response(const uint8_t* data, size_t length) {
    // Need at least 2 bytes for address and function code
    if (length < 2) {
        LOGW(TAG, "Response too short: %d bytes (need at least 2)", length);
        return false;
    }

    const uint8_t func = data[1];
    const bool is_exception = (func & 0x80) != 0;
    const size_t min_size = is_exception ? MODBUS_MIN_EXCEPTION_SIZE : MODBUS_MIN_RESPONSE_SIZE;

    if (length < min_size) {
        LOGW(TAG, "Response too short: %d bytes (min %d for %s)", length, min_size,
             is_exception ? "exception" : "normal");
        return false;
    }

    // Verify address is 0x01 (response)
    if (data[InverterProtocolOffsets::ADDR] != MODBUS_DEVICE_ADDR_RESPONSE) {
        LOGI(TAG, "Invalid response address: 0x%02X (expected 0x%02X)",
             data[InverterProtocolOffsets::ADDR], MODBUS_DEVICE_ADDR_RESPONSE);
        return false;
    }

    // Verify valid function code
    const uint8_t base_func = func & 0x7F;
    if (base_func != static_cast<uint8_t>(ModbusFunctionCode::READ_HOLDING) &&
        base_func != static_cast<uint8_t>(ModbusFunctionCode::READ_INPUT) &&
        base_func != static_cast<uint8_t>(ModbusFunctionCode::WRITE_SINGLE) &&
        base_func != static_cast<uint8_t>(ModbusFunctionCode::WRITE_MULTI)) {
        LOGW(TAG, "Invalid function code: 0x%02X", func);
        return false;
    }

    return true;
}

// ============================================================================
// SECTION 8: Response Parsing - Exception
// ============================================================================

/**
 * @brief Parse exception response (function code with 0x80 bit set)
 */
static ParseResult parse_exception_response(const uint8_t* data, size_t length) {
    ParseResult result;

    // Safety check: need at least 12 bytes for header
    if (length < 12) {
        result.error_message = "Exception response too short to read header";
        LOGE(TAG, "%s: got %d bytes, need at least 12", result.error_message.c_str(), length);
        return result;
    }

    const uint8_t func_byte = data[InverterProtocolOffsets::FUNC];
    result.function_code = static_cast<ModbusFunctionCode>(func_byte & 0x7F);
    memcpy(result.serial_number, &data[InverterProtocolOffsets::SERIAL_NUM],
           MODBUS_SERIAL_NUMBER_LENGTH);

    if (length >= MODBUS_MIN_EXCEPTION_SIZE) {
        const uint16_t failed_register =
            InverterProtocol::parse_little_endian_uint16(data, InverterProtocolOffsets::START_REG);
        result.start_address = failed_register;
        const uint8_t exception_code = data[InverterProtocolOffsets::EXCEPTION_CODE];

        // Map exception code to message
        const char* exception_msg;
        switch (exception_code) {
            case 0x01:
                exception_msg = "Illegal function";
                break;
            case 0x02:
                exception_msg = "Illegal data address";
                break;
            case 0x03:
                exception_msg = "Illegal data value";
                break;
            case 0x04:
                exception_msg = "Slave device failure";
                break;
            default:
                exception_msg = "Unknown exception";
                break;
        }

        result.error_message = String("Modbus Exception 0x") + String(exception_code, HEX) + ": " +
                               exception_msg + " (register " + String(failed_register) + ")";
        LOGE(TAG, "Inverter exception: func=0x%02X, reg=%d, code=0x%02X (%s)", func_byte,
             failed_register, exception_code, exception_msg);
    } else {
        result.error_message = "Modbus exception (malformed response)";
        LOGE(TAG, "Exception response too short: %d bytes", length);
    }

    return result; // success remains false
}

// ============================================================================
// SECTION 9: Response Parsing - Read
// ============================================================================

/**
 * @brief Log extra bytes when buffer contains concatenated frames
 */
static void log_extra_bytes(const uint8_t* data, size_t length, size_t frame_length) {
    const size_t extra_bytes = length - frame_length;
    LOGW(TAG, "Received %d bytes but frame is %d bytes, %d bytes extra", length, frame_length,
         extra_bytes);

    // Log up to 64 bytes of extra data for debugging
    const size_t extra_to_log = (extra_bytes < 64) ? extra_bytes : 64;
    LOGW(TAG, "   Extra data [%d bytes]: %s%s", extra_bytes,
         InverterProtocol::format_hex(data + frame_length, extra_to_log).c_str(),
         extra_bytes > 64 ? "..." : "");
}

/**
 * @brief Parse read response (function 0x03 or 0x04)
 *
 * Response format (17 + byte_count bytes):
 * [0]     Address (0x01)
 * [1]     Function code
 * [2-11]  Serial number
 * [12-13] Start address (echoed)
 * [14]    Byte count
 * [15+]   Data (byte_count bytes)
 * [last2] CRC16
 */
static ParseResult parse_read_response(const uint8_t* data, size_t length, uint8_t func_byte) {
    ParseResult result;
    result.function_code = static_cast<ModbusFunctionCode>(func_byte);

    // Safety check: need at least 15 bytes to read header including byte_count
    if (length < 15) {
        result.error_message = "Response packet too short to read header";
        LOGE(TAG, "%s: got %d bytes, need at least 15 for func 0x%02X",
             result.error_message.c_str(), length, func_byte);
        return result;
    }

    // Parse header
    memcpy(result.serial_number, &data[InverterProtocolOffsets::SERIAL_NUM],
           MODBUS_SERIAL_NUMBER_LENGTH);
    result.start_address =
        InverterProtocol::parse_little_endian_uint16(data, InverterProtocolOffsets::START_REG);

    const uint8_t byte_count = data[InverterProtocolOffsets::COUNT_OR_VALUE];
    const size_t frame_length = 17 + byte_count;

    // Verify we have complete frame
    if (length < frame_length) {
        result.error_message = "Response packet too short";
        LOGE(TAG, "%s: got %d, expected %d for func 0x%02X (byte_count=%d)",
             result.error_message.c_str(), length, frame_length, func_byte, byte_count);
        return result;
    }

    // Log extra bytes (concatenated frames from other master)
    if (length > frame_length) {
        log_extra_bytes(data, length, frame_length);
    }

    // Verify CRC (calculate on frame_length, not total buffer!)
    const uint16_t calculated_crc = InverterProtocol::calculate_crc16(data, frame_length - 2);
    const uint16_t received_crc =
        InverterProtocol::parse_little_endian_uint16(data, frame_length - 2);
    LOGD(TAG, "CRC Check: calculated=0x%04X, received=0x%04X", calculated_crc, received_crc);

    if (calculated_crc != received_crc) {
        result.error_message = "CRC mismatch";
        LOGW(TAG, "%s: calculated=0x%04X, received=0x%04X", result.error_message.c_str(),
             calculated_crc, received_crc);
        LOGW(TAG, "   Packet [%d bytes]: %s", length,
             InverterProtocol::format_hex(data, std::min(length, (size_t) 32)).c_str());
        // Continue parsing anyway - sometimes CRC errors are transient
    }

    // Extract register values
    result.register_count = byte_count / 2;
    result.register_values.reserve(result.register_count);

    const size_t data_offset = InverterProtocolOffsets::COUNT_OR_VALUE + 1;
    for (size_t i = 0; i < result.register_count; i++) {
        const uint16_t value =
            InverterProtocol::parse_little_endian_uint16(data, data_offset + (i * 2));
        result.register_values.push_back(value);
    }

    result.success = true;
    return result;
}

// ============================================================================
// SECTION 10: Response Parsing - Write
// ============================================================================

/**
 * @brief Parse write single response (function 0x06)
 */
static ParseResult parse_write_single_response(const uint8_t* data, size_t length) {
    ParseResult result;
    result.function_code = ModbusFunctionCode::WRITE_SINGLE;

    const size_t expected_length = 18;
    if (length < expected_length) {
        result.error_message = "Response packet too short";
        LOGE(TAG, "%s: got %d, expected %d for func 0x06", result.error_message.c_str(), length,
             expected_length);
        return result;
    }

    // Parse header
    memcpy(result.serial_number, &data[InverterProtocolOffsets::SERIAL_NUM],
           MODBUS_SERIAL_NUMBER_LENGTH);
    result.start_address =
        InverterProtocol::parse_little_endian_uint16(data, InverterProtocolOffsets::START_REG);

    // Verify CRC
    const uint16_t calculated_crc = InverterProtocol::calculate_crc16(data, length - 2);
    const uint16_t received_crc = InverterProtocol::parse_little_endian_uint16(data, length - 2);
    LOGD(TAG, "CRC Check: calculated=0x%04X, received=0x%04X", calculated_crc, received_crc);
    if (calculated_crc != received_crc) {
        result.error_message = "CRC mismatch";
        LOGW(TAG, "%s: calculated=0x%04X, received=0x%04X", result.error_message.c_str(),
             calculated_crc, received_crc);
    }

    // Extract value
    result.register_count = 1;
    result.register_values.reserve(1);
    const uint16_t value =
        InverterProtocol::parse_little_endian_uint16(data, InverterProtocolOffsets::COUNT_OR_VALUE);
    result.register_values.push_back(value);

    result.success = true;
    return result;
}

/**
 * @brief Parse write multiple response (function 0x10)
 */
static ParseResult parse_write_multi_response(const uint8_t* data, size_t length) {
    ParseResult result;
    result.function_code = ModbusFunctionCode::WRITE_MULTI;

    const size_t expected_length = 18;
    if (length < expected_length) {
        result.error_message = "Response packet too short";
        LOGE(TAG, "%s: got %d, expected %d for func 0x10", result.error_message.c_str(), length,
             expected_length);
        return result;
    }

    // Parse header
    memcpy(result.serial_number, &data[InverterProtocolOffsets::SERIAL_NUM],
           MODBUS_SERIAL_NUMBER_LENGTH);
    result.start_address =
        InverterProtocol::parse_little_endian_uint16(data, InverterProtocolOffsets::START_REG);

    // Verify CRC
    const uint16_t calculated_crc = InverterProtocol::calculate_crc16(data, length - 2);
    const uint16_t received_crc = InverterProtocol::parse_little_endian_uint16(data, length - 2);
    LOGD(TAG, "CRC Check: calculated=0x%04X, received=0x%04X", calculated_crc, received_crc);
    if (calculated_crc != received_crc) {
        result.error_message = "CRC mismatch";
        LOGW(TAG, "%s: calculated=0x%04X, received=0x%04X", result.error_message.c_str(),
             calculated_crc, received_crc);
    }

    // Response includes only count confirmation (no values)
    result.register_count =
        InverterProtocol::parse_little_endian_uint16(data, InverterProtocolOffsets::COUNT_OR_VALUE);
    result.success = true;
    return result;
}

// ============================================================================
// SECTION 11: Response Parsing - Main Entry Point
// ============================================================================

ParseResult InverterProtocol::parse_response(const uint8_t* data, size_t length) {
    if (!is_valid_response(data, length)) {
        ParseResult invalid;
        invalid.error_message = "Invalid response packet";
        LOGE(TAG, "%s", invalid.error_message.c_str());
        return invalid;
    }

    const uint8_t func_byte = data[InverterProtocolOffsets::FUNC];

    // Check for exception response (0x80 bit set)
    if (func_byte & 0x80) {
        return parse_exception_response(data, length);
    }

    // Route to appropriate parser based on function code
    ParseResult result;
    switch (func_byte) {
        case 0x03: // READ_HOLDING
        case 0x04: // READ_INPUT
            result = parse_read_response(data, length, func_byte);
            break;
        case 0x06: // WRITE_SINGLE
            result = parse_write_single_response(data, length);
            break;
        case 0x10: // WRITE_MULTI
            result = parse_write_multi_response(data, length);
            break;
        default:
            result.error_message = "Unknown function code in response";
            LOGE(TAG, "%s: 0x%02X", result.error_message.c_str(), func_byte);
            return result;
    }

    if (result.success) {
        LOGD(TAG, "Parsed response: func=0x%02X, start=0x%04X, count=%d, SN=%s",
             static_cast<uint8_t>(result.function_code), result.start_address,
             result.register_count, serial_to_string(result.serial_number).c_str());
    }

    return result;
}

// ============================================================================
// SECTION 12: Multi-Frame Handling
// ============================================================================

/**
 * @brief Calculate frame length based on function code
 */
size_t InverterProtocol::calculate_frame_length(const uint8_t* frame, size_t available) {
    if (available < 2)
        return 0;

    const uint8_t addr = frame[0];
    const uint8_t func = frame[1] & 0x7F;

    // Request: always 18 bytes
    if (addr == MODBUS_DEVICE_ADDR_REQUEST) {
        return 18;
    }

    // Exception response: 17 bytes
    if (frame[1] & 0x80) {
        return MODBUS_MIN_EXCEPTION_SIZE;
    }

    // Read response: 17 + byte_count
    if (func == 0x03 || func == 0x04) {
        if (available >= 15) {
            return 17 + frame[14];
        }
        return 0;
    }

    // Write response: 18 bytes
    if (func == 0x06 || func == 0x10) {
        return 18;
    }

    return 0;
}

/**
 * @brief Parse all frames in the buffer
 *
 * This handles the case where we receive concatenated frames from
 * multiple masters on the shared RS485 bus.
 */
std::vector<FrameInfo> InverterProtocol::parse_all_frames(const std::vector<uint8_t>& data) {
    std::vector<FrameInfo> frames;
    size_t offset = 0;

    while (offset < data.size()) {
        if (data.size() - offset < 2)
            break;

        const uint8_t* frame_start = data.data() + offset;
        const size_t remaining = data.size() - offset;
        const uint8_t addr = frame_start[0];

        // Handle request (addr=0x00)
        if (addr == MODBUS_DEVICE_ADDR_REQUEST) {
            size_t frame_len = calculate_frame_length(frame_start, remaining);
            if (frame_len > 0 && frame_len <= remaining) {
                FrameInfo info = {offset, frame_len, true, {}};
                frames.push_back(info);
                LOGD(TAG, "Frame[%d]: REQUEST at offset %d, len=%d", frames.size() - 1, offset,
                     frame_len);
                offset += frame_len;
            } else {
                offset++;
            }
            continue;
        }

        // Handle response (addr=0x01)
        if (addr == MODBUS_DEVICE_ADDR_RESPONSE) {
            size_t frame_len = calculate_frame_length(frame_start, remaining);
            if (frame_len > 0 && frame_len <= remaining) {
                FrameInfo info;
                info.offset = offset;
                info.length = frame_len;
                info.is_request = false;
                info.result = parse_response(frame_start, frame_len);
                frames.push_back(info);
                LOGD(TAG, "Frame[%d]: RESPONSE at offset %d, len=%d, func=0x%02X, start=%d",
                     frames.size() - 1, offset, frame_len,
                     static_cast<uint8_t>(info.result.function_code), info.result.start_address);
                offset += frame_len;
            } else {
                offset++;
            }
            continue;
        }

        // Unknown byte, skip
        offset++;
    }

    return frames;
}

/**
 * @brief Find the response matching our request
 * @return Index of matching frame, or -1 if not found
 */
int InverterProtocol::find_matching_response_index(const std::vector<FrameInfo>& frames,
                                                   ModbusFunctionCode expected_func,
                                                   uint16_t expected_start_reg) {
    for (size_t i = 0; i < frames.size(); i++) {
        const FrameInfo& frame = frames[i];

        if (frame.is_request)
            continue;

        if (frame.result.success && frame.result.function_code == expected_func &&
            frame.result.start_address == expected_start_reg) {
            return static_cast<int>(i);
        }
    }
    return -1;
}
