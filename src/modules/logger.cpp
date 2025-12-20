/**
 * @file logger.cpp
 * @brief Logging system with Serial and Telnet output
 *
 * @license GPL-3.0
 * @author OpenLux Contributors
 */

#include "logger.h"

#include "../config.h"
#include "command_manager.h"
#include "ntp_manager.h"

#include <cstdarg>


Logger::Logger() : mutex_(xSemaphoreCreateRecursiveMutex()), buffer_{} {}

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

#ifndef OPENLUX_LOG_LEVEL
// 0=DEBUG, 1=INFO, 2=WARN, 3=ERROR, 4=NONE
#define OPENLUX_LOG_LEVEL 1
#endif

Logger::~Logger() {
    stopTelnet();
}

void Logger::begin(uint32_t baud_rate) {
    // Apply default log level from config
    level_ = static_cast<LogLevel>(OPENLUX_LOG_LEVEL);


    Serial.begin(baud_rate);

    // Wait for serial (max 3 seconds)
    uint32_t start = millis();
    while (!Serial && (millis() - start < 3000)) {
        delay(10);
    }

    Serial.println();
    Serial.println("================================================");
    Serial.printf("         %s v%s            \n", FIRMWARE_NAME, FIRMWARE_VERSION);
    Serial.println("      Open Source Luxpower WiFi Dongle         ");
    Serial.println("================================================");
    Serial.println();
}

void Logger::loop() {
    if (telnet_server_) {
        processClients();
    }
}

void Logger::debug(const char* tag, const char* format, ...) {
    if (level_ > LogLevel::DEBUG)
        return;
    va_list args;
    va_start(args, format);
    log("D", COLOR_DEBUG, tag, format, args);
    va_end(args);
}

void Logger::info(const char* tag, const char* format, ...) {
    if (level_ > LogLevel::INFO)
        return;
    va_list args;
    va_start(args, format);
    log("I", COLOR_INFO, tag, format, args);
    va_end(args);
}

void Logger::warning(const char* tag, const char* format, ...) {
    if (level_ > LogLevel::WARN)
        return;
    va_list args;
    va_start(args, format);
    log("W", COLOR_WARN, tag, format, args);
    va_end(args);
}

void Logger::error(const char* tag, const char* format, ...) {
    if (level_ > LogLevel::ERROR)
        return;
    va_list args;
    va_start(args, format);
    log("E", COLOR_ERROR, tag, format, args);
    va_end(args);
}

void Logger::log(const char* level, const char* color, const char* tag, const char* format,
                 va_list args) {
    if (mutex_) {
        xSemaphoreTakeRecursive(mutex_, portMAX_DELAY);
    }

    // Format timestamp (ESPHome style: [HH:MM:SS])
    unsigned long log_h, log_m, log_s;

    if (NTPManager::getInstance().isSynced()) {
        time_t now;
        time(&now);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        log_h = timeinfo.tm_hour;
        log_m = timeinfo.tm_min;
        log_s = timeinfo.tm_sec;
    } else {
        unsigned long ms = millis();
        unsigned long sec = ms / 1000;
        unsigned long min = sec / 60;
        unsigned long hr = min / 60;
        log_h = hr % 24;
        log_m = min % 60;
        log_s = sec % 60;
    }

    // Format message
    vsnprintf(buffer_, sizeof(buffer_), format, args);

    // Build log line (ESPHome-style format)
    // [HH:MM:SS][LEVEL][tag]: message
    char serial_line[600];

    // Senza colori (plain text)
    snprintf(serial_line, sizeof(serial_line), "[%02lu:%02lu:%02lu][%s][%s]: %s", log_h, log_m,
             log_s,    // Timestamp
             level,    // Level
             tag,      // Tag
             buffer_); // Message


    Serial.println(serial_line);


    // Build log line for Telnet (always with colors for better terminal support)
    char telnet_line[600];
    snprintf(telnet_line, sizeof(telnet_line), "%s[%02lu:%02lu:%02lu][%s][%s]:%s %s", color, log_h,
             log_m, log_s, level, tag, COLOR_RESET, buffer_);

    // Send to Telnet clients
    if (telnet_server_) {
        broadcast(telnet_line);
    }

    if (mutex_) {
        xSemaphoreGiveRecursive(mutex_);
    }
}

void Logger::startTelnet(uint16_t port) {
    if (telnet_server_) {
        return; // Already running
    }

    telnet_port_ = port;
    telnet_server_ = new WiFiServer(port);
    telnet_server_->begin();
    telnet_server_->setNoDelay(true);

    Serial.printf("[I][telnet] Server started on port %d\n", port);
}

void Logger::stopTelnet() {
    if (!telnet_server_) {
        return;
    }

    // Disconnect all clients
    for (auto& client : telnet_clients_) {
        if (client.connected()) {
            client.stop();
        }
    }
    telnet_clients_.clear();

    // Stop server
    telnet_server_->stop();
    delete telnet_server_;
    telnet_server_ = nullptr;

    Serial.println("[I][telnet] Server stopped");
}

void Logger::processClients() {
    if (mutex_) {
        xSemaphoreTakeRecursive(mutex_, portMAX_DELAY);
    }

    // Accept new clients
    if (telnet_server_->hasClient()) {
        WiFiClient new_client = telnet_server_->available();
        if (new_client) {
            if (telnet_clients_.size() < 5) {
                telnet_clients_.push_back(new_client);

                String welcome;
                welcome.reserve(250); // Pre-allocate for entire message
                welcome += "\n";
                welcome += "================================================\n";
                welcome += "      OpenLux Remote Logging Session          \n";
                welcome += "================================================\n";
                welcome += "FW: ";
                welcome += FIRMWARE_NAME;
                welcome += " v";
                welcome += FIRMWARE_VERSION;
                welcome += "\nBuilt: ";
                welcome += BUILD_TIMESTAMP;
                welcome += "\nConnected from: ";
                welcome += new_client.remoteIP().toString();
                welcome += "\nType 'q' to disconnect\n\n";

                new_client.print(welcome);

                Serial.printf("[I][telnet] Client connected from %s\n",
                              new_client.remoteIP().toString().c_str());
            } else {
                new_client.println("ERROR: Too many clients connected");
                new_client.stop();
            }
        }
    }

    // Process existing clients
    for (auto it = telnet_clients_.begin(); it != telnet_clients_.end();) {
        if (!it->connected()) {
            Serial.println("[I][telnet] Client disconnected");
            it->stop();
            it = telnet_clients_.erase(it);
        } else {
            // Check for quit command
            if (it->available()) {
                String cmd = it->readStringUntil('\n');
                cmd.trim();
                // Command handling: allow !cmd syntax
                if (cmd.equalsIgnoreCase("q") || cmd.equalsIgnoreCase("quit") ||
                    cmd.equalsIgnoreCase("exit")) {
                    it->println("Goodbye!");
                    it->stop();
                    it = telnet_clients_.erase(it);
                    continue;
                } else if (cmd.startsWith("!")) {
                    auto res = CommandManager::getInstance().execute(cmd);
                    it->println(res.ok ? "OK: \n" + res.message : "ERR: " + res.message);
                    Serial.printf("[%s][telnet]: %s\n", res.ok ? "OK" : "ERR", res.message.c_str());
                }
            }
            ++it;
        }
    }

    if (mutex_) {
        xSemaphoreGiveRecursive(mutex_);
    }
}

void Logger::broadcast(const char* message) {
    for (auto& client : telnet_clients_) {
        if (client.connected()) {
            client.println(message);
        }
    }
}

void Logger::printSeparator(const char* title, const char* color) {
    // Plain text separator without colors
    if (title) {
        Serial.printf("--- %s ---\n", title);
    } else {
        Serial.println("--------------------------------------------");
    }
}

void Logger::printHeader(const char* title) {
    Serial.println();
    Serial.printf("-- %s\n", title);
}
