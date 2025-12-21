/**
 * @file logger.h
 * @brief Logging system with Serial and Telnet output
 *
 * @license GPL-3.0
 * @author OpenLux Contributors
 */

#pragma once

#include <Arduino.h>

#include <array>
#include <cstddef>
#include <vector>

#include <WiFi.h>

/**
 * @brief Distributed logging system with serial + telnet output.
 */

enum class LogLevel : uint8_t { DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3, NONE = 4 };
class Logger {
  public:
    static Logger& getInstance();

    // Lifecycle
    void begin(uint32_t baud_rate = 115200);
    void loop(); // Call inside main loop

    // Log level control
    void setGlobalLevel(LogLevel level);
    LogLevel getGlobalLevel() const;
    void setModuleLevel(const char* tag, LogLevel level);
    LogLevel getModuleLevel(const char* tag) const;
    void clearModuleLevel(const char* tag);
    // Deprecated compatibility helpers
    void setLogLevel(LogLevel level) { setGlobalLevel(level); }
    LogLevel getLogLevel() const { return getGlobalLevel(); }

    // Logging methods (thread-safe)
#if OPENLUX_ENABLE_LOGGING
    void debug(const char* tag, const char* format, ...);
    void info(const char* tag, const char* format, ...);
    void warning(const char* tag, const char* format, ...);
#else
    inline void debug(const char*, const char*, ...) {}
    inline void info(const char*, const char*, ...) {}
    inline void warning(const char*, const char*, ...) {}
#endif
    void error(const char* tag, const char* format, ...);

    // Utility methods for pretty output
    void printSeparator(const char* title = nullptr, const char* color = nullptr);
    void printHeader(const char* title);

    // Telnet management
    void startTelnet(uint16_t port);
    void stopTelnet();
    bool isTelnetRunning() const { return telnet_server_ != nullptr; }
    int getTelnetClientCount() const { return telnet_clients_.size(); }


  private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void log(const char* level, const char* color, const char* tag, const char* format,
             va_list args);
    void processClients();
    void broadcast(const char* message);
    bool shouldLog(LogLevel message_level, const char* tag) const;
    LogLevel getEffectiveLevel(const char* tag) const;
    static LogLevel clampLevel(int raw_level);
    void applyCompileTimeOverrides();

    SemaphoreHandle_t mutex_ = NULL;
    WiFiServer* telnet_server_ = nullptr;
    std::vector<WiFiClient> telnet_clients_;
    char buffer_[512];
    uint16_t telnet_port_ = 0;
    LogLevel global_level_ = LogLevel::INFO;
    struct ModuleLevelOverride {
        const char* tag;
        LogLevel level;
    };
    static constexpr size_t MAX_MODULE_OVERRIDES = 16;
    std::array<ModuleLevelOverride, MAX_MODULE_OVERRIDES> module_levels_{};
    size_t module_level_count_ = 0;

    // ANSI color codes for terminal (ESPHome-style)
    static constexpr const char* COLOR_RESET = "\033[0m";
    static constexpr const char* COLOR_BOLD = "\033[1m";
    static constexpr const char* COLOR_DEBUG = "\033[35m";   // Magenta (like ESPHome)
    static constexpr const char* COLOR_INFO = "\033[32m";    // Green
    static constexpr const char* COLOR_WARN = "\033[33m";    // Yellow
    static constexpr const char* COLOR_ERROR = "\033[31m";   // Red
    static constexpr const char* COLOR_VERBOSE = "\033[37m"; // White/Gray

    // Symbols for log levels (ESPHome-style)
    static constexpr const char* SYMBOL_DEBUG = "D";
    static constexpr const char* SYMBOL_INFO = "I";
    static constexpr const char* SYMBOL_WARN = "W";
    static constexpr const char* SYMBOL_ERROR = "E";
    static constexpr const char* SYMBOL_VERBOSE = "V";
};

// Convenience macros
#if OPENLUX_ENABLE_LOGGING
#define LOGD(tag, ...) Logger::getInstance().debug(tag, __VA_ARGS__)
#define LOGI(tag, ...) Logger::getInstance().info(tag, __VA_ARGS__)
#define LOGW(tag, ...) Logger::getInstance().warning(tag, __VA_ARGS__)
#else
#define LOGD(tag, ...)
#define LOGI(tag, ...)
#define LOGW(tag, ...)
#endif
#define LOGE(tag, ...) Logger::getInstance().error(tag, __VA_ARGS__)
