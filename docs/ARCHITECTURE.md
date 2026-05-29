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
│                     (Modbus integration)                        │
└────────────────────────────┬────────────────────────────────────┘
                             │ TCP (WiFi)
                             │ Port 8000
                             │ Protocol-compatible dongle
                             │
┌────────────────────────────┴────────────────────────────────────┐
│                         ESP32 OpenLux                           │
│  ┌──────────────┐   ┌──────────────┐   ┌──────────────┐         │
│  │ TCP Server   │──>│ Protocol     │──>│ RS485        │         │
│  │ (Port 8000)  │   │ Bridge       │   │ Manager      │         │
│  │              │<──│ Queue/Worker │<──│ Parser       │         │
│  └──────────────┘   └──────────────┘   └──────────────┘         │
│         ↑                                       ↓               │
│         │                                       │ RS485         │
│    WiFi Protocol                         Modbus-like Protocol   │
│    (inverter dongle format)              (19200 baud)           │
└─────────────────────────────────────────────────┬───────────────┘
                                                  │
                                                  │ A/B (RS485)
                                                  │
┌─────────────────────────────────────────────────┴───────────────┐
│                      Solar Inverter                             │
└─────────────────────────────────────────────────────────────────┘
```

### Component Breakdown

```
OpenLux ESP32 Firmware
├── System & Coordination (src/modules/)
│   ├── SystemManager       → Hardware operations (reboot, heap, watchdog, uptime)
│   ├── OperationGuard      → Lock manager for blocking operations (TCP/RS485/Network/WiFi)
│   └── CommandManager      → CLI commands via Telnet/Serial/Web API
│
├── Core Services (src/modules/)
│   ├── NetworkManager      → WiFi/Ethernet, OTA updates, hostname/mDNS
│   ├── Logger              → Dual output (Serial + Telnet on port 23)
│   └── NTPManager          → Time synchronization
│
├── User Interface (src/modules/)
│   ├── WebServerManager    → Web dashboard (port 80) + REST API
│   └── MqttManager         → MQTT telemetry + Home Assistant Auto-Discovery
│
├── Communication Layer (src/modules/)
│   ├── TCPServer           → Multi-client TCP server (port 8000, max 3)
│   ├── TCPProtocol         → WiFi protocol parser (A1 1A format)
│   ├── RS485Manager        → UART communication, pacing, response collection
│   └── InverterProtocol    → Inverter-specific protocol
│
├── Coordination Layer (src/modules/)
│   └── ProtocolBridge      → Bounded queue, single RS485 worker, cache/coexistence
│
└── Utilities (src/utils/)
    ├── CRC16               → CRC16-Modbus calculator
    └── SerialUtils         → Serial number utilities
```

### Module Descriptions

#### System & Coordination

**SystemManager** (`system_manager.h/cpp`)
- Hardware abstraction layer for system operations
- Watchdog management (enable/disable/feed)
- Reboot with reason tracking (stored in NVS)
- Diagnostics: heap, PSRAM, CPU frequency, flash size
- Uptime tracking in seconds
- System health checks in main loop

**OperationGuard** (`operation_guard.h/cpp`)
- RAII-based lock manager for expensive synchronous operations
- Prevents simultaneous execution of blocking operations
- Managed operations: TCP_CLIENT_PROCESSING, RS485_OPERATION, NETWORK_VALIDATION, WIFI_SCAN, OTA_OPERATION
- Automatic lock release when going out of scope

**CommandManager** (`command_manager.h/cpp`)
- Interactive CLI command system via Telnet/Serial
- The same command engine is exposed by the web dashboard at `POST /api/cmd`
- Built-in commands include `status`, `reboot`, `help`, `wifi_restart`, `wifi_reconnect`, `wifi_roam`, `wifi_scan`, `wifi_reset`, `probe_rs485`, `mqtt_status`, `ntp_sync`, `heap`, `tcp_clients`, `pause`, `resume`, `pause_status`, `cache_status`, `cache_info`, and `cache_clear`
- Extensible command registration system
- Command debouncing for critical operations
- Status reporting (uptime, memory, network, TCP, RS485, web, MQTT, cache, coexistence)

#### Core Services

**NetworkManager** (`network_manager.h/cpp`)
- Manages WiFi or Ethernet connectivity based on `OPENLUX_USE_ETHERNET` flag
- Handles OTA (Over-The-Air) firmware updates on port 3232
- Provides the `openlux` network hostname and registers mDNS when available
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

#### User Interface

**WebServerManager** (`web_server.h/cpp`)
- HTTP server on port 80 with basic authentication
- Minimal web dashboard with live status view
- REST API endpoints:
  - `GET /api/status` - System status JSON
  - `POST /api/cmd` - Execute CLI commands
- Configurable credentials in `config.h`
- HTML interface for quick troubleshooting
- Responsive design with real-time updates

**MqttManager** (`mqtt_manager.h/cpp`)
- MQTT client for Home Assistant integration (if `ENABLE_MQTT` enabled)
- Publishes system telemetry:
  - Device status (Online/Offline)
  - Uptime and last reboot reason
  - WiFi RSSI (signal strength)
  - IP address and hostname
  - Free heap memory
  - WiFi channel information
- Home Assistant MQTT Auto-Discovery support
- Remote command execution via MQTT topics
- Configurable broker, topics, and credentials in `config.h`

#### Communication Layer

**TCPServer** (`tcp_server.h/cpp`)
- Asynchronous TCP server on port 8000 (protocol-compatible dongle)
- Multi-client support (up to 3 simultaneous connections)
- Per-client timeout management (5 minutes default)
- Connection state tracking and cleanup
- Central ownership of `AsyncClient` close/removal lifecycle
- Deferred close/delete path: close requests are marked first, then clients are destroyed only after `AsyncClient::free()` reports it is safe
- Integration with ProtocolBridge for packet forwarding

**TCPProtocol** (`tcp_protocol.h/cpp`)
- Parser and builder for WiFi protocol compatible with standard dongle format
- Handles protocol versions (requests=2, responses=5)
- CRC16-Modbus validation
- Packet framing and serialization
- Support for all Modbus function codes (0x03, 0x04, 0x06, 0x10)
- Works over WiFi or Ethernet transparently

**RS485Manager** (`rs485_manager.h/cpp`)
- Hardware UART communication (`Serial1` in the current ESP32 build)
- Configurable TX/RX/DE pins
- Modbus-like protocol implementation
- Direction control (DE/RE pin) with auto-direction support; the default build uses `RS485_DE_PIN=-1` for auto-direction/no explicit DE pin
- 1024-byte UART RX ring buffer for full 125-register response frames
- Request pacing with a 120ms quiet gap between serialized RS485 transactions
- Response timeout defaults to 800ms
- Multi-frame parsing support for buffers that contain unrelated bus traffic
- Response matching by function code, start register, and register count
- Frame validation with CRC checking
- Inverter serial number detection with on-demand/auto retry probing when the link is down

**InverterProtocol** (`inverter_protocol.h/cpp`)
- Inverter-specific protocol implementation
- Packet structure handling and validation
- Register-level operations (read/write)
- Inverter-specific frame formatting
- Parser helpers for concatenated frames and matching a specific expected response
- Device identification and probing logic

#### Coordination Layer

**ProtocolBridge** (`protocol_bridge.h/cpp`)
- Central coordinator between TCP and RS485
- Bidirectional packet translation (WiFi <-> RS485)
- Fixed-size request queue and single RS485 worker so Home Assistant TCP framing is decoupled from the RS485 round-trip
- Explicit worker states: `QUEUED`, `RS485_SEND`, `RS485_RETRY`, `WAIT_RESPONSE`, `CACHE_FALLBACK`, `RESPOND_TCP`, `DONE`, `FAILED`
- Request routing and response correlation by function code, start register, and register count
- CRC validation on both protocols
- Register count validation (max 127 protocol register slots per request)
- Serial number extraction and forwarding
- Fallback read cache with 14 entries and a 45-second maximum fallback age
- Coexistence pressure mode: after repeated bus contention/corruption, OpenLux can briefly back off and serve fresh cached read responses up to 45 seconds old
- Protocol-compatible gateway exception (`0x0B`) when the inverter response is missing and no valid cache entry is available
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

OpenLux acts as a transparent bridge between Home Assistant and your solar inverter:

1. **TCP Connection**: Home Assistant connects to OpenLux on port 8000
2. **Request Reception**: TCPServer receives and frames complete Lux TCP packets
3. **Protocol Parsing**: TCPProtocol validates the A1 1A wrapper and CRC
4. **Queueing**: ProtocolBridge enqueues the request if the bridge is not paused and the queue has room
5. **Serialized RS485 Access**: a single worker sends one RS485 request at a time with pacing/retry guards
6. **Response Matching**: RS485Manager/InverterProtocol parse all received frames and pick the one matching function, start register, and count
7. **Fallback/Exception**: on missing or mismatched responses, the bridge uses cache when valid or sends a protocol-compatible exception
8. **TCP Response**: the selected RS485 response is wrapped back into the TCP protocol and sent to Home Assistant

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

### Dual-Dongle / Coexistence Mode

Coexistence with the official dongle is best-effort. OpenLux is still an RS485 master, so a second master on the same physical bus can create timing contention or expose electrical weaknesses.

The current firmware mitigates this by:
- Ignoring unrelated valid frames that do not match the active request.
- Matching read responses by function, start register, and register count.
- Detecting repeated contention/corruption events and opening a short backoff window.
- Serving fresh cached read responses during the pressure window when possible.
- Returning exception `0x0B` instead of dropping the TCP client when no valid response/cache exists.

These mitigations do not replace correct RS485 wiring, termination, failsafe biasing, common ground, polarity, shielding, and stable transceiver direction control.

### Implementation Notes

**Protocol Details**: The specific packet structures and protocol specifications are based on community reverse-engineering efforts and are kept in internal documentation to respect intellectual property concerns. The implementation follows standard Modbus RTU principles adapted for solar inverter equipment.

**References**:
- Community projects: [lxp-bridge](https://github.com/celsworth/lxp-bridge), [LuxPower HA Integration](https://github.com/ant0nkr/luxpower-ha-integration)
- Standard Modbus RTU specification (for general understanding)


---

## ⚙️ Configuration & Customization

### Compile-Time Configuration

Default firmware settings live in `src/config.h`. Site-specific values should go in `src/secrets.h` or `src/config.local.h`; local credentials must not be committed to git.

**Network Settings:**
- `WIFI_HOSTNAME` - Network hostname and mDNS label (default: "openlux")
- `OPENLUX_USE_ETHERNET` - Enable Ethernet instead of WiFi (0=WiFi, 1=Ethernet)
- `TCP_SERVER_PORT` - TCP server port (default: 8000, don't change!)
- `TCP_MAX_CLIENTS` - Maximum simultaneous clients (default: 3)
- `WEB_DASH_PORT` - Web dashboard port (default: 80)

**MQTT Settings** (if `ENABLE_MQTT` enabled):
- `MQTT_HOST` - MQTT broker IP/hostname
- `MQTT_PORT` - MQTT broker port (default: 1883)
- `MQTT_USER` / `MQTT_PASS` - MQTT credentials
- `MQTT_DISCOVERY_PREFIX` - Home Assistant MQTT discovery prefix (default: "homeassistant")
- `MQTT_TOPIC_PREFIX` - Topic prefix for device topics (default: "openlux")

**Hardware Configuration:**
- `RS485_TX_PIN`, `RS485_RX_PIN`, `RS485_DE_PIN` - UART pins
- `RS485_DE_PIN=-1` - Current default for auto-direction modules or modules without explicit DE/RE control
- `RS485_BAUD_RATE` - Fixed at 19200 per inverter protocol specification
- Ethernet PHY settings (if using Ethernet)

**Logging & Monitoring:**
- `OPENLUX_LOG_LEVEL` - 0=DEBUG, 1=INFO, 2=WARN, 3=ERROR, 4=NONE
- `TELNET_PORT` - Remote logging port (default: 23)

**Time Synchronization:**
- `NTP_SERVER_1/2/3` - Configurable NTP servers
- `TIMEZONE` - POSIX timezone format with DST support

**Watchdog & Recovery:**
- `WIFI_WATCHDOG_RECONNECT_DELAY_MS` - WiFi reconnection trigger
- `WIFI_WATCHDOG_RESTART_DELAY_MS` - WiFi interface restart trigger
- `WIFI_WATCHDOG_REBOOT_DELAY_MS` - Device reboot trigger
- `BOOT_FAIL_RESET_THRESHOLD` - Auto-reset credentials after N failed boots
- `RS485_MIN_REQUEST_GAP_MS` - Quiet gap between serialized RS485 requests (default: 120ms)
- `RS485_UART_RX_BUFFER_SIZE` - UART RX ring buffer size (default: 1024 bytes)
- `RS485_COEXISTENCE_ENABLED` - Enable best-effort dual-master coexistence mode
- `RS485_COEXISTENCE_TRIGGER_EVENTS` - Consecutive contention events before coex backoff
- `RS485_COEXISTENCE_BACKOFF_MS` - Backoff window after detected contention
- `RS485_COEXISTENCE_PRESSURE_WINDOW_MS` - Window where fresh cache is preferred
- `RS485_COEXISTENCE_CACHE_MAX_AGE_MS` - Maximum fresh-cache age during coex pressure

**Feature Flags:**
```cpp
#define ENABLE_NTP      // Time synchronization
#define ENABLE_OTA      // Wireless firmware updates
#define ENABLE_TELNET   // Remote logging
#define ENABLE_WEB_DASH // Web dashboard
#define ENABLE_MQTT     // MQTT telemetry publishing
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
- Runtime logging compiled in, with firmware/module defaults set to WARN

**openlux-debug** - Debug build
- Verbose logging (DEBUG level)
- Additional debug symbols
- Useful for development and troubleshooting

**openlux-serial** - Serial recovery/upload
- USB upload using `esptool`
- Useful for first flash or recovery when OTA is unavailable

### Build Scripts

**scripts/build_info.py**
- Auto-generates `src/build_info.h` with:
  - Build timestamp
  - Git commit hash (if in git repo)
- Runs before each compilation

### Dependencies

Managed via PlatformIO `lib_deps`:
- `ESP32Async/AsyncTCP` - Async TCP library
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
nc openlux 23
# or
telnet openlux 23
```

Features:
- Same output as serial console
- Multiple simultaneous clients
- No physical connection required
- Interactive command execution

### CLI Commands

Available via Serial or Telnet:

| Command | Description |
|---------|-------------|
| `status` | Show link, network, TCP, RS485, web, firmware, cache, and coexistence status |
| `reboot` | Restart the device |
| `help` | Show available commands |
| `wifi_restart` / `wifi_reconnect` | Restart or softly reconnect WiFi |
| `wifi_roam` / `wifi_scan` | Force roaming scan or list visible APs |
| `wifi_reset` | Clear WiFi credentials and open provisioning portal |
| `probe_rs485` | Probe inverter serial registers |
| `mqtt_status` | Show MQTT connection status if MQTT is enabled |
| `ntp_sync` | Force NTP synchronization |
| `heap` | Show heap/PSRAM diagnostics |
| `tcp_clients` / `tcp_clients drop` | Inspect or disconnect TCP clients |
| `pause` / `resume` / `pause_status` | Pause/resume RS485 bridge activity for maintenance |
| `cache_status` / `cache_info` / `cache_clear` | Inspect or clear fallback cache |

### Web Dashboard

Access via browser:
```
http://openlux
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
#define OPENLUX_LOG_LEVEL 2  // 0=DEBUG, 1=INFO, 2=WARN, 3=ERROR, 4=NONE
```

- **DEBUG (0)**: Detailed packet dumps, frame-by-frame RS485 communication
- **INFO (1)**: Normal operations, connections, status updates
- **WARN (2)**: Warnings and recoverable errors (default)
- **ERROR (3)**: Critical errors only
- **NONE (4)**: No logging (not recommended)

Runtime logging can be changed without reflashing through `log_level`, for example `log_level 0`, `log_level rs485 0`, or `log_level reset`.

---

## 📊 Performance Characteristics

### Resource Usage

**Memory (ESP32 standard)**
- RAM: ~53 KB / 328 KB (about 16%)
- Flash: ~1.05 MB / 1.31 MB (about 80%)
- PSRAM: Not required

**Network**
- TCP connections: Max 3 simultaneous clients
- Packet latency is dominated by the RS485 round-trip and Home Assistant polling pattern
- Web status is cached briefly to reduce contention with the TCP/RS485 bridge path
- TCP client shutdown is centralized in `TCPServer` so protocol errors, timeouts, and bridge failures all follow the same deferred `AsyncClient` cleanup path

### Stability

**Watchdog Protection:**
- WiFi reconnection after 2 minutes offline
- WiFi interface restart after 5 minutes offline
- Device reboot after 10 minutes offline

**Error Handling:**
- CRC validation on all packets (TCP + RS485)
- Timeout protection on RS485 (800ms)
- Client timeout on TCP (5 minutes)
- RS485 send retry path before cache/exception fallback

**Recovery Mechanisms:**
- Boot failure counter (auto-reset after N fails)
- Network watchdog with staged recovery
- RS485 request pacing, retry, cache fallback, and coexistence backoff
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
- [RS485 Application Notes](HARDWARE.md)

### Development
- [PlatformIO Documentation](https://docs.platformio.org/)
- [ESP32 Arduino Core](https://github.com/espressif/arduino-esp32)

---

**Document Version**: 2.0.0
**Last Updated**: May 27, 2026
**License**: GPL-3.0
