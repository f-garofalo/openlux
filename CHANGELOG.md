# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]
- Placeholder for future changes.

## [1.0.0] - 2025-12-11
### Added
- Initial public release of OpenLux firmware (ESP32-based Luxpower/EG4 bridge).
- TCP bridge on port 8000 (Luxpower dongle protocol), multi-client support.
- RS485 manager with read/write registers, protocol bridge, and command engine.
- Minimal web dashboard on port 80 with status and command runner (basic auth).
- Telnet logging on port 23; NTP/time sync; configurable Ethernet/WiFi.
- Hardware docs and setup instructions (RS485 wiring, HDMI pinout).
