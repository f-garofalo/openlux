# OpenLux Network Bridge

[![Version](https://img.shields.io/badge/Version-1.0.1-green.svg)](./src/config.h)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![ESP32](https://img.shields.io/badge/ESP32-Supported-blue.svg)](https://www.espressif.com/en/products/socs/esp32)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-Ready-orange.svg)](https://platformio.org/)

> **Open-source network bridge (WiFi/Ethernet) for Luxpower inverters** - Connect your Luxpower/EG4 inverter to Home Assistant without proprietary hardware!
> **Status**: in active development/testing; so far tested only by the author. More field testing and scenarios are needed.
> **Note**: for study/personal use only, not intended for sale.
> **No affiliation**: This project is independent and not endorsed by LuxpowerTek. LuxpowerTek, and EG4 are trademarks of their respective owners. This project is not affiliated with, sponsored by, or endorsed by these companies.

---

## 🎯 Overview

**OpenLux** is an ESP32-based firmware that implements a Luxpower-compatible (unofficial) WiFi dongle protocol, enabling direct communication between your Luxpower/EG4 inverter and Home Assistant. This open-source solution eliminates the need for proprietary hardware and provides full local control of your solar installation.

**Why OpenLux?**
- 🆓 Free & Open Source - No expensive proprietary dongles
- 🔒 Local Control - No cloud dependencies
- 🏡 Home Assistant Native - Seamless integration
- 🔧 Fully Customizable - Modify and extend as needed

**Supported Inverters:**
- Luxpower SNA-6000, SNA-12000
- Any inverter using the Luxpower RS485 protocol

---

## ✨ Features

- ✅ **Network Bridge (WiFi/Ethernet)** - Implements a Luxpower-compatible (unofficial) dongle over TCP port 8000
- ✅ **RS485 Communication** - Full Modbus RTU protocol support
- ✅ **Protocol Translation** - Seamless WiFi ↔ RS485 conversion
- ✅ **Read/Write Operations** - Full register access (0x03, 0x04, 0x06, 0x10)
- ✅ **Multi-client** - Up to 5 simultaneous connections
- ✅ **Minimal Web Dashboard** - Basic status + command runner on port 80 (with basic auth)
- ✅ **Dual Dongle Mode** - Can operate simultaneously with the official WiFi dongle using a breakout board
- ✅ **Smart WiFi Roaming** - Automatically connects to the strongest AP and periodically scans for better signals (mesh-friendly)

---

## 🔌 Dual Dongle Operation

OpenLux can now operate simultaneously with the original manufacturer's WiFi dongle. By using a custom breakout board (splitter), you can connect the official dongle to the inverter and tap into the RS485 signals (A/B/5V/GND) to feed them into OpenLux.

This allows you to:
- Keep the official cloud monitoring and app functionality active.
- Use OpenLux for local Home Assistant integration at the same time.
- OpenLux is smart enough to ignore the official dongle's traffic and only respond to its own requests, avoiding collisions.

---

## 🚫 Not Included

- Official inverter firmware upgrades: OpenLux does not handle updating the inverter's official firmware. To update, temporarily reinstall the original vendor dongle, perform the firmware upgrade, then reconnect OpenLux.

---

## 🛠️ Hardware

Hardware documentation (BOM, pinout, RS485 wiring, Ethernet notes) is in [`docs/HARDWARE.md`](docs/HARDWARE.md), with detailed wiring diagrams.

---

## 🚀 Quick Start

### 1. Clone & Install

```bash
git clone https://github.com/f-garofalo/openlux.git
cd openlux
```

### (Optional) Isolate tools with a Python venv

```bash
#Requires Python 3.10+ on your system
python -m venv .venv           # create local venv
source .venv/bin/activate      # Windows: .venv\Scripts\activate
pip install --upgrade pip
pip install platformio         # installs PlatformIO CLI and dependencies
#When finished, exit the venv with:
deactivate
```

### 2. Configure

```bash
#Copy secrets template
cp src/secrets.h.example src/secrets.h

#Edit with your OTA password (WiFi creds can be set via portal)
nano src/secrets.h
```

**Also edit `src/config.h`** to customize:
- RS485 pins (if different from GPIO 16, 17, 4)
- Network mode: `OPENLUX_USE_ETHERNET` (0=WiFi, 1=Ethernet)
- WiFi portal (SSID/PASS/TIMEOUT) and log level
- NTP servers and timezone
- Watchdog timings (WiFi watchdog, boot-fail reset threshold)

See [Configuration] section below for details.

### 3. Build & Upload

```bash
#First upload(USB)
pio run -e openlux -t upload

#Monitor logs
pio device monitor -b 115200
```

### 4. OTA Updates

After first upload, update wirelessly:

```bash
#Edit platformio.ini with your ESP32 IP or use openlux.local
pio run -e openlux-ota -t upload
```

---

## 🖥️ Web Dashboard (Port 80)

- Access: `http://openlux.local` (or your device IP) on port `80`.
- Auth: basic auth enabled by default — user: `admin`, password: `openlux` (change in `src/config.h` via `WEB_DASH_USER`/`WEB_DASH_PASS`).
- Pages/APIs: minimal HTML dashboard at `/` with live status view (`/api/status`) and a simple command runner (`/api/cmd`) that forwards to the same command engine used by Telnet.
- Purpose: quick troubleshooting (check status, run commands like `status`, `reboot`, `help`) without needing Telnet/HA.

---

## 🏡 Home Assistant Integration

OpenLux works with the [LuxPower Modbus Integration](https://github.com/ant0nkr/luxpower-ha-integration).

**Quick Setup:**
1. Install via HACS: `https://github.com/ant0nkr/luxpower-ha-integration`
2. Add Integration → "LuxPower Inverter (Modbus)"
3. Configure: IP (`openlux.local`), Port (`8000`), Serial Numbers

---

## ⚙️ Configuration

### Network Settings

- WiFi: leave `WIFI_SSID`/`WIFI_PASSWORD` empty to trigger the captive portal at first boot (portal SSID/PASS in `config.h`). Set them in `src/secrets.h` if you prefer preconfigured WiFi.
- OTA password: set `OTA_PASSWORD` in `src/secrets.h`.
- Static IP (WiFi): configure in `config.h` via `setStaticIP` params; portal also supports static IP when preconfigured.
- Ethernet: set `OPENLUX_USE_ETHERNET` to `1` and adjust ETH PHY/pins in `config.h`.

### Hardware Pins

Default pins in `src/config.h` (change if needed):
```cpp
#define RS485_TX_PIN 17
#define RS485_RX_PIN 16
#define RS485_DE_PIN 4
```

Ethernet pins/PHY are also in `config.h` (only used when `OPENLUX_USE_ETHERNET=1`).

### Board Selection

OpenLux supports all ESP32 variants. Edit `platformio.ini`:
```ini
[env]
board = esp32dev              # Standard ESP32
#board = esp32 - s3 - devkitc - 1 #ESP32 - S3
#board = esp32 - c3 - devkitm - 1 #ESP32 - C3
```

---

## 🔧 Development

### Pre-commit Hooks (Recommended)

This project uses [pre-commit](https://pre-commit.com/) to enforce code quality and prevent committing sensitive files.

**Quick setup:**
```bash
pip install pre-commit
pre-commit install
```

The hooks will automatically:
- ✅ Prevent committing `secrets.h` and other sensitive files
- ✅ Format C++ code with clang-format
- ✅ Check for common issues (trailing whitespace, large files, etc.)

For complete documentation, see: https://pre-commit.com/

### Build Commands

```bash
#Build
pio run -e openlux

#Upload(USB)
pio run -e openlux -t upload

#Upload(OTA)
pio run -e openlux-ota -t upload

#Debug build
pio run -e openlux-debug -t upload
```

### Monitoring

```bash
#Serial monitor
pio device monitor -b 115200

#WiFi monitor
nc openlux.local 23
```

---

## 🧪 Testing Status & Compatibility

### Current Testing Coverage

⚠️ **Limited Field Testing**: This project is currently in **BETA** stage with limited real-world validation.

**Tested Configuration:**
- ✅ Inverter: Luxpower SNA-6000
- ✅ ESP32 Board: ESP32-DevKitC (standard ESP32)
- ✅ RS485 Module: MAX485-based transceiver
- ✅ Network: WiFi (2.4GHz)
- ✅ Integration: Home Assistant with LuxPower Modbus Integration
- ✅ Operations: Read registers (0x03, 0x04) - Extensively tested
- ⚠️ Operations: Write registers (0x06, 0x10) - Basic testing only

**Untested/Unknown:**
- ❓ Other inverter models (SNA-12000, EG4 18kPV, etc.)
- ❓ Different ESP32 variants (ESP32-S3, ESP32-C3)
- ❓ Different RS485 modules (SP485, auto-direction modules)
- ❓ Ethernet connectivity (code present but untested)
- ❓ Extended runtime stability (>1 week continuous operation)
- ❓ Edge cases and error recovery scenarios
- ❓ High-frequency polling (Home Assistant default is 30s)

### Help Needed! 🙏

**We need community testing to make this project stable!**

If you can test OpenLux on different hardware, please:
1. Open a [Hardware Compatibility Issue](https://github.com/f-garofalo/openlux/issues/new?template=hardware_compatibility.md)
2. Report your configuration (inverter model, ESP32 board, RS485 module)
3. Share your results (what works, what doesn't)
4. Help identify bugs and edge cases

**Your testing helps everyone!** The more diverse hardware configurations we validate, the more reliable the project becomes.

See [CONTRIBUTING.md](CONTRIBUTING.md) for testing guidelines.

---

## 🤝 Contributing

Contributions welcome! **Testing on different hardware is the #1 priority right now.**

Areas for contribution:
- 🧪 **Testing on different inverters** (MOST NEEDED!)
- 🧪 **Testing on different ESP32 boards**
- 🧪 **Testing with Ethernet**
- 🐛 Bug fixes
- 📝 Documentation improvements
- ✨ New features

---

## 📄 License

This project is licensed under **GPL-3.0**.

**What this means:**
- ✅ Use, modify, and distribute freely
- ✅ Commercial use allowed
- 📋 Must share source code when distributing
- 📋 Must keep same license

See [LICENSE](LICENSE) for full terms and [LEGAL.md](LEGAL.md) for complete legal information, disclaimers, and trademark acknowledgments.

---

## ⚠️ Disclaimer

**USE AT YOUR OWN RISK**

This firmware involves electrical equipment and solar inverters. Improper use may:
- Damage equipment
- Void warranties
- Cause electrical hazards

**Important for inverter safety:**
- Writing incorrect registers/values can disrupt inverter operation or damage batteries/components. Always verify register maps and values before issuing write commands.
- Verify RS485 wiring and polarity (A/B) and use shielded cable; incorrect wiring can lead to communication faults.
- Test changes incrementally and monitor inverter status after any write.
- No support or liability is provided; you are responsible for any impacts on your inverter/installation.

**Safety First:**
- ⚡ Follow electrical safety guidelines
- 📖 Consult inverter manual
- 🛡️ Test in safe environment
- ⚠️ No warranty provided

---

## 🚀 Future Development

OpenLux is actively evolving! Here are planned enhancements for future releases:

### Hardware Improvements

**🔴 Status LEDs**
- Visual indication of system status
- Multi-color LED for different states:
  - 🟢 Green: Connected and running
  - 🟡 Yellow: WiFi connecting/RS485 error
  - 🔴 Red: System error
  - 🔵 Blue: OTA update in progress
- Configurable LED pin in `config.h`

**🔘 Physical Buttons**
- **Reset/Reboot button**: Quick device restart without power cycle
- **WiFi reset button**: Clear credentials and restart portal (long press)
- **Safe mode button**: Boot without network for troubleshooting
- Configurable GPIO pins for maximum flexibility

**📟 Custom PCB Design**
- Integrated all-in-one board design:
  - ESP32 module (ESP32-WROOM or ESP32-S3)
  - Built-in RS485 transceiver (isolated)
  - Status LEDs and control buttons
  - HDMI connector for direct inverter connection (pins compatible with Luxpower)
  - Screw terminals for A/B RS485 (alternative to HDMI)
  - USB-C for power and programming
  - Compact form factor for easy installation
- Open hardware design (KiCad files)
- Optional 3D-printed enclosure

### Software Features

**📊 Enhanced Web Dashboard**
- Real-time inverter data visualization
- Configuration management via web interface
- Network settings without editing files
- Firmware update via web upload

**📈 Data Logging**
- Local data storage (SD card or SPIFFS)
- Historical data graphs
- Export to CSV/JSON
- Optional MQTT publishing

**🔔 Alerts & Notifications**
- Configurable alerts for inverter errors
- Push notifications (Telegram, email)
- Watchdog notifications
- RS485 communication failure alerts

**🌐 Additional Protocols**
- MQTT support for advanced integrations
- REST API for custom applications
- WebSocket for real-time updates

### Community Requests

Have a feature request? We'd love to hear it!
- Open a [Feature Request](https://github.com/f-garofalo/openlux/issues/new?template=feature_request.md)
- Join the [Discussions](https://github.com/f-garofalo/openlux/discussions)
- Check the [Project Roadmap](https://github.com/f-garofalo/openlux/projects) for planned features

**Want to contribute?** See [CONTRIBUTING.md](CONTRIBUTING.md) - hardware designs, PCB layouts, and code contributions are all welcome!

---

## 🙏 Acknowledgments

**Inspired by:**
- [LuxPower Protocol](https://github.com/celsworth/lxp-bridge) - Protocol reverse engineering
- [LuxPower HA Integration](https://github.com/ant0nkr/luxpower-ha-integration) - Home Assistant integration

**Built with:**
- [ESP32](https://www.espressif.com/en/products/socs/esp32) - Microcontroller
- [PlatformIO](https://platformio.org/) - Development platform
- [AsyncTCP-esphome](https://github.com/esphome/AsyncTCP) - TCP library

---

<div align="center">

**Made with ❤️ for the solar community**

[Report Bug](https://github.com/f-garofalo/openlux/issues) · [Request Feature](https://github.com/f-garofalo/openlux/issues) · [Documentation](https://github.com/f-garofalo/openlux/wiki)

⭐ **Star this project if you find it useful!** ⭐

</div>
