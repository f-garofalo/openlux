/**
 * @file serial_utils.h
 * @brief Serial utilities for debugging and formatting
 *
 * @license GPL-3.0
 * @author OpenLux Contributors
 */

#pragma once

#include <Arduino.h>

#include <cstddef>
#include <cstdint>

/**
 * @brief Utility helpers for serial number handling (copy/format).
 */
class SerialUtils {
  public:
    /**
     * Copy a serial string into a fixed-size byte buffer.
     * Pads with zeros if the string is shorter than the buffer.
     */
    static void write_serial(uint8_t* dest, size_t len, const String& serial);

    /**
     * Format a serial number (byte buffer) as printable string.
     * Non-printable bytes become '.' to keep length consistent.
     */
    static String format_serial(const uint8_t* serial, size_t len);
};
