/**
 * @file operation_guard.h
 * @brief Operation guard for coordinating expensive synchronous operations
 *
 * Prevents simultaneous execution of blocking operations (TCP, RS485, Network, WiFi Scan).
 * Uses RAII pattern to ensure automatic release of locks.
 *
 * @license GPL-3.0
 * @author OpenLux Contributors
 */

#pragma once

#include <cstdint>
#include <utility>

/**
 * @brief Operation guard for coordinating expensive synchronous operations
 *
 * Prevents simultaneous execution of blocking operations that could interfere with each other.
 * Uses RAII (Resource Acquisition Is Initialization) pattern for automatic lock management.
 *
 * Supports:
 * - TCP client processing
 * - RS485 communication
 * - Network validation (Gateway/MQTT checks)
 * - WiFi scanning (formerly ScanGuard)
 */
class OperationGuard {
  public:
    enum class OperationType : uint8_t {
        TCP_CLIENT_PROCESSING = 0, // TCP client data processing
        RS485_OPERATION = 1,       // RS485 communication
        NETWORK_VALIDATION = 2,    // Gateway/MQTT connectivity check
        WIFI_SCAN = 3,             // WiFi scanning
        OTA_OPERATION = 4,         // Over-The-Air firmware update
    };

    // Non-copyable, movable
    OperationGuard(const OperationGuard&) = delete;
    OperationGuard& operator=(const OperationGuard&) = delete;

    OperationGuard(OperationGuard&& other) noexcept { moveFrom(std::move(other)); }
    OperationGuard& operator=(OperationGuard&& other) noexcept {
        if (this != &other) {
            release();
            moveFrom(std::move(other));
        }
        return *this;
    }

    // RAII: Automatic release on destruction
    ~OperationGuard() { release(); }

    // Check if guard is active
    explicit operator bool() const { return active_; }
    bool is_active() const { return active_; }

    // Get reason for WiFi scan (if applicable)
    const char* getScanReason() const { return scan_reason_; }

    // Manual release if needed (usually not necessary due to RAII)
    void release();

  private:
    friend class OperationGuardManager;
    friend class NetworkManager;

    // Private constructor - can only be created by OperationGuardManager
    OperationGuard(bool active, OperationType type, const char* reason = nullptr)
        : active_(active), type_(type), scan_reason_(reason) {}

    void moveFrom(OperationGuard&& other) {
        active_ = other.active_;
        type_ = other.type_;
        scan_reason_ = other.scan_reason_;
        other.active_ = false;
        other.scan_reason_ = nullptr;
    }

    bool active_ = false;
    OperationType type_ = OperationType::TCP_CLIENT_PROCESSING;
    const char* scan_reason_ = nullptr;
};

/**
 * @brief Manager for operation guards
 *
 * Singleton that tracks active operations and manages the guard lifecycle.
 */
class OperationGuardManager {
  public:
    static OperationGuardManager& getInstance();

    // Acquire a guard for a specific operation type
    OperationGuard acquireGuard(OperationGuard::OperationType type, const char* reason = nullptr);

    // Check if operation is in progress
    bool isOperationInProgress(OperationGuard::OperationType type) const;

    // Check if WiFi scan is in progress
    bool isScanning() const {
        return isOperationInProgress(OperationGuard::OperationType::WIFI_SCAN);
    }

    // Check if OTA is in progress
    bool isOTAInProgress() const {
        return isOperationInProgress(OperationGuard::OperationType::OTA_OPERATION);
    }

    // Check if operation can be performed (no conflicting operations)
    bool canPerformOperation(OperationGuard::OperationType type) const;

    // Get current active operation (if any)
    OperationGuard::OperationType getActiveOperation() const { return active_operation_; }
    bool hasActiveOperation() const { return operation_locked_; }
    const char* getScanReason() const { return reason_; }

    // Helper function to get operation type name for logging
    static const char* getOperationTypeName(OperationGuard::OperationType type);

  private:
    OperationGuardManager() = default;
    ~OperationGuardManager() = default;
    OperationGuardManager(const OperationGuardManager&) = delete;
    OperationGuardManager& operator=(const OperationGuardManager&) = delete;

    friend class OperationGuard;

    // Called by OperationGuard on release
    void releaseGuard();


    // State tracking
    OperationGuard::OperationType active_operation_ =
        OperationGuard::OperationType::TCP_CLIENT_PROCESSING;
    bool operation_locked_ = false;
    const char* reason_ = nullptr;
};
