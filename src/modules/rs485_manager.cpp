/**
 * @file rs485_manager.cpp
 * @brief RS485 communication manager implementation
 *
 * This module handles the physical RS485 interface.
 * Protocol encoding/decoding is delegated to inverter_protocol.cpp.
 *
 * @license GPL-3.0
 * @author OpenLux Contributors
 */

#include "rs485_manager.h"

#include "../config.h"
#include "logger.h"
#include "utils/serial_utils.h"

#include <algorithm>

static const char* TAG = "rs485";

// ============================================================================
// SECTION 1: Initialization
// ============================================================================

RS485Manager& RS485Manager::getInstance() {
    static RS485Manager instance;
    return instance;
}

const char* RS485Manager::function_code_to_string(ModbusFunctionCode func) {
    switch (func) {
        case ModbusFunctionCode::READ_HOLDING:
            return "READ_HOLD";
        case ModbusFunctionCode::READ_INPUT:
            return "READ_INPUT";
        case ModbusFunctionCode::WRITE_SINGLE:
            return "WRITE_SINGLE";
        case ModbusFunctionCode::WRITE_MULTI:
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

    // Initialize UART with short timeout to avoid blocking
    serial_->begin(baud_rate, SERIAL_8N1, rx_pin, tx_pin);
    serial_->setTimeout(15);

    // Initialize DE/RE pin (receive mode by default)
    if (de_pin_ >= 0) {
        pinMode(de_pin_, OUTPUT);
        digitalWrite(de_pin_, LOW);
    }

    initialized_ = true;
    serial_probe_backoff_ms_ = RS485_PROBE_BACKOFF_BASE_MS;
    next_serial_probe_ms_ = 0;

    LOGI(TAG, "RS485 Manager initialized successfully");
}

// ============================================================================
// SECTION 2: Inverter Probe
// ============================================================================

void RS485Manager::probe_inverter_serial() {
    request_inverter_serial_probe();
}

void RS485Manager::request_inverter_serial_probe() {
    if (!initialized_)
        return;

    if (waiting_response_) {
        LOGW(TAG, "Skipping inverter serial probe: waiting for previous response");
        return;
    }

    if (millis() < next_serial_probe_ms_)
        return;

    inverter_link_ok_ = false;

    std::vector<uint8_t> packet;
    if (!InverterProtocol::create_read_request(packet, ModbusFunctionCode::READ_INPUT,
                                               MODBUS_INVERTER_SN_START_REG,
                                               MODBUS_INVERTER_SN_REG_COUNT, serial_number_)) {
        LOGE(TAG, "Failed to build inverter serial probe request");
        return;
    }

    LOGI(TAG, "Probing inverter serial (regs %d-%d)...", MODBUS_INVERTER_SN_START_REG,
         MODBUS_INVERTER_SN_START_REG + MODBUS_INVERTER_SN_REG_COUNT - 1);

    serial_probe_pending_ = true;
    expected_function_code_ = ModbusFunctionCode::READ_INPUT;
    expected_start_reg_ = MODBUS_INVERTER_SN_START_REG;
    send_packet(packet);
}

// ============================================================================
// SECTION 3: Main Loop
// ============================================================================

void RS485Manager::loop() {
    if (!initialized_)
        return;

    // Auto-probe when link is down
    if (!inverter_link_ok_ && !serial_probe_pending_ && !waiting_response_ &&
        millis() >= next_serial_probe_ms_) {
        request_inverter_serial_probe();
    }

    // Process incoming data
    process_incoming_data();

    // Check for response timeout
    if (waiting_response_ && (millis() - last_tx_time_) > response_timeout_ms_) {
        handle_timeout();
    }
}

// ============================================================================
// SECTION 4: Request Sending
// ============================================================================

bool RS485Manager::send_read_request(ModbusFunctionCode func, uint16_t start_reg, uint16_t count) {
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
    if (!InverterProtocol::create_read_request(packet, func, start_reg, count, serial_number_)) {
        return false;
    }

    LOGI(TAG, "→ TX: %s regs=%d-%d (%d regs)", function_code_to_string(func), start_reg,
         start_reg + count - 1, count);

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
    if (!InverterProtocol::create_write_request(packet, start_reg, values, serial_number_)) {
        return false;
    }

    // Log write request
    const char* func_name = (values.size() == 1) ? "WRITE_SINGLE" : "WRITE_MULTI";
    if (values.size() == 1) {
        LOGI(TAG, "→ TX: %s reg=%d val=0x%04X (%d)", func_name, start_reg, values[0], values[0]);
    } else {
        String preview = String("[0x") + String(values[0], HEX);
        for (size_t i = 1; i < std::min((size_t) 3, values.size()); i++) {
            preview += String(", 0x") + String(values[i], HEX);
        }
        preview += (values.size() > 3) ? "...]" : "]";
        LOGI(TAG, "→ TX: %s regs=%d-%d (%d vals) %s", func_name, start_reg,
             start_reg + values.size() - 1, values.size(), preview.c_str());
    }

    expected_function_code_ =
        (values.size() == 1) ? ModbusFunctionCode::WRITE_SINGLE : ModbusFunctionCode::WRITE_MULTI;
    expected_start_reg_ = start_reg;
    send_packet(packet);
    return true;
}

void RS485Manager::send_packet(const std::vector<uint8_t>& packet) {
    LOGD(TAG, "   TX raw [%d bytes]: %s", packet.size(),
         InverterProtocol::format_hex(packet.data(), packet.size()).c_str());

    // Switch to transmit mode
    if (de_pin_ >= 0) {
        digitalWrite(de_pin_, HIGH);
        delayMicroseconds(10);
    }

    // Send packet
    serial_->write(packet.data(), packet.size());
    serial_->flush();

    // Switch back to receive mode
    if (de_pin_ >= 0) {
        delayMicroseconds(10);
        digitalWrite(de_pin_, LOW);
    }

    last_tx_time_ = millis();
    waiting_response_ = true;
    total_requests_++;
}

// ============================================================================
// SECTION 5: Data Reception
// ============================================================================

bool RS485Manager::should_ignore_packet(const std::vector<uint8_t>& data) {
    if (data.empty())
        return true;

    // Ignore requests from another master (address 0x00)
    if (InverterProtocol::is_request(data.data(), data.size())) {
        LOGD(TAG, "Ignoring request packet from another master");
        ignored_packets_++;
        return true;
    }

    // Ignore if we're not waiting for a response
    if (!waiting_response_) {
        LOGD(TAG, "Ignoring packet while not waiting for response");
        ignored_packets_++;
        return true;
    }

    return false;
}

void RS485Manager::process_incoming_data() {
    // Read available bytes
    const int available = serial_->available();
    if (available > 0) {
        const size_t old_size = rx_buffer_.size();
        rx_buffer_.resize(old_size + available);
        const size_t bytes_read = serial_->readBytes(&rx_buffer_[old_size], available);
        rx_buffer_.resize(old_size + bytes_read);

        if (bytes_read > 0) {
            last_rx_time_ = millis();
        }
    }

    // Discard buffer if too large (loss of sync)
    if (rx_buffer_.size() > MODBUS_MAX_RX_BUFFER_SIZE) {
        LOGW(TAG, "RX buffer overflow (%d bytes), discarding", rx_buffer_.size());
        rx_buffer_.clear();
        waiting_response_ = false;
        return;
    }

    // Wait for inter-frame delay before processing
    if (rx_buffer_.empty() || (millis() - last_rx_time_) <= MODBUS_INTER_FRAME_DELAY_MS) {
        return;
    }

    // Try to process the accumulated data
    if (should_ignore_packet(rx_buffer_)) {
        rx_buffer_.clear();
        return;
    }

    if (InverterProtocol::is_valid_response(rx_buffer_.data(), rx_buffer_.size())) {
        handle_response(rx_buffer_);
    } else {
        handle_invalid_frame();
    }

    rx_buffer_.clear();
    waiting_response_ = false;
}

void RS485Manager::handle_invalid_frame() {
    LOGW(TAG, "RX [%d bytes] - INVALID: %s", rx_buffer_.size(),
         InverterProtocol::format_hex(rx_buffer_.data(), rx_buffer_.size()).c_str());

    // Try to recover by finding valid response start (0x01)
    for (size_t i = 1; i < rx_buffer_.size() - 1; i++) {
        if (rx_buffer_[i] == 0x01) {
            uint8_t func = rx_buffer_[i + 1];
            bool valid_func = (func == 0x03 || func == 0x04 || func == 0x06 || func == 0x10 ||
                               (func >= 0x83 && func <= 0x90));
            if (valid_func) {
                LOGW(TAG, "Found potential valid response at offset %d, discarding %d bytes", i, i);
                rx_buffer_.erase(rx_buffer_.begin(), rx_buffer_.begin() + i);
                return;
            }
        }
    }

    failed_responses_++;
    if (serial_probe_pending_) {
        handle_probe_failure("invalid response frame");
    }
}

// ============================================================================
// SECTION 6: Response Processing
// ============================================================================

void RS485Manager::handle_response(const std::vector<uint8_t>& data) {
    LOGD(TAG, "   RX raw [%d bytes]: %s", data.size(),
         InverterProtocol::format_hex(data.data(), data.size()).c_str());

    // Parse all frames (handles concatenated traffic from multiple masters)
    std::vector<FrameInfo> frames = InverterProtocol::parse_all_frames(data);

    if (frames.empty()) {
        last_result_.success = false;
        last_result_.error_message = "No valid frames found in response";
        last_raw_response_ = data;
        LOGW(TAG, "No valid frames found in %d bytes", data.size());
        failed_responses_++;
        return;
    }

    // Log summary if multiple frames
    if (frames.size() > 1) {
        size_t req_count = 0, resp_count = 0;
        for (const auto& f : frames) {
            f.is_request ? req_count++ : resp_count++;
        }
        LOGI(TAG, "Found %d frames: %d requests, %d responses", frames.size(), req_count,
             resp_count);
    }

    // Find our response
    int idx = InverterProtocol::find_matching_response_index(frames, expected_function_code_,
                                                             expected_start_reg_);

    if (idx >= 0) {
        const FrameInfo& our_frame = frames[idx];
        if (our_frame.offset > 0) {
            LOGI(TAG, "Found our response at offset %d (skipped %d bytes)", our_frame.offset,
                 our_frame.offset);
        }
        last_result_ = our_frame.result;
        last_raw_response_.assign(data.data() + our_frame.offset,
                                  data.data() + our_frame.offset + our_frame.length);
    } else {
        handle_response_not_found(frames);
    }

    process_response_result(data);
}

void RS485Manager::handle_response_not_found(const std::vector<FrameInfo>& frames) {
    last_result_.success = false;
    last_result_.error_message = "Response not found (traffic from other master?)";

    LOGW(TAG, "Could not find our response (expected func=0x%02X start=%d)",
         static_cast<uint8_t>(expected_function_code_), expected_start_reg_);

    for (const auto& f : frames) {
        if (!f.is_request) {
            LOGW(TAG, "   Found: func=0x%02X start=%d (not ours)",
                 static_cast<uint8_t>(f.result.function_code), f.result.start_address);
        }
    }
}

void RS485Manager::process_response_result(const std::vector<uint8_t>& data) {
    const bool is_serial_probe = serial_probe_pending_ &&
                                 last_result_.function_code == ModbusFunctionCode::READ_INPUT &&
                                 last_result_.start_address == MODBUS_INVERTER_SN_START_REG &&
                                 last_result_.register_count >= MODBUS_INVERTER_SN_REG_COUNT;

    if (last_result_.success) {
        log_successful_response();

        if (is_serial_probe) {
            extract_inverter_serial(data);
        }
        successful_responses_++;
    } else {
        LOGE(TAG, "← RX: FAIL | %s", last_result_.error_message.c_str());
        failed_responses_++;

        if (is_serial_probe) {
            handle_probe_failure(last_result_.error_message.c_str());
        }
    }

    if (serial_probe_pending_ && !is_serial_probe) {
        serial_probe_pending_ = false;
    }
}

void RS485Manager::log_successful_response() {
    const char* func_name = function_code_to_string(last_result_.function_code);

    // Build value preview
    String value_preview = "";
    if (last_result_.function_code == ModbusFunctionCode::WRITE_MULTI) {
        value_preview = " (confirmed)";
    } else if (last_result_.register_count == 1 && !last_result_.register_values.empty()) {
        value_preview = String(" = 0x") + String(last_result_.register_values[0], HEX);
    } else if (last_result_.register_count > 0 && !last_result_.register_values.empty()) {
        value_preview = String(" = [0x") + String(last_result_.register_values[0], HEX);
        for (size_t i = 1; i < std::min((size_t) 3, last_result_.register_values.size()); i++) {
            value_preview += String(", 0x") + String(last_result_.register_values[i], HEX);
        }
        value_preview += (last_result_.register_count > 3) ? "...]" : "]";
    }

    LOGI(TAG, "← RX: %s OK | %d regs%s", func_name, last_result_.register_count,
         value_preview.c_str());
}

void RS485Manager::extract_inverter_serial(const std::vector<uint8_t>& data) {
    const size_t data_offset = InverterProtocolOffsets::COUNT_OR_VALUE + 1;
    const size_t available = (data.size() > data_offset) ? data.size() - data_offset : 0;

    uint8_t serial_bytes[MODBUS_SERIAL_NUMBER_LENGTH] = {0};
    size_t copy_len = std::min(available, static_cast<size_t>(MODBUS_SERIAL_NUMBER_LENGTH));
    memcpy(serial_bytes, &data[data_offset], copy_len);

    inverter_serial_detected_ =
        SerialUtils::format_serial(serial_bytes, MODBUS_SERIAL_NUMBER_LENGTH);
    serial_number_ = inverter_serial_detected_;

    LOGI(TAG, "Inverter serial (regs %d-%d): %s", MODBUS_INVERTER_SN_START_REG,
         MODBUS_INVERTER_SN_START_REG + MODBUS_INVERTER_SN_REG_COUNT - 1,
         inverter_serial_detected_.c_str());

    serial_probe_pending_ = false;
    inverter_link_ok_ = true;
    serial_probe_backoff_ms_ = RS485_PROBE_BACKOFF_BASE_MS;
    next_serial_probe_ms_ = 0;
}

// ============================================================================
// SECTION 7: Timeout & Error Handling
// ============================================================================

void RS485Manager::handle_timeout() {
    if (!waiting_response_)
        return;

    timeout_count_++;

    const char* func_name = function_code_to_string(expected_function_code_);
    LOGW(TAG, "Response timeout (%d ms) | func=%s (0x%02X) start_reg=%d", response_timeout_ms_,
         func_name, static_cast<uint8_t>(expected_function_code_), expected_start_reg_);
    LOGW(TAG, "  Stats: timeout=%d, failed=%d, success=%d", timeout_count_, failed_responses_,
         successful_responses_);

    waiting_response_ = false;
    last_result_.success = false;
    last_result_.error_message = "Timeout";
    last_raw_response_.clear();

    if (serial_probe_pending_) {
        handle_probe_failure("timeout");
    }
}

void RS485Manager::handle_probe_failure(const char* reason) {
    LOGE(TAG, "Inverter serial probe failed: %s", reason);
    serial_probe_pending_ = false;
    inverter_link_ok_ = false;

    // Exponential backoff for next probe
    next_serial_probe_ms_ = millis() + serial_probe_backoff_ms_;
    serial_probe_backoff_ms_ =
        std::min(serial_probe_backoff_ms_ * 2, static_cast<uint32_t>(RS485_PROBE_BACKOFF_MAX_MS));
}
