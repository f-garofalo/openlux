/**
 * @file rs485_manager.cpp
 * @brief RS485 communication implementation
 *
 * @license GPL-3.0
 * @author OpenLux Contributors
 */
#include "rs485_manager.h"

#include "../config.h"
#include "logger.h"
#include "utils/crc16.h"
#include "utils/serial_utils.h"

#include <algorithm>

static const char* TAG = "rs485";

// ============================================================================
// LuxProtocol Implementation
// ============================================================================

uint16_t LuxProtocol::calculate_crc16(const uint8_t* data, size_t length) {
    return CRC16::calculate(data, length);
}

bool LuxProtocol::create_read_request(std::vector<uint8_t>& packet, LuxFunctionCode func,
                                      uint16_t start_reg, uint16_t count,
                                      const String& serial_number) {
    if (count == 0 || count > LUX_MAX_REGISTERS) {
        LOGE(TAG, "Invalid register count: %d (max %d)", count, LUX_MAX_REGISTERS);
        return false;
    }

    packet.clear();
    packet.resize(LUX_MIN_REQUEST_SIZE);

    // Device address (0x00 for requests)
    packet[LuxProtocolOffsets::ADDR] = LUX_DEVICE_ADDR_REQUEST;

    // Function code
    packet[LuxProtocolOffsets::FUNC] = static_cast<uint8_t>(func);

    // Serial number (10 bytes) - use zeros if not provided
    if (serial_number.length() == 0) {
        memset(&packet[LuxProtocolOffsets::SERIAL_NUM], 0x00, LUX_SERIAL_NUMBER_LENGTH);
    } else {
        SerialUtils::write_serial(&packet[LuxProtocolOffsets::SERIAL_NUM], LUX_SERIAL_NUMBER_LENGTH,
                                  serial_number);
    }

    // Start address (little-endian)
    write_little_endian_uint16(&packet[0], LuxProtocolOffsets::START_REG, start_reg);

    // Register count (little-endian)
    write_little_endian_uint16(&packet[0], LuxProtocolOffsets::COUNT_OR_VALUE, count);

    // Calculate CRC (exclude CRC bytes themselves)
    const uint16_t crc = calculate_crc16(&packet[0], LuxProtocolOffsets::CRC_MIN_PACKET);

    // Write CRC (little-endian)
    write_little_endian_uint16(&packet[0], LuxProtocolOffsets::CRC_MIN_PACKET, crc);

    const char* func_name = (func == LuxFunctionCode::READ_HOLDING) ? "READ_HOLD"
                            : (func == LuxFunctionCode::READ_INPUT) ? "READ_INPUT"
                                                                    : "READ";
    LOGI(TAG, "→ TX: %s regs=%d-%d (%d regs)", func_name, start_reg, start_reg + count - 1, count);

    return true;
}

bool LuxProtocol::create_write_request(std::vector<uint8_t>& packet, uint16_t start_reg,
                                       const std::vector<uint16_t>& values,
                                       const String& serial_number) {
    if (values.empty() || values.size() > LUX_MAX_REGISTERS) {
        LOGE(TAG, "Invalid register count: %d (max %d)", values.size(), LUX_MAX_REGISTERS);
        return false;
    }

    packet.clear();

    if (values.size() == 1) {
        // Write Single Register (0x06)
        // Format: [addr][func][serial:10][register:2][value:2][crc:2] = 18 bytes
        packet.resize(LUX_MIN_REQUEST_SIZE);

        packet[LuxProtocolOffsets::ADDR] = LUX_DEVICE_ADDR_REQUEST;
        packet[LuxProtocolOffsets::FUNC] = static_cast<uint8_t>(LuxFunctionCode::WRITE_SINGLE);

        // Serial number
        if (serial_number.length() == 0) {
            memset(&packet[LuxProtocolOffsets::SERIAL_NUM], 0x00, LUX_SERIAL_NUMBER_LENGTH);
        } else {
            SerialUtils::write_serial(&packet[LuxProtocolOffsets::SERIAL_NUM],
                                      LUX_SERIAL_NUMBER_LENGTH, serial_number);
        }

        // Register address (little-endian)
        write_little_endian_uint16(&packet[0], LuxProtocolOffsets::START_REG, start_reg);

        // Register value (little-endian)
        write_little_endian_uint16(&packet[0], LuxProtocolOffsets::COUNT_OR_VALUE, values[0]);

        // Calculate CRC
        const uint16_t crc = calculate_crc16(&packet[0], LuxProtocolOffsets::CRC_MIN_PACKET);
        write_little_endian_uint16(&packet[0], LuxProtocolOffsets::CRC_MIN_PACKET, crc);

        LOGI(TAG, "→ TX: WRITE_SINGLE reg=%d val=0x%04X (%d)", start_reg, values[0], values[0]);

    } else {
        // Write Multiple Registers (0x10)
        // Format: [addr][func][serial:10][start:2][count:2][bytecount:1][values...][crc:2]
        const size_t byte_count = values.size() * 2;
        const size_t packet_size = 17 + byte_count + 2; // Header(17) + data + CRC(2)

        packet.resize(packet_size);

        packet[LuxProtocolOffsets::ADDR] = LUX_DEVICE_ADDR_REQUEST;
        packet[LuxProtocolOffsets::FUNC] = static_cast<uint8_t>(LuxFunctionCode::WRITE_MULTI);

        // Serial number
        if (serial_number.length() == 0) {
            memset(&packet[LuxProtocolOffsets::SERIAL_NUM], 0x00, LUX_SERIAL_NUMBER_LENGTH);
        } else {
            SerialUtils::write_serial(&packet[LuxProtocolOffsets::SERIAL_NUM],
                                      LUX_SERIAL_NUMBER_LENGTH, serial_number);
        }

        // Start address (little-endian)
        write_little_endian_uint16(&packet[0], LuxProtocolOffsets::START_REG, start_reg);

        // Register count (little-endian)
        write_little_endian_uint16(&packet[0], LuxProtocolOffsets::COUNT_OR_VALUE, values.size());

        // Byte count
        packet[LuxProtocolOffsets::BYTE_COUNT] = byte_count & 0xFF;

        // Register values (little-endian)
        for (size_t i = 0; i < values.size(); i++) {
            write_little_endian_uint16(&packet[0], LuxProtocolOffsets::DATA_START + (i * 2),
                                       values[i]);
        }

        // Calculate CRC
        const uint16_t crc = calculate_crc16(&packet[0], packet_size - 2);
        write_little_endian_uint16(&packet[0], packet_size - 2, crc);

        // Build value preview
        String value_preview = String("[0x") + String(values[0], HEX);
        for (size_t i = 1; i < min((size_t) 3, values.size()); i++) {
            value_preview += String(", 0x") + String(values[i], HEX);
        }
        if (values.size() > 3) {
            value_preview += "...";
        }
        value_preview += "]";

        LOGI(TAG, "→ TX: WRITE_MULTI regs=%d-%d (%d vals) %s", start_reg,
             start_reg + values.size() - 1, values.size(), value_preview.c_str());
    }

    return true;
}

// ------------------ Internal parse helpers to keep parse_response concise ------------------
static LuxParseResult parse_exception_response(const uint8_t* data, size_t length) {
    LuxParseResult result;

    const uint8_t func_byte = data[LuxProtocolOffsets::FUNC];
    result.function_code = static_cast<LuxFunctionCode>(func_byte & 0x7F);
    memcpy(result.serial_number, &data[LuxProtocolOffsets::SERIAL_NUM], LUX_SERIAL_NUMBER_LENGTH);

    if (length >= LUX_MIN_EXCEPTION_SIZE) {
        const uint16_t failed_register =
            LuxProtocol::parse_little_endian_uint16(data, LuxProtocolOffsets::START_REG);
        result.start_address = failed_register;
        const uint8_t exception_code = data[LuxProtocolOffsets::EXCEPTION_CODE];

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

static LuxParseResult parse_read_response(const uint8_t* data, size_t length, uint8_t func_byte) {
    LuxParseResult result;
    result.function_code = static_cast<LuxFunctionCode>(func_byte);
    memcpy(result.serial_number, &data[LuxProtocolOffsets::SERIAL_NUM], LUX_SERIAL_NUMBER_LENGTH);
    result.start_address =
        LuxProtocol::parse_little_endian_uint16(data, LuxProtocolOffsets::START_REG);

    const uint8_t byte_count = data[LuxProtocolOffsets::COUNT_OR_VALUE];
    const size_t expected_length = 15 + byte_count + 2; // header + data + crc
    if (length < expected_length) {
        result.error_message = "Response packet too short";
        LOGE(TAG, "%s: got %d, expected %d for func 0x%02X", result.error_message.c_str(), length,
             expected_length, func_byte);
        return result;
    }

    const uint16_t calculated_crc = LuxProtocol::calculate_crc16(data, length - 2);
    const uint16_t received_crc = LuxProtocol::parse_little_endian_uint16(data, length - 2);
    LOGD(TAG, "CRC Check: calculated=0x%04X, received=0x%04X", calculated_crc, received_crc);
    if (calculated_crc != received_crc) {
        result.error_message = "CRC mismatch";
        LOGW(TAG, "%s: calculated=0x%04X, received=0x%04X", result.error_message.c_str(),
             calculated_crc, received_crc);
        LOGW(TAG, "   Packet [%d bytes]: %s", length,
             LuxProtocol::format_hex(data, min(length, (size_t) 32)).c_str());
        // continue parsing anyway
    }

    result.register_count = byte_count / 2;
    result.register_values.reserve(result.register_count);
    size_t data_offset = LuxProtocolOffsets::COUNT_OR_VALUE + 1;
    for (size_t i = 0; i < result.register_count; i++) {
        const uint16_t value = LuxProtocol::parse_little_endian_uint16(data, data_offset + (i * 2));
        result.register_values.push_back(value);
    }

    result.success = true;
    return result;
}

static LuxParseResult parse_write_single_response(const uint8_t* data, size_t length) {
    LuxParseResult result;
    result.function_code = LuxFunctionCode::WRITE_SINGLE;
    memcpy(result.serial_number, &data[LuxProtocolOffsets::SERIAL_NUM], LUX_SERIAL_NUMBER_LENGTH);
    result.start_address =
        LuxProtocol::parse_little_endian_uint16(data, LuxProtocolOffsets::START_REG);

    const size_t expected_length = 18;
    if (length < expected_length) {
        result.error_message = "Response packet too short";
        LOGE(TAG, "%s: got %d, expected %d for func 0x06", result.error_message.c_str(), length,
             expected_length);
        return result;
    }

    const uint16_t calculated_crc = LuxProtocol::calculate_crc16(data, length - 2);
    const uint16_t received_crc = LuxProtocol::parse_little_endian_uint16(data, length - 2);
    LOGD(TAG, "CRC Check: calculated=0x%04X, received=0x%04X", calculated_crc, received_crc);
    if (calculated_crc != received_crc) {
        result.error_message = "CRC mismatch";
        LOGW(TAG, "%s: calculated=0x%04X, received=0x%04X", result.error_message.c_str(),
             calculated_crc, received_crc);
    }

    result.register_count = 1;
    result.register_values.reserve(1);
    const uint16_t value =
        LuxProtocol::parse_little_endian_uint16(data, LuxProtocolOffsets::COUNT_OR_VALUE);
    result.register_values.push_back(value);

    result.success = true;
    return result;
}

static LuxParseResult parse_write_multi_response(const uint8_t* data, size_t length) {
    LuxParseResult result;
    result.function_code = LuxFunctionCode::WRITE_MULTI;
    memcpy(result.serial_number, &data[LuxProtocolOffsets::SERIAL_NUM], LUX_SERIAL_NUMBER_LENGTH);
    result.start_address =
        LuxProtocol::parse_little_endian_uint16(data, LuxProtocolOffsets::START_REG);

    const size_t expected_length = 18;
    if (length < expected_length) {
        result.error_message = "Response packet too short";
        LOGE(TAG, "%s: got %d, expected %d for func 0x10", result.error_message.c_str(), length,
             expected_length);
        return result;
    }

    const uint16_t calculated_crc = LuxProtocol::calculate_crc16(data, length - 2);
    const uint16_t received_crc = LuxProtocol::parse_little_endian_uint16(data, length - 2);
    LOGD(TAG, "CRC Check: calculated=0x%04X, received=0x%04X", calculated_crc, received_crc);
    if (calculated_crc != received_crc) {
        result.error_message = "CRC mismatch";
        LOGW(TAG, "%s: calculated=0x%04X, received=0x%04X", result.error_message.c_str(),
             calculated_crc, received_crc);
    }

    // Response includes only count confirmation (no values)
    result.register_count =
        LuxProtocol::parse_little_endian_uint16(data, LuxProtocolOffsets::COUNT_OR_VALUE);
    result.success = true;
    return result;
}

LuxParseResult LuxProtocol::parse_response(const uint8_t* data, size_t length) {
    if (!is_valid_response(data, length)) {
        LuxParseResult invalid;
        invalid.error_message = "Invalid response packet";
        LOGE(TAG, "%s", invalid.error_message.c_str());
        return invalid;
    }

    // Check for Modbus exception response (function code with 0x80 bit set)
    const uint8_t func_byte = data[LuxProtocolOffsets::FUNC];
    if (func_byte & 0x80) {
        return parse_exception_response(data, length);
    }

    LuxParseResult result;
    switch (func_byte) {
        case 0x03:
        case 0x04:
            result = parse_read_response(data, length, func_byte);
            break;
        case 0x06:
            result = parse_write_single_response(data, length);
            break;
        case 0x10:
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

bool LuxProtocol::is_request(const uint8_t* data, size_t length) {
    if (length < 1) {
        return false;
    }
    // Requests always start with 0x00
    return data[0] == LUX_DEVICE_ADDR_REQUEST;
}

bool LuxProtocol::is_valid_response(const uint8_t* data, size_t length) {
    // Exception responses are shorter, so check appropriate minimum
    const uint8_t func = data[1];
    const bool is_exception = (func & 0x80) != 0;
    const size_t min_size = is_exception ? LUX_MIN_EXCEPTION_SIZE : LUX_MIN_RESPONSE_SIZE;

    if (length < min_size) {
        LOGD(TAG, "Response too short: %d bytes (min %d for %s)", length, min_size,
             is_exception ? "exception" : "normal");
        return false;
    }

    // Check device address (should be 0x01 for responses)
    if (data[LuxProtocolOffsets::ADDR] != LUX_DEVICE_ADDR_RESPONSE) {
        LOGD(TAG, "Invalid response address: 0x%02X (expected 0x%02X)",
             data[LuxProtocolOffsets::ADDR], LUX_DEVICE_ADDR_RESPONSE);
        return false;
    }

    // Check function code (allow exception codes with 0x80 bit set)
    const uint8_t base_func = func & 0x7F; // Remove exception bit

    if (base_func != static_cast<uint8_t>(LuxFunctionCode::READ_HOLDING) &&
        base_func != static_cast<uint8_t>(LuxFunctionCode::READ_INPUT) &&
        base_func != static_cast<uint8_t>(LuxFunctionCode::WRITE_SINGLE) &&
        base_func != static_cast<uint8_t>(LuxFunctionCode::WRITE_MULTI)) {
        LOGD(TAG, "Invalid function code: 0x%02X", func);
        return false;
    }

    return true;
}

String LuxProtocol::serial_to_string(const uint8_t* serial) {
    return SerialUtils::format_serial(serial, LUX_SERIAL_NUMBER_LENGTH);
}

void LuxProtocol::string_to_serial(const String& str, uint8_t* serial) {
    SerialUtils::write_serial(serial, LUX_SERIAL_NUMBER_LENGTH, str);
}

uint16_t LuxProtocol::parse_little_endian_uint16(const uint8_t* data, size_t offset) {
    return data[offset] | (static_cast<uint16_t>(data[offset + 1]) << 8);
}

void LuxProtocol::write_little_endian_uint16(uint8_t* data, size_t offset, uint16_t value) {
    data[offset] = value & 0xFF;            // Low byte first
    data[offset + 1] = (value >> 8) & 0xFF; // High byte second
}

String LuxProtocol::format_hex(const uint8_t* data, size_t length) {
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
// RS485Manager Implementation
// ============================================================================

RS485Manager& RS485Manager::getInstance() {
    static RS485Manager instance;
    return instance;
}

const char* RS485Manager::function_code_to_string(LuxFunctionCode func) {
    switch (func) {
        case LuxFunctionCode::READ_HOLDING:
            return "READ_HOLD";
        case LuxFunctionCode::READ_INPUT:
            return "READ_INPUT";
        case LuxFunctionCode::WRITE_SINGLE:
            return "WRITE_SINGLE";
        case LuxFunctionCode::WRITE_MULTI:
            return "WRITE_MULTI";
        default:
            return "UNKNOWN";
    }
}

void RS485Manager::begin(HardwareSerial& serial, int8_t tx_pin, int8_t rx_pin, int8_t de_pin,
                         uint32_t baud_rate) {
    serial_ = &serial;
    de_pin_ = de_pin;

    LOGI(TAG, "Initializing RS485 Manager");
    LOGI(TAG, "  TX Pin: GPIO%d", tx_pin);
    LOGI(TAG, "  RX Pin: GPIO%d", rx_pin);
    if (de_pin >= 0) {
        LOGI(TAG, "  DE/RE Pin: GPIO%d", de_pin);
    }
    LOGI(TAG, "  Baud Rate: %d", baud_rate);

    // Initialize UART
    serial_->begin(baud_rate, SERIAL_8N1, rx_pin, tx_pin);

    // Set short timeout for readBytes() to avoid blocking the loop
    // At 19200 baud: 1 byte ≈ 0.52ms, 18 bytes ≈ 9.4ms
    // 15ms timeout is safe and prevents long blocking
    serial_->setTimeout(15);

    // Initialize DE/RE pin if provided
    if (de_pin_ >= 0) {
        pinMode(de_pin_, OUTPUT);
        digitalWrite(de_pin_, LOW); // Receive mode by default
    }

    initialized_ = true;
    serial_probe_backoff_ms_ = RS485_PROBE_BACKOFF_BASE_MS;
    next_serial_probe_ms_ = 0;
    LOGI(TAG, "RS485 Manager initialized successfully");
}

void RS485Manager::probe_inverter_serial() {
    request_inverter_serial_probe();
}

void RS485Manager::request_inverter_serial_probe() {
    if (!initialized_) {
        return;
    }
    if (waiting_response_) {
        LOGW(TAG, "Skipping inverter serial probe: waiting for previous response");
        return;
    }

    unsigned long now = millis();
    if (now < next_serial_probe_ms_) {
        return;
    }

    inverter_link_ok_ = false; // assume down until proven otherwise

    std::vector<uint8_t> packet;
    if (!LuxProtocol::create_read_request(packet, LuxFunctionCode::READ_INPUT,
                                          LUX_INVERTER_SN_START_REG, LUX_INVERTER_SN_REG_COUNT,
                                          serial_number_)) {
        LOGE(TAG, "Failed to build inverter serial probe request");
        return;
    }

    LOGI(TAG, "Probing inverter serial (regs %d-%d)...", LUX_INVERTER_SN_START_REG,
         LUX_INVERTER_SN_START_REG + LUX_INVERTER_SN_REG_COUNT - 1);

    serial_probe_pending_ = true;
    expected_function_code_ = LuxFunctionCode::READ_INPUT;
    expected_start_reg_ = LUX_INVERTER_SN_START_REG;
    send_packet(packet);
}

void RS485Manager::loop() {
    if (!initialized_) {
        return;
    }

    // Process incoming data
    process_incoming_data();

    // Check for timeout
    if (waiting_response_ && (millis() - last_tx_time_) > response_timeout_ms_) {
        handle_timeout();
    }

    // Auto-probe with backoff when link is down
    if (!inverter_link_ok_ && !serial_probe_pending_ && !waiting_response_ &&
        millis() >= next_serial_probe_ms_) {
        request_inverter_serial_probe();
    }
}

bool RS485Manager::send_read_request(LuxFunctionCode func, uint16_t start_reg, uint16_t count) {
    if (!initialized_ || waiting_response_) {
        LOGW(TAG, "Cannot send request: %s",
             !initialized_ ? "not initialized" : "waiting for response");
        return false;
    }

    if (!inverter_link_ok_ && !serial_probe_pending_) {
        LOGW(TAG, "Inverter link down, re-probing serial before processing requests");
        probe_inverter_serial();
        return false;
    }

    std::vector<uint8_t> packet;
    if (!LuxProtocol::create_read_request(packet, func, start_reg, count, serial_number_)) {
        return false;
    }

    expected_function_code_ = func;
    expected_start_reg_ = start_reg;
    send_packet(packet);
    return true;
}

bool RS485Manager::send_write_request(uint16_t start_reg, const std::vector<uint16_t>& values) {
    if (!initialized_ || waiting_response_) {
        LOGW(TAG, "Cannot send request: %s",
             !initialized_ ? "not initialized" : "waiting for response");
        return false;
    }

    if (!inverter_link_ok_ && !serial_probe_pending_) {
        LOGW(TAG, "Inverter link down, re-probing serial before processing requests");
        probe_inverter_serial();
        return false;
    }

    std::vector<uint8_t> packet;
    if (!LuxProtocol::create_write_request(packet, start_reg, values, serial_number_)) {
        return false;
    }

    expected_function_code_ =
        values.size() == 1 ? LuxFunctionCode::WRITE_SINGLE : LuxFunctionCode::WRITE_MULTI;
    expected_start_reg_ = start_reg;
    send_packet(packet);
    return true;
}

void RS485Manager::send_packet(const std::vector<uint8_t>& packet) {
    // Log raw packet at debug level
    LOGD(TAG, "   TX raw [%d bytes]: %s", packet.size(),
         LuxProtocol::format_hex(packet.data(), packet.size()).c_str());

    // Set transmit mode (DE/RE pin HIGH)
    if (de_pin_ >= 0) {
        digitalWrite(de_pin_, HIGH);
        delayMicroseconds(10); // Small delay for transceiver switching
    }

    // Send packet
    serial_->write(packet.data(), packet.size());
    serial_->flush();

    // Set receive mode (DE/RE pin LOW)
    if (de_pin_ >= 0) {
        delayMicroseconds(10); // Small delay for last bit to be sent
        digitalWrite(de_pin_, LOW);
    }

    last_tx_time_ = millis();
    waiting_response_ = true;
    total_requests_++;
}

bool RS485Manager::should_ignore_packet(const std::vector<uint8_t>& data) {
    if (data.empty()) {
        return true;
    }

    // 1. Check if it's a request from another master (starts with 0x00)
    if (LuxProtocol::is_request(data.data(), data.size())) {
        LOGD(TAG, "Ignoring request packet from another master (starts with 0x00): %s",
             LuxProtocol::format_hex(data.data(), data.size()).c_str());
        ignored_packets_++;
        return true;
    }

    // 2. If we are NOT waiting for a response, we should ignore everything
    //    (unless we implement sniffing later)
    if (!waiting_response_) {
        LOGD(TAG, "Ignoring packet while not waiting for response: %s",
             LuxProtocol::format_hex(data.data(), data.size()).c_str());
        ignored_packets_++;
        return true;
    }

    return false;
}

void RS485Manager::process_incoming_data() {
    // Read all available bytes using optimized readBytes()
    const int available = serial_->available();
    if (available > 0) {
        const size_t old_size = rx_buffer_.size();

        // Pre-allocate space for incoming bytes
        rx_buffer_.resize(old_size + available);
        const size_t bytes_read = serial_->readBytes(&rx_buffer_[old_size], available);

        // Adjust buffer to actual bytes read
        rx_buffer_.resize(old_size + bytes_read);

        // Update timestamp only if we actually read something
        if (bytes_read > 0) {
            last_rx_time_ = millis();
        }
    }

    // Check if we have accumulated a complete frame
    if (!rx_buffer_.empty() && (millis() - last_rx_time_) > LUX_INTER_FRAME_DELAY_MS) {
        if (should_ignore_packet(rx_buffer_)) {
            rx_buffer_.clear();
            // Do NOT reset waiting_response_ here. If we are waiting, we keep waiting.
            return;
        }

        // Try to parse the response
        if (LuxProtocol::is_valid_response(rx_buffer_.data(), rx_buffer_.size())) {
            handle_response(rx_buffer_);
        } else {
            // Log invalid frame
            LOGW(TAG, "RX [%d bytes] - INVALID: %s", rx_buffer_.size(),
                 LuxProtocol::format_hex(rx_buffer_.data(), rx_buffer_.size()).c_str());
            failed_responses_++;
            if (serial_probe_pending_) {
                LOGE(TAG, "Inverter serial probe failed: invalid response frame");
                serial_probe_pending_ = false;
                inverter_link_ok_ = false;
                next_serial_probe_ms_ = millis() + serial_probe_backoff_ms_;
                serial_probe_backoff_ms_ =
                    std::min(serial_probe_backoff_ms_ * 2,
                             static_cast<uint32_t>(RS485_PROBE_BACKOFF_MAX_MS));
            }
        }

        // Clear buffer
        rx_buffer_.clear();
        waiting_response_ = false;
    }

    /*// Timeout: clear buffer if too old
    if (!rx_buffer_.empty() && (millis() - last_rx_time_) > 500) {
        LOGW(TAG, "RX buffer timeout, clearing %d bytes", rx_buffer_.size());
        rx_buffer_.clear();
        // Do NOT reset waiting_response_ here, let handle_timeout do it properly
        // waiting_response_ = false;

        if (serial_probe_pending_) {
            LOGE(TAG, "Inverter serial probe failed: inter-frame timeout");
            serial_probe_pending_ = false;
            inverter_link_ok_ = false;
            next_serial_probe_ms_ = millis() + serial_probe_backoff_ms_;
            serial_probe_backoff_ms_ = std::min(serial_probe_backoff_ms_ * 2,
                                                static_cast<uint32_t>(RS485_PROBE_BACKOFF_MAX_MS));
        }
    }*/
}

void RS485Manager::handle_response(const std::vector<uint8_t>& data) {
    // Log raw received packet at debug level
    LOGD(TAG, "   RX raw [%d bytes]: %s", data.size(),
         LuxProtocol::format_hex(data.data(), data.size()).c_str());

    // Save raw response for error forwarding
    last_raw_response_ = data;

    // Parse response
    last_result_ = LuxProtocol::parse_response(data.data(), data.size());

    // Validate against expected request
    if (last_result_.success) {
        if (last_result_.function_code != expected_function_code_) {
            last_result_.success = false;
            last_result_.error_message = "Response function code mismatch";
            LOGW(TAG, "Response function code mismatch: expected 0x%02X, got 0x%02X",
                 static_cast<uint8_t>(expected_function_code_),
                 static_cast<uint8_t>(last_result_.function_code));
        } else if (last_result_.start_address != expected_start_reg_) {
            last_result_.success = false;
            last_result_.error_message = "Response start register mismatch";
            LOGW(TAG, "Response start register mismatch: expected %d, got %d", expected_start_reg_,
                 last_result_.start_address);
        }
    }

    const bool is_serial_probe = serial_probe_pending_ &&
                                 last_result_.function_code == LuxFunctionCode::READ_INPUT &&
                                 last_result_.start_address == LUX_INVERTER_SN_START_REG &&
                                 last_result_.register_count >= LUX_INVERTER_SN_REG_COUNT;

    if (last_result_.success) {
        // Build operation description
        const char* func_name = function_code_to_string(last_result_.function_code);

        // Build value preview
        String value_preview = "";
        if (last_result_.function_code == LuxFunctionCode::WRITE_MULTI) {
            // Write Multi response has no values, only count confirmation
            value_preview = " (confirmed)";
        } else if (last_result_.register_count == 1 && last_result_.register_values.size() > 0) {
            value_preview = String(" = 0x") + String(last_result_.register_values[0], HEX);
        } else if (last_result_.register_count > 0 && last_result_.register_values.size() > 0) {
            value_preview = String(" = [0x") + String(last_result_.register_values[0], HEX);
            for (size_t i = 1; i < min((size_t) 3, last_result_.register_values.size()); i++) {
                value_preview += String(", 0x") + String(last_result_.register_values[i], HEX);
            }
            if (last_result_.register_count > 3) {
                value_preview += "...";
            }
            value_preview += "]";
        }

        LOGI(TAG, "← RX: %s OK | %d regs%s", func_name, last_result_.register_count,
             value_preview.c_str());
        if (is_serial_probe) {
            const size_t data_offset = LuxProtocolOffsets::COUNT_OR_VALUE + 1;
            const size_t available = data.size() > data_offset ? data.size() - data_offset : 0;
            uint8_t serial_bytes[LUX_SERIAL_NUMBER_LENGTH] = {0};
            size_t copy_len = min(available, static_cast<size_t>(LUX_SERIAL_NUMBER_LENGTH));
            memcpy(serial_bytes, &data[data_offset], copy_len);
            inverter_serial_detected_ =
                SerialUtils::format_serial(serial_bytes, LUX_SERIAL_NUMBER_LENGTH);
            serial_number_ = inverter_serial_detected_; // use detected SN for subsequent requests
            LOGI(TAG, "Inverter serial (regs %d-%d): %s", LUX_INVERTER_SN_START_REG,
                 LUX_INVERTER_SN_START_REG + LUX_INVERTER_SN_REG_COUNT - 1,
                 inverter_serial_detected_.c_str());
            serial_probe_pending_ = false;
            inverter_link_ok_ = true;
            serial_probe_backoff_ms_ = RS485_PROBE_BACKOFF_BASE_MS;
            next_serial_probe_ms_ = 0;
        }
        successful_responses_++;
    } else {
        LOGE(TAG, "← RX: FAIL | %s", last_result_.error_message.c_str());
        failed_responses_++;
        if (is_serial_probe) {
            LOGE(TAG, "Inverter serial probe failed: %s", last_result_.error_message.c_str());
            serial_probe_pending_ = false;
            inverter_link_ok_ = false;
            next_serial_probe_ms_ = millis() + serial_probe_backoff_ms_;
            serial_probe_backoff_ms_ = std::min(serial_probe_backoff_ms_ * 2,
                                                static_cast<uint32_t>(RS485_PROBE_BACKOFF_MAX_MS));
        }
    }

    if (serial_probe_pending_ && !is_serial_probe) {
        serial_probe_pending_ = false;
    }
}

void RS485Manager::handle_timeout() {
    if (!waiting_response_) {
        return;
    }
    timeout_count_++;

    const char* func_name = function_code_to_string(expected_function_code_);

    LOGW(TAG, "Response timeout (%d ms) | func=%s (0x%02X) start_reg=%d", response_timeout_ms_,
         func_name, static_cast<uint8_t>(expected_function_code_), expected_start_reg_);
    LOGW(TAG, "  Timeout stats: total=%d, failed=%d, success=%d", timeout_count_, failed_responses_,
         successful_responses_);

    waiting_response_ = false;

    last_result_.success = false;
    last_result_.error_message = "Timeout";
    last_raw_response_.clear(); // Clear stale data
    if (serial_probe_pending_) {
        LOGE(TAG, "Inverter serial probe failed: timeout");
        serial_probe_pending_ = false;
        inverter_link_ok_ = false;
        next_serial_probe_ms_ = millis() + serial_probe_backoff_ms_;
        serial_probe_backoff_ms_ = std::min(serial_probe_backoff_ms_ * 2,
                                            static_cast<uint32_t>(RS485_PROBE_BACKOFF_MAX_MS));
    }
}
