/**
 * @file crc16.cpp
 * @brief CRC16-Modbus implementation
 *
 * @license GPL-3.0
 * @author OpenLux Contributors
 */

#include "crc16.h"

uint16_t CRC16::calculate(const uint8_t* data, size_t length) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}
