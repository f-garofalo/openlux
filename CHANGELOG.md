# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]
- Placeholder for future changes.

## [1.0.3] - 2025-12-16
### Added
- **Smart WiFi Roaming**: Automatically scans and connects to the strongest Access Point (AP) on boot.
- **Periodic WiFi Scan**: Periodically checks for better APs and roams if a significantly stronger signal is found (configurable interval/threshold).
- **Fast Connect Mode**: Added `WIFI_FAST_CONNECT` option to skip scanning for faster boot times.
- **Manual Roaming Command**: Added `wifi_roam` command to force an immediate scan and reconnection to the best AP.

## [1.0.2] - 2025-12-15
### Added
- **SystemManager**: Centralized system operations (reboot, heap, uptime).
- **Reboot Persistence**: Last reboot reason is now saved to NVS and displayed in `status` command.
- **NTP Logging Fix**: Logs now use synchronized time (if available) instead of uptime.
- **Hardware Abstraction**: Refactored hardware-specific calls (like `ESP.restart()`) into `SystemManager`.

## [1.0.1] - 2025-12-15
### Added
- Support for simultaneous operation with official WiFi dongle (Dual Dongle Mode).
- Logic to ignore RS485 packets from other masters (official dongle) to avoid collisions.
- Improved RS485 packet validation and error handling.

## [1.0.0] - 2025-12-11
### Added
- Initial public release of OpenLux firmware (ESP32-based Luxpower/EG4 bridge).
- TCP bridge on port 8000 (Luxpower dongle protocol), multi-client support.
- RS485 manager with read/write registers, protocol bridge, and command engine.
- Minimal web dashboard on port 80 with status and command runner (basic auth).
- Telnet logging on port 23; NTP/time sync; configurable Ethernet/WiFi.
- Hardware docs and setup instructions (RS485 wiring, HDMI pinout).
