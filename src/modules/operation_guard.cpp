/**
 * @file operation_guard.cpp
 * @brief Operation guard implementation
 *
 * @license GPL-3.0
 * @author OpenLux Contributors
 */

#include "operation_guard.h"

#include "logger.h"

static const char* TAG = "guard";

// ============================================================================
// OperationGuardManager (Singleton)
// ============================================================================

const char* OperationGuardManager::getOperationTypeName(OperationGuard::OperationType type) {
    switch (type) {
        case OperationGuard::OperationType::TCP_CLIENT_PROCESSING:
            return "TCP";
        case OperationGuard::OperationType::RS485_OPERATION:
            return "RS485";
        case OperationGuard::OperationType::NETWORK_VALIDATION:
            return "NET_VALID";
        case OperationGuard::OperationType::WIFI_SCAN:
            return "WiFi_SCAN";
        case OperationGuard::OperationType::OTA_OPERATION:
            return "OTA";
        default:
            return "UNKNOWN";
    }
}

// ============================================================================
// OperationGuardManager (Singleton)
// ============================================================================

OperationGuardManager& OperationGuardManager::getInstance() {
    static OperationGuardManager instance;
    return instance;
}

OperationGuard OperationGuardManager::acquireGuard(OperationGuard::OperationType type,
                                                   const char* reason) {
    operation_locked_ = true;
    active_operation_ = type;
    reason_ = reason;

    const char* type_name = getOperationTypeName(type);

    if (reason) {
        LOGD(TAG, "Guard acquired: %s (%s)", type_name, reason);
    } else {
        LOGD(TAG, "Guard acquired: %s", type_name);
    }
    return OperationGuard(true, type, reason);
}

bool OperationGuardManager::isOperationInProgress(OperationGuard::OperationType type) const {
    return operation_locked_ && active_operation_ == type;
}

bool OperationGuardManager::canPerformOperation(OperationGuard::OperationType type) const {
    // Can perform if no operation is locked, or if it's the same operation type
    if (!operation_locked_) {
        return true;
    }
    return active_operation_ == type;
}

void OperationGuardManager::releaseGuard() {
    const char* type_name = getOperationTypeName(active_operation_);
    LOGD(TAG, "Guard released: %s", type_name);
    operation_locked_ = false;
    reason_ = nullptr;
}

// ============================================================================
// OperationGuard
// ============================================================================

void OperationGuard::release() {
    if (!active_) {
        return;
    }

    OperationGuardManager::getInstance().releaseGuard();
    active_ = false;
}
