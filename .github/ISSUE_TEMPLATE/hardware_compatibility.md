---
name: Hardware Compatibility Report
about: Report testing results on specific hardware configurations
title: '[HARDWARE] '
labels: hardware, testing
assignees: ''
---

## 🔧 Hardware Configuration

**ESP32 Board:**
- Manufacturer: <!-- e.g., Espressif, DOIT, etc. -->
- Model: <!-- e.g., ESP32-DevKitC, ESP32-S3-DevKitC-1 -->
- Chip variant: <!-- ESP32, ESP32-S2, ESP32-S3, ESP32-C3 -->
- Flash size: <!-- e.g., 4MB -->
- PSRAM: <!-- Yes/No, size if applicable -->

**RS485 Module:**
- Model: <!-- e.g., MAX485, SP485, XY-017, XY-K485 -->
- Direction control: <!-- Manual (DE/RE pin) or Auto -->
- Voltage: <!-- 3.3V or 5V -->
- Vendor/Link: <!-- Where purchased -->

**Inverter:**
- Brand: <!-- e.g., Luxpower, EG4 -->
- Model: <!-- e.g., SNA-6000, SNA-12000, EG4 18kPV -->
- Firmware version: <!-- If known -->
- RS485 port used: <!-- CT port or HDMI port -->

**Network:**
- Connection: <!-- WiFi or Ethernet -->
- Router/AP model: <!-- If relevant -->

## 🔌 Wiring Configuration

**Pin Connections:**
```
RS485_TX_PIN: [GPIO number]
RS485_RX_PIN: [GPIO number]
RS485_DE_PIN: [GPIO number or -1]
```

**RS485 to Inverter:**
- Cable type: <!-- e.g., Shielded twisted pair, Cat5e -->
- Cable length: <!-- e.g., 2 meters -->
- Termination resistor: <!-- Yes/No -->

## ✅ Testing Results

**Working Features:**
- [ ] WiFi connection
- [ ] Ethernet connection (if applicable)
- [ ] RS485 communication
- [ ] Read registers (0x03/0x04)
- [ ] Write single register (0x06)
- [ ] Write multiple registers (0x10)
- [ ] TCP server (port 8000)
- [ ] Web dashboard (port 80)
- [ ] Telnet logging (port 23)
- [ ] OTA updates
- [ ] mDNS (openlux.local)
- [ ] NTP time sync

**Issues Encountered:**
<!-- Describe any problems or limitations -->

## 📊 Performance

- **Stability:** <!-- e.g., Running for X days without issues -->
- **Response time:** <!-- e.g., Fast/Normal/Slow -->
- **Reconnection:** <!-- How well does it handle network/RS485 disconnections? -->

## 🔍 Home Assistant Integration

- **Integration used:** <!-- e.g., LuxPower Modbus Integration -->
- **Entities discovered:** <!-- Number of entities -->
- **Update frequency:** <!-- How often does HA poll? -->
- **Issues:** <!-- Any integration-specific problems? -->

## 📝 Additional Notes

<!-- Photos of setup, configuration tips, gotchas, etc. -->

## ✔️ Recommendation

- [ ] ✅ **Recommended** - Works perfectly
- [ ] ⚠️ **Works with caveats** - See issues above
- [ ] ❌ **Not recommended** - Major issues
