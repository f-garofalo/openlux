/**
 * @file crc16.h
 * @brief CRC16-Modbus calculator
 *
 * @license GPL-3.0
 * @author OpenLux Contributors
 */

#pragma once

#include <cstddef>
#include <cstdint>

/**
 * @brief CRC16-Modbus (poly 0xA001) calculator.
 *
 * Used by both TCP and RS485 protocol packets.
 */
class CRC16 {
  public:
    /**
     * @param data Pointer to the buffer
     * @param length Number of bytes to process
     * @return CRC16 value
     */
    static uint16_t calculate(const uint8_t* data, size_t length);
};
