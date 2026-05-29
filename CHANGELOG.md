# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]
- Placeholder for future changes.

## [2.0.0] - 2026-05-29
### Added
- **RS485 worker queue**: TCP requests are now parsed and queued, while a single bridge worker serializes all inverter access.
- **Bridge request state machine**: each active request moves through explicit worker states (`QUEUED`, `RS485_SEND`, `RS485_RETRY`, `WAIT_RESPONSE`, `CACHE_FALLBACK`, `RESPOND_TCP`, `DONE`, `FAILED`) for clearer diagnostics.
- **Bridge queue diagnostics**: `status` now reports worker state, active request id, queue depth, queued requests, queue drops, client-disconnect drops, and the last completed request outcome.
- **RS485 coexistence pressure window**: after repeated contention/corruption events, OpenLux can briefly prefer fresh cached read responses instead of immediately adding more traffic to the bus.
- **Coexistence diagnostics**: status output includes `coex_events`, `coex_cache`, `coex_stale`, and `coex_miss` counters to show whether the coexistence path is actually being used.

### Changed
- **TCP framing decoupled from RS485 round-trip**: the TCP server no longer stops framing client data just because the bridge is busy; complete frames are handed to the worker queue and consumed one at a time by the RS485 side.
- **Bridge busy handling**: close/reject behavior is now reserved for pause, blocking maintenance operations, invalid frames, or queue-full pressure instead of normal in-flight RS485 requests.
- **Fixed-size bridge queue**: the worker queue uses a bounded ring buffer instead of a dynamic STL deque, reducing heap churn on the ESP32.
- **Fallback cache housekeeping**: cache eviction now uses last-access time consistently and cache invalidation counters include evictions/manual clears.
- **WiFi link is now authoritative**: removed periodic active probes to MQTT and gateway ports from the network connection check.
- **WiFi watchdog cleanup**: runtime recovery now has a single clear path (`reconnect` after 2 minutes, WiFi interface restart after 5 minutes, device reboot after 10 minutes).
- **RS485 pacing**: serialized inverter requests keep a 120ms quiet gap between transactions to reduce corrupted responses during polling bursts.
- **Fallback cache headroom**: cached read-response capacity is 14 entries.
- **Fresh-cache coexistence policy**: during the coexistence pressure window, cached read responses must be fresh enough for active Home Assistant polling rather than using the longer fallback-cache TTL.
- **Dual-dongle wording**: coexistence is treated as best-effort RS485 contention handling, not a guarantee that two masters can always share a noisy or improperly biased bus.
- **Fallback cache freshness**: generic RS485 fallback cache responses now expire after 45 seconds instead of 10 minutes, so stale inverter data is rejected with a protocol exception rather than shown as current data.

### Fixed
- **Protocol-compatible RS485 failure handling**: when an RS485 response is missing/mismatched and no fallback cache is available, OpenLux returns a Lux/Modbus exception `0x0B` (`Gateway Target Device Failed to Respond`) instead of immediately closing the Home Assistant TCP connection.
- **RS485 full-frame reception**: enlarged the ESP32 UART RX ring buffer so 125-register Lux responses fit without overrunning the default serial buffer.
- **CRC handling**: responses with CRC mismatch are no longer accepted as successful frames.
- **RS485 request classification**: external-master request detection requires a valid function code and CRC, avoiding false request frames from response payloads.
- **RS485 retry semantics**: an initially busy RS485 send now enters the retry path before using fallback cache.
- **Count-aware RS485 response matching**: response correlation now matches function code, start register, and register count.
- **Mismatch diagnostics**: response mismatch warnings include the expected and received register counts.
- **TCP disconnect noise**: late bytes received after a TCP disconnect are logged at DEBUG instead of WARN.

## [1.1.0] - 2026-05-22
### Added
- **WiFi diagnostics in status**: `status` now reports AP BSSID, power-save state, connect/disconnect counters, gateway validation state, and the last WiFi disconnect reason/age.
- **Web status diagnostics**: `status` now reports `/api/status` request count, cache hits, cache TTL, last build/total time, and slow response count.
- **TCP listener health diagnostics**: `status` now reports listener state, client count, restart count, and self-probe health counters.
- **Hardware reset reason in status**: `status` now includes the ESP32 reset reason and code so watchdog, brownout, panic, and power-on resets are visible after reboot.

### Changed
- **Production runtime logging control**: production and serial builds now compile `DEBUG`/`INFO`/`WARN` logging in, while preserving firmware log-level filters so diagnostics can be enabled at runtime.
- **Quiet production log defaults**: all module log-level defaults now start at `WARN`, so reboot/reset keeps rsyslog quiet while runtime diagnostics remain available with `log_level 0` or `log_level <tag> <level>`.
- **WiFi power-save disabled explicitly**: WiFi station mode now disables modem sleep via both Arduino WiFi and ESP-IDF power-save APIs to reduce delayed TCP SYN/ACK responses on a busy bridge.
- **Cached `/api/status` responses**: the web dashboard/API now serves a short-lived cached status response for rapid repeat requests, reducing contention with the Lux TCP bridge on the ESP32 main loop.
- **WiFi TX power tuning**: default station transmit power is now `WIFI_POWER_15dBm`, with power reapplied during WiFi handshakes and reported accurately in `status`.
- Added `ProtocolBridge::is_busy()` so the TCP server can avoid feeding new requests into the bridge while an RS485 transaction is in flight.

### Fixed
- **TCP stream framing for Home Assistant**: `TCPServer::process_client_data()` now uses the declared Lux TCP `frame_length` to process one complete frame at a time, preserving any coalesced bytes for the next loop instead of clearing the full RX buffer.
- **Backpressure while RS485 is busy**: TCP frames received while the bridge is waiting for an inverter response are kept buffered instead of being passed to the bridge and rejected as `Bridge busy`.
- **RS485 send backoff**: when an RS485 request cannot be sent immediately, OpenLux now keeps the TCP client open and retries the same request for a short window before falling back to cache/error handling.
- **Stale error forwarding guard**: error handling no longer wraps and forwards a previous raw RS485 response unless it matches the current request, preventing unrelated stale data from being returned after a send failure.
- **Fallback cache counters**: cache hits and misses are now counted consistently in diagnostics.
- **TCP listener self-healing**: when the TCP listener is accepting connections but repeated local self-probes fail, OpenLux now restarts only the TCP listener instead of rebooting the whole device or leaving Home Assistant to hit repeated connect timeouts.
- **Runtime log level command**: `log_level <0-4>` now clears module overrides and applies the selected level to every tag, so the command actually changes runtime verbosity instead of being masked by per-module defaults.
- **Runtime module log overrides**: module tags set through `log_level <tag> <level>` are now copied into logger storage, avoiding dangling pointers from command strings.
- **TCP logger default override**: the `OPENLUX_LOG_LEVEL_TCP` default now applies to both the TCP server tag (`tcp`) and TCP protocol parser tag (`tcp_proto`).
- **Filtered log overhead**: log macros now check the effective runtime level before evaluating formatted arguments, avoiding expensive `format_hex(...)` work when verbose logs are disabled.
- **Network validation false negatives**: failed gateway/MQTT probes are diagnostics while the WiFi STA link remains associated, reducing spurious reconnects and avoidable `ASSOC_LEAVE` events.
- **Gateway diagnostic reset on WiFi association**: when the ESP32 reconnects to the AP, the gateway reachability flag returns to optimistic `ok` until the next active validation.
- **WiFi credentials source of truth**: firmware-configured WiFi credentials now take precedence over stale ESP32 NVS credentials, including reconnect and interface restart paths.
- **WiFi credentials source alignment**: generated firmware headers and local configuration now use the same credential source.
- **WiFi power-save status accuracy**: the reported PS state now comes from the ESP-IDF WiFi driver after forcing `WIFI_PS_NONE`.
- **Reconnect service reinitialization**: OTA and mDNS setup are now idempotent across WiFi reconnects, preventing repeated setup failures after link recovery.
- **Auth-failure reconnect dampening**: repeated WiFi auth/association disconnect events now defer explicit reconnect scans briefly, reducing reconnect storms while the ESP32 WiFi stack is already retrying.
- **WiFi TX power status display**: status output now recognizes observed ESP32 raw TX power values for `8.25 dBm` and `14.75 dBm`.

## [1.0.7] - 2026-04-17
### Security
- **TCP input hardening**: `parse_request` now validates the declared `frame_length` against the actual packet size and requires `byte_count == register_count*2` on write-multi, blocking confused-deputy parsing from malformed/malicious TCP frames.
- **Bounds checks on response build**: `TcpProtocol::build_response` and `build_rs485_write_multi` guard against NULL/oversized input and pre-check destination bounds before every `memcpy`.
- **DoS protection**: per-client TCP RX buffer is capped at 4 KiB (`MAX_RX_BUFFER_SIZE`); clients that exceed it are disconnected, preventing slow-drip heap exhaustion.

### Fixed
- **Latent use-after-free in ProtocolBridge**: the bridge used to cache a raw `TCPClient*` pointing into a `std::vector` that can move (on `push_back`) or be erased (on timeout). Now stores the heap-stable `AsyncClient*` handle plus an IP snapshot, and re-resolves to a live `TCPClient` via the new `TCPServer::resolve_client()` at every send point.
- **Dual-master collision recovery**: on a response mismatch in `handle_rs485_success`, the bridge now tries the fallback cache before returning an error to Home Assistant.
- `ReadCacheEntry::hit_count` widened from `uint8_t` to `uint32_t` (no wrap after 256 hits).

### Changed
- `rs485_manager` TX/RX log paths replace `String` concatenation with stack-buffer `snprintf`, reducing heap fragmentation on the ESP32.

## [1.0.6] - 2025-12-31
### Added
- **InverterProtocol Module**: New dedicated inverter protocol implementation
- **Bridge Pause/Resume Commands**: Manual maintenance mode for firmware updates
    - Use case: Official dongle firmware update without disconnecting OpenLux
- Fallback cache for RS485 read operations
- External device detection and logging on RS485 bus
- RS485_BUS_BUSY collision avoidance
- Cache management commands (`cache_status`, `cache_clear`, `cache_info`)

### Changed
- RS485 request handling now falls back to cache during bus contention

### Fixed
- Dual dongle mode stability: eliminated cascading timeout errors
- HomeAssistant no longer sees transient errors during RS485 collisions

## [1.0.5] - 2025-12-24
### Added
- Implement operation guard for managing concurrent operations; refactor WiFi scanning and network validation

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
- Initial public release of OpenLux firmware (ESP32-based WiFi bridge).
- TCP bridge on port 8000 (protocol dongle), multi-client support.
- RS485 communication with protocol translation.
- RS485 manager with read/write registers, protocol bridge, and command engine.
- Minimal web dashboard on port 80 with status and command runner (basic auth).
- Telnet logging on port 23; NTP/time sync; configurable Ethernet/WiFi.
- Hardware docs and setup instructions (RS485 wiring, HDMI pinout).
