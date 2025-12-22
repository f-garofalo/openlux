/**
 * @file protocol_bridge.cpp
 * @brief Protocol bridge implementation
 *
 * @license GPL-3.0
 * @author OpenLux Contributors
 */
#include "protocol_bridge.h"

#include "logger.h"
#include "network_manager.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char* TAG = "bridge";

ProtocolBridge& ProtocolBridge::getInstance() {
    static ProtocolBridge instance;
    return instance;
}

void ProtocolBridge::begin(const String& dongle_serial) {
    dongle_serial_ = dongle_serial;

    LOGI(TAG, "Initializing Protocol Bridge");
    LOGI(TAG, "  Dongle Serial: %s", dongle_serial_.c_str());
}

void ProtocolBridge::loop() {
    if (!is_ready()) {
        return;
    }

    // Check for RS485 response if we're waiting
    if (waiting_rs485_response_) {
        process_rs485_response();

        const uint32_t timeout_ms = REQUEST_TIMEOUT_MS;

        // Check timeout
        if (millis() - last_request_time_ > timeout_ms) {
            // Don't log as error if it's a network issue
            if (WiFi.status() != WL_CONNECTED) {
                LOGW(TAG, "Request timeout during WiFi disconnection (%lu ms)", timeout_ms);
            } else {
                LOGW(TAG, "Request timeout (%lu ms)", timeout_ms);
            }
            send_error_response(current_request_.client, "Request timeout");
            waiting_rs485_response_ = false;
            failed_requests_++;
        }
    }
}

void ProtocolBridge::process_wifi_request(const uint8_t* data, size_t length, TCPClient* client) {
    if (!is_ready()) {
        LOGW(TAG, "Bridge not ready (tcp_server=%p, rs485=%p)", tcp_server_, rs485_);
        return;
    }

    // Check if any blocking operation (other than TCP) is in progress
    // WiFi scan, OTA, and Network validation can interfere with TCP processing
    auto& guard_mgr = OperationGuardManager::getInstance();
    if (guard_mgr.hasActiveOperation()) {
        OperationGuard::OperationType active_op = guard_mgr.getActiveOperation();
        // if (active_op != OperationGuard::OperationType::TCP_CLIENT_PROCESSING) {
        const char* op_name = OperationGuardManager::getOperationTypeName(active_op);
        LOGW(TAG, "Bridge paused (%s), rejecting request, operation in progress: ", op_name);
        send_error_response(client, "Bridge paused");
        failed_requests_++;
        return;
        //}
    }

    // Acquire TCP operation guard here - this is where we actually process the request
    // and communicate with RS485
    auto guard = guard_mgr.acquireGuard(OperationGuard::OperationType::TCP_CLIENT_PROCESSING,
                                        "process_wifi_request");

    total_requests_++;

    // Use static buffers instead of String to avoid memory fragmentation
    char req_tag[20];
    snprintf(req_tag, sizeof(req_tag), "[REQ#%u] ", total_requests_);
    LOGD(TAG, "%sWiFi raw (first 40b): %s", req_tag,
         TcpProtocol::format_hex(data, min(length, (size_t) 40)).c_str());

    TcpParseResult parse_result = TcpProtocol::parse_request(data, length);

    if (!parse_result.success) {
        LOGE(TAG, "✗ Failed to parse WiFi request: %s", parse_result.error_message.c_str());
        send_error_response(client, parse_result.error_message);
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
         client ? client->remote_ip.c_str() : "unknown");
    LOGD(TAG, "%sInverter SN: %s", req_tag,
         TcpProtocol::format_serial(parse_result.inverter_serial).c_str());

    // Check if we're already processing a request
    if (waiting_rs485_response_) {
        LOGW(TAG, "⚠ Already processing a request, rejecting");
        send_error_response(client, "Bridge busy");
        failed_requests_++;
        return;
    }

    // Save current request
    current_request_.client = client;
    current_request_.wifi_request = parse_result;
    current_request_.timestamp = millis();
    current_request_.retry_count = 0;

    // Forward to RS485
    LOGD(TAG, "  RS485 packet: %s",
         TcpProtocol::format_hex(parse_result.rs485_packet.data(), parse_result.rs485_packet.size())
             .c_str());

    // Send RS485 request (read or write)
    bool sent = false;

    if (parse_result.is_write_operation) {
        sent = rs485_->send_write_request(parse_result.start_register, parse_result.write_values);
    } else {
        LuxFunctionCode func = static_cast<LuxFunctionCode>(parse_result.function_code);
        sent = rs485_->send_read_request(func, parse_result.start_register,
                                         parse_result.register_count);
    }

    if (!sent) {
        LOGE(TAG, "✗ Failed to send RS485 request");
        send_error_response(client, "RS485 send failed");
        failed_requests_++;
        return;
    }
    waiting_rs485_response_ = true;
    last_request_time_ = millis();
}

bool ProtocolBridge::validate_response_match(const LuxParseResult& result,
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
        const LuxParseResult& rs485_result = rs485_->get_last_result();

        unsigned long elapsed = millis() - last_request_time_;

        if (rs485_result.success) {
            // Validate response matches request to avoid processing snooped packets
            if (!validate_response_match(rs485_result, current_request_.wifi_request)) {
                LOGW(TAG,
                     "⚠ Response mismatch! Expected func=0x%02X start=%d, Got func=0x%02X start=%d",
                     current_request_.wifi_request.function_code,
                     current_request_.wifi_request.start_register,
                     static_cast<uint8_t>(rs485_result.function_code), rs485_result.start_address);

                send_error_response(current_request_.client, "Response mismatch (collision?)");
                failed_requests_++;
                waiting_rs485_response_ = false;
                return;
            }

            // Build value summary using static buffer instead of String
            char value_summary[128] = "";
            if (rs485_result.register_count == 1 && !rs485_result.register_values.empty()) {
                snprintf(value_summary, sizeof(value_summary), " val=0x%X",
                         rs485_result.register_values[0]);
            } else if (rs485_result.register_count > 0 && !rs485_result.register_values.empty()) {
                char* pos = value_summary;
                int remaining = sizeof(value_summary);

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

            LOGI(TAG, "[REQ#%d] OK func=0x%02X regs=%d start=%d time=%lums%s", total_requests_,
                 static_cast<uint8_t>(rs485_result.function_code), rs485_result.register_count,
                 rs485_result.start_address, elapsed, value_summary);

            send_wifi_response(current_request_.client, rs485_result);
            successful_requests_++;
            LOGI(TAG, "[REQ#%d] ✓ Completed (success: %d/%d = %.1f%%)", total_requests_,
                 successful_requests_, total_requests_,
                 (100.0f * successful_requests_) / total_requests_);

            waiting_rs485_response_ = false;
        } else {
            // If it's a Modbus exception, validate it matches our request
            if (rs485_result.error_message.startsWith("Modbus Exception")) {
                if (!validate_response_match(rs485_result, current_request_.wifi_request)) {
                    LOGW(TAG,
                         "⚠ Exception response mismatch! Expected func=0x%02X start=%d, Got "
                         "func=0x%02X start=%d",
                         current_request_.wifi_request.function_code,
                         current_request_.wifi_request.start_register,
                         static_cast<uint8_t>(rs485_result.function_code),
                         rs485_result.start_address);

                    send_error_response(current_request_.client, "Response mismatch (collision?)");
                    failed_requests_++;
                    waiting_rs485_response_ = false;
                    return;
                }
            }

            LOGE(TAG, "✗ RS485 FAIL: %s (after %lums)", rs485_result.error_message.c_str(),
                 elapsed);
            const std::vector<uint8_t>& raw = rs485_->get_last_raw_response();
            if (!raw.empty()) {
                LOGD(TAG, "[REQ#%d] Raw RS485 resp: %s", total_requests_,
                     TcpProtocol::format_hex(raw.data(), raw.size()).c_str());
            }
            send_error_response(current_request_.client, rs485_result.error_message);
            failed_requests_++;
            LOGE(TAG, "[REQ#%d] ✗ Failed (failures: %d/%d = %.1f%%)", total_requests_,
                 failed_requests_, total_requests_, (100.0f * failed_requests_) / total_requests_);
        }

        waiting_rs485_response_ = false;
    }
}

void ProtocolBridge::send_wifi_response(TCPClient* client, const LuxParseResult& rs485_result) {
    if (!client || !client->is_connected()) {
        LOGW(TAG, "⚠ Client disconnected, cannot send response");
        return;
    }

    // Prefer forwarding the raw RS485 packet (inverter echo) to preserve exact format
    const std::vector<uint8_t>& raw_response = rs485_->get_last_raw_response();
    if (raw_response.empty()) {
        LOGE(TAG, "✗ No raw RS485 response available");
        send_error_response(client, "No RS485 response available");
        return;
    }

    LOGD(TAG, "[REQ#%d] Wrapping raw RS485 response in TCP (A1 1A)...", total_requests_);
    std::vector<uint8_t> wifi_response;
    uint8_t dongle_serial[10];
    TcpProtocol::copy_serial(dongle_serial_, dongle_serial);

    bool built = TcpProtocol::build_response(wifi_response, raw_response.data(),
                                             raw_response.size(), dongle_serial);

    if (!built) {
        LOGE(TAG, "✗ Failed to build WiFi response");
        send_error_response(client, "Response build failed");
        return;
    }

    LOGI(TAG, "WiFi response built: %d bytes", wifi_response.size());
    LOGD(TAG, "  WiFi packet (first 60 bytes): %s",
         TcpProtocol::format_hex(wifi_response.data(), min(wifi_response.size(), (size_t) 60))
             .c_str());

    // Send to TCP client
    LOGI(TAG, "→ Sending to TCP client %s...", client->remote_ip.c_str());

    if (client->client) {
        size_t written = client->client->write(reinterpret_cast<const char*>(wifi_response.data()),
                                               wifi_response.size());
        if (written == wifi_response.size()) {
            LOGI(TAG, "✓ Response sent successfully (%d bytes)", written);
        } else {
            LOGW(TAG, "⚠ Partial write: %d/%d bytes", written, wifi_response.size());
        }
    } else {
        LOGE(TAG, "✗ Client pointer is null!");
    }
}

void ProtocolBridge::send_error_response(TCPClient* client, const String& error) {
    if (!client || !client->is_connected()) {
        return;
    }

    LOGW(TAG, "Sending error response to client: %s", error.c_str());

    // Get the last RS485 raw response if available
    const std::vector<uint8_t>& raw_response = rs485_->get_last_raw_response();

    if (!raw_response.empty()) {
        // We have the raw exception response from inverter - forward it to client
        // Build WiFi response packet wrapping the exception
        std::vector<uint8_t> wifi_response;
        uint8_t dongle_serial[10];
        TcpProtocol::copy_serial(dongle_serial_, dongle_serial);

        bool built = TcpProtocol::build_response(wifi_response, raw_response.data(),
                                                 raw_response.size(), dongle_serial);

        if (built && client->client) {
            size_t written = client->client->write(
                reinterpret_cast<const char*>(wifi_response.data()), wifi_response.size());
            LOGI(TAG, "✓ Exception response forwarded to client (%d bytes)", written);
            return;
        }
    }

    // Fallback: Close connection if we can't build proper response
    LOGW(TAG, "⚠ Cannot build proper error response, closing connection");
    if (client->client) {
        client->client->close();
    }
}
