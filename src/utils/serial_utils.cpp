/**
 * @file serial_utils.cpp
 * @brief Serial utilities implementation
 *
 * @license GPL-3.0
 * @author OpenLux Contributors
 */
#include "serial_utils.h"

void SerialUtils::write_serial(uint8_t* dest, size_t len, const String& serial) {
    memset(dest, 0x00, len);
    size_t copy_len = min(serial.length(), len);
    memcpy(dest, serial.c_str(), copy_len);
}

String SerialUtils::format_serial(const uint8_t* serial, size_t len) {
    String result;
    result.reserve(len);
    for (size_t i = 0; i < len; i++) {
        if (serial[i] >= 0x20 && serial[i] <= 0x7E) {
            result += static_cast<char>(serial[i]);
        } else {
            result += '.';
        }
    }
    return result;
}
