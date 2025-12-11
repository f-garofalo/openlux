<!--
@file ARCHITECTURE.md
@brief OpenLux Architecture and Protocol Documentation
@license GPL-3.0
@author OpenLux Contributors
-->

# OpenLux Architecture & Protocol Documentation

## 📐 System Architecture

### High-Level Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                        Home Assistant                           │
│                     (lxp_modbus integration)                    │
└────────────────────────────┬────────────────────────────────────┘
                             │ TCP (WiFi)
                             │ Port 8000
                             │ Luxpower A1 1A Protocol
                             │
┌────────────────────────────┴────────────────────────────────────┐
│                         ESP32 OpenLux                           │
│  ┌──────────────┐   ┌──────────────┐   ┌──────────────┐         │
│  │ TCP Server   │──>│ Protocol     │──>│ RS485        │         │
│  │ (Port 8000)  │   │ Bridge       │   │ Manager      │         │
│  │              │<──│ (Translator) │<──│              │         │
│  └──────────────┘   └──────────────┘   └──────────────┘         │
│         ↑                                       ↓               │
│         │                                       │ RS485         │
│    WiFi Protocol                         Modbus-like Protocol   │
│    (A1 1A format)                        (19200 baud)           │
└─────────────────────────────────────────────────┬───────────────┘
                                                  │
                                                  │ A/B (RS485)
                                                  │
┌─────────────────────────────────────────────────┴───────────────┐
│                      Luxpower Inverter                          │
└─────────────────────────────────────────────────────────────────┘
```

### Component Breakdown

```
OpenLux ESP32 Firmware
├── Core Services (src/modules/)
│   ├── NetworkManager    → WiFi/Ethernet connectivity, OTA updates, mDNS
│   ├── Logger            → Dual output (Serial + Telnet on port 23)
│   ├── NTPManager        → Time synchronization with configurable servers
│   ├── CommandManager    → CLI commands via Telnet/Serial (status, reboot, etc.)
│   └── WebServerManager  → Web dashboard (port 80) with basic auth
│
├── Communication Layer (src/modules/)
│   ├── TCPServer         → Multi-client TCP server (port 8000, max 5 clients)
│   ├── TCPProtocol       → WiFi protocol parser/builder (A1 1A format)
│   └── RS485Manager      → UART hardware communication (Modbus-like protocol)
│
├── Coordination Layer (src/modules/)
│   └── ProtocolBridge    → Bidirectional translator (WiFi ↔ RS485)
│                           - Handles read/write operations (0x03, 0x04, 0x06, 0x10)
│                           - CRC validation and error handling
│                           - Serial number extraction
│
└── Utilities (src/utils/)
    ├── CRC16             → CRC16-Modbus calculator (poly 0xA001)
    └── SerialUtils       → Serial number formatting and validation
```

### Module Descriptions

#### Core Services

**NetworkManager** (`network_manager.h/cpp`)
- Manages WiFi or Ethernet connectivity based on `OPENLUX_USE_ETHERNET` flag
- Handles OTA (Over-The-Air) firmware updates on port 3232
- Provides mDNS service (openlux.local)
- Supports static IP or DHCP configuration
- Implements WiFi captive portal for initial setup
- Automatic reconnection with configurable watchdog timers
- Boot failure recovery (clears credentials after N failed boots)

**Logger** (`logger.h/cpp`)
- Dual-output logging system (Serial UART + Telnet network)
- Telnet server on port 23 for remote log monitoring
- Configurable log levels (DEBUG, INFO, WARN, ERROR, NONE)
- Multiple client support for Telnet
- Timestamped log entries with module tags
- Integration with CommandManager for interactive debugging

**NTPManager** (`ntp_manager.h/cpp`)
- Synchronizes system time with NTP servers
- Configurable primary, secondary, and fallback servers
- Timezone support (POSIX TZ format with automatic DST)
- Periodic time synchronization
- Time-based logging and operations

**CommandManager** (`command_manager.h/cpp`)
- Interactive CLI command system via Telnet/Serial
- Built-in commands: `status`, `reboot`, `help`, `wifi_restart`, `probe_rs485`
- Extensible command registration system
- Command debouncing for critical operations
- Status reporting (uptime, memory, network, RS485)

**WebServerManager** (`web_server.h/cpp`)
- HTTP server on port 80 with basic authentication
- Minimal web dashboard with live status view
- REST API endpoints:
  - `GET /api/status` - System status JSON
  - `POST /api/cmd` - Execute CLI commands
- Configurable credentials in `config.h`
- HTML interface for quick troubleshooting

#### Communication Layer

**TCPServer** (`tcp_server.h/cpp`)
- Asynchronous TCP server on port 8000 (Luxpower dongle protocol)
- Multi-client support (up to 5 simultaneous connections)
- Per-client timeout management (5 minutes default)
- Connection state tracking and cleanup
- Integration with ProtocolBridge for packet forwarding

**TCPProtocol** (`tcp_protocol.h/cpp`)
- Parser and builder for Luxpower A1 1A WiFi protocol
- Handles protocol versions (requests=2, responses=5)
- CRC16-Modbus validation
- Packet framing and serialization
- Support for all Modbus function codes (0x03, 0x04, 0x06, 0x10)
- Works over WiFi or Ethernet transparently

**RS485Manager** (`rs485_manager.h/cpp`)
- Hardware UART communication (Serial2 on ESP32)
- Configurable TX/RX/DE pins
- Modbus-like protocol implementation
- Direction control (DE/RE pin) with auto-direction support
- Timeout and retry logic
- Frame validation with CRC checking
- Inverter serial number detection and probing

#### Coordination Layer

**ProtocolBridge** (`protocol_bridge.h/cpp`)
- Central coordinator between TCP and RS485
- Bidirectional packet translation (WiFi ↔ RS485)
- Request routing and response correlation
- CRC validation on both protocols
- Register count validation (≤127 for new inverters, ≤40 for old)
- Serial number extraction and forwarding
- Error handling and response generation

#### Utilities

**CRC16** (`crc16.h/cpp`)
- CRC16-Modbus calculator (polynomial 0xA001)
- Used by both TCP and RS485 protocols
- Little-endian format support

**SerialUtils** (`serial_utils.h/cpp`)
- Serial number formatting and copying
- Validation helpers
- String conversion utilities

---

## 🔄 Communication Flow

### High-Level Operation

OpenLux acts as a transparent bridge between Home Assistant and the Luxpower inverter:

1. **TCP Connection**: Home Assistant connects to OpenLux on port 8000
2. **Request Reception**: TCPServer receives requests from Home Assistant
3. **Protocol Translation**: TCPProtocol parses the WiFi protocol format
4. **Bridge Processing**: ProtocolBridge validates and forwards to RS485
5. **RS485 Communication**: RS485Manager sends Modbus-like requests to inverter
6. **Response Handling**: Inverter response flows back through the same path
7. **TCP Response**: Translated response sent back to Home Assistant

### Supported Operations

OpenLux supports all standard Modbus-like operations:

- **Read Holding Registers (0x03)**: Read/write configuration registers
- **Read Input Registers (0x04)**: Read-only operational data
- **Write Single Register (0x06)**: Update single configuration value
- **Write Multiple Registers (0x10)**: Batch configuration updates

### Protocol Translation

The ProtocolBridge component handles bidirectional translation between:
- **WiFi Protocol**: TCP-based protocol used by Home Assistant integration
- **RS485 Protocol**: Serial Modbus-like protocol used by the inverter

All packets are validated with CRC16-Modbus checksums on both sides.

### Implementation Notes

**Protocol Details**: The specific packet structures and protocol specifications are based on community reverse-engineering efforts and are kept in internal documentation to respect intellectual property concerns. The implementation follows standard Modbus RTU principles adapted for Luxpower equipment.

**References**:
- Community projects: [lxp-bridge](https://github.com/celsworth/lxp-bridge), [LuxPower HA Integration](https://github.com/ant0nkr/luxpower-ha-integration)
- Standard Modbus RTU specification (for general understanding)


---

## ⚙️ Configuration & Customization

### Compile-Time Configuration

All user-configurable settings are in `src/config.h`:

**Network Settings:**
- `WIFI_HOSTNAME` - mDNS hostname (default: "openlux")
- `OPENLUX_USE_ETHERNET` - Enable Ethernet instead of WiFi (0=WiFi, 1=Ethernet)
- `TCP_SERVER_PORT` - TCP server port (default: 8000, don't change!)
- `TCP_MAX_CLIENTS` - Maximum simultaneous clients (default: 5)
- `WEB_DASH_PORT` - Web dashboard port (default: 80)

**Hardware Configuration:**
- `RS485_TX_PIN`, `RS485_RX_PIN`, `RS485_DE_PIN` - UART pins
- `RS485_BAUD_RATE` - Fixed at 19200 per Luxpower spec
- Ethernet PHY settings (if using Ethernet)

**Logging & Monitoring:**
- `OPENLUX_LOG_LEVEL` - 0=DEBUG, 1=INFO, 2=WARN, 3=ERROR, 4=NONE
- `TELNET_PORT` - Remote logging port (default: 23)
- `STATUS_LOG_INTERVAL_MS` - Periodic status logging interval

**Time Synchronization:**
- `NTP_SERVER_1/2/3` - Configurable NTP servers
- `TIMEZONE` - POSIX timezone format with DST support

**Watchdog & Recovery:**
- `WIFI_WATCHDOG_RECONNECT_DELAY_MS` - WiFi reconnection trigger
- `WIFI_WATCHDOG_RESTART_DELAY_MS` - WiFi interface restart trigger
- `WIFI_WATCHDOG_REBOOT_DELAY_MS` - Device reboot trigger
- `BOOT_FAIL_RESET_THRESHOLD` - Auto-reset credentials after N failed boots

**Feature Flags:**
```cpp
#define ENABLE_NTP      // Time synchronization
#define ENABLE_OTA      // Wireless firmware updates
#define ENABLE_TELNET   // Remote logging
#define ENABLE_WEB_DASH // Web dashboard
```

### Runtime Configuration

**WiFi Credentials** (in `src/secrets.h`):
- `WIFI_SSID` / `WIFI_PASSWORD` - WiFi credentials
- `OTA_PASSWORD` - OTA update password
- `USE_STATIC_IP` - Enable static IP configuration

**Web Dashboard Authentication:**
- `WEB_DASH_USER` / `WEB_DASH_PASS` - Basic auth credentials

### Board Selection

Edit `platformio.ini` to select your ESP32 variant:
```ini
board = esp32dev              # Standard ESP32
#board = esp32-s3-devkitc-1  # ESP32-S3
#board = esp32-c3-devkitm-1  # ESP32-C3
```

---

## 🏗️ Build System

### PlatformIO Environments

**openlux** - Production build
- Optimized for size and performance
- Standard debug level (INFO)

**openlux-debug** - Debug build
- Verbose logging (DEBUG level)
- Additional debug symbols
- Useful for development and troubleshooting

**openlux-ota** - OTA upload environment
- Wireless firmware upload
- Requires device IP or mDNS name
- Password-protected

### Build Scripts

**scripts/build_info.py**
- Auto-generates `src/build_info.h` with:
  - Build timestamp
  - Git commit hash (if in git repo)
- Runs before each compilation

### Dependencies

Managed via PlatformIO `lib_deps`:
- `esphome/AsyncTCP-esphome` - Async TCP library
- `tzapu/WiFiManager` - WiFi captive portal
- ESP32 Arduino Core libraries (WiFi, mDNS, OTA, etc.)

---

## 🔍 Debugging & Monitoring

### Serial Console

Connect via USB:
```bash
pio device monitor -b 115200
```

Output includes:
- Boot sequence and initialization
- Network connection status
- RS485 communication logs
- Error messages and warnings

### Telnet Logging

Connect remotely:
```bash
nc openlux.local 23
# or
telnet openlux.local 23
```

Features:
- Same output as serial console
- Multiple simultaneous clients
- No physical connection required
- Interactive command execution

### CLI Commands

Available via Serial or Telnet:

| Command       | Description |
|---------------|-------------|
| `status`      | Show system status (uptime, memory, network, RS485) |
| `reboot`      | Restart the device |
| `help`        | Show available commands |
| `wifi_restart`| Restart WiFi interface |
| `probe_rs485` | Test RS485 communication with inverter |

### Web Dashboard

Access via browser:
```
http://openlux.local
# or
http://<device-ip>
```

Features:
- Live system status view
- Memory usage, uptime, network info
- RS485 connection status
- Execute CLI commands via web interface
- Basic authentication (configurable)

### Log Levels

Configure in `config.h`:
```cpp
#define OPENLUX_LOG_LEVEL 1  // 0=DEBUG, 1=INFO, 2=WARN, 3=ERROR, 4=NONE
```

**DEBUG (0)**: Detailed packet dumps, frame-by-frame RS485 communication
**INFO (1)**: Normal operations, connections, status updates (recommended)
**WARN (2)**: Warnings and recoverable errors
**ERROR (3)**: Critical errors only
**NONE (4)**: No logging (not recommended)

---

## 📊 Performance Characteristics

### Resource Usage

**Memory (ESP32 standard)**
- RAM: ~43 KB / 328 KB (13.3%)
- Flash: ~778 KB / 1.31 MB (59.3%)
- PSRAM: Not required

**Network**
- TCP connections: Max 5 simultaneous clients
- Packet latency: <50ms typical (WiFi ↔ RS485)
- Throughput: ~100 requests/second (tested)

### Stability

**Watchdog Protection:**
- WiFi reconnection after 5 minutes offline
- WiFi restart after 10 minutes
- Device reboot after 15 minutes
- Captive portal after 20 minutes (one-time)

**Error Handling:**
- CRC validation on all packets (TCP + RS485)
- Timeout protection on RS485 (1000ms)
- Client timeout on TCP (5 minutes)
- Automatic retry with exponential backoff

**Recovery Mechanisms:**
- Boot failure counter (auto-reset after N fails)
- Network watchdog with staged recovery
- RS485 probe retry with backoff
- OTA rollback protection (manual)

---

## 🔐 Security Considerations

**Authentication:**
- Web dashboard: HTTP Basic Auth (not encrypted over HTTP)
- OTA updates: Password-protected
- TCP server (port 8000): No authentication (intended for local network only)

**Network Exposure:**
- All services listen on all interfaces
- Intended for local/trusted networks only
- **Do NOT expose directly to the internet**

**Best Practices:**
1. Change default web dashboard credentials
2. Use strong OTA password
3. Keep firmware updated
4. Isolate on VLAN if possible
5. Use firewall rules to restrict access
6. Monitor logs for unexpected activity

See [SECURITY.md](../SECURITY.md) for full security policy.

---

## 📚 References

### Community Projects
- [lxp-bridge](https://github.com/celsworth/lxp-bridge) - Community protocol analysis
- [LuxPower HA Integration](https://github.com/ant0nkr/luxpower-ha-integration) - Home Assistant integration

### Standards & Specifications
- [Modbus RTU Specification](https://modbus.org/specs.php) - General Modbus protocol reference
- [RS-485 Standard](https://en.wikipedia.org/wiki/RS-485) - Serial communication standard

### Hardware
- [ESP32 Technical Reference](https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf)
- [RS485 Application Notes](docs/HARDWARE.md)

### Development
- [PlatformIO Documentation](https://docs.platformio.org/)
- [ESP32 Arduino Core](https://github.com/espressif/arduino-esp32)

---

**Document Version**: 1.0.0
**Last Updated**: December 11, 2024
**License**: GPL-3.0
