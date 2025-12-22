# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]
- Placeholder for future changes.

## [1.0.4] - 2025-12-22
### Added
- **MQTT Support**: Full MQTT integration for telemetry publishing and remote command execution with configurable broker, topics, and Home Assistant discovery prefix.
- **Enhanced WiFi Roaming**: Improved WiFi roaming with scan guard mechanism and refined connection validation logic to prevent reconnection loops.
- **WiFi Channel Reporting**: WiFi channel information is now reported in status and system diagnostics commands.
- **Improved Network Validation**: Enhanced active connection validation with better error handling and stability improvements.
- **WiFi TX Power Reporting**: Added WiFi TX power information to command output and system diagnostics.

### Fixed
- Improved stability and reliability of WiFi connection management with better validation of active connections.
- Enhanced error handling in network operations to reduce spurious reconnection attempts.
- Improved logging consistency across all network-related modules.

### Changed
- Refactored network manager internals for better separation of concerns and improved maintainability.
- Enhanced documentation with dual dongle mode schematic and additional hardware references.

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
