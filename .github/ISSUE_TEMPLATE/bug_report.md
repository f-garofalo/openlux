---
name: Bug Report
about: Report a bug or issue with OpenLux
title: '[BUG] '
labels: bug
assignees: ''
---

## 🐛 Bug Description

<!-- A clear and concise description of the bug -->

## 📋 Steps to Reproduce

1. <!-- First step -->
2. <!-- Second step -->
3. <!-- Third step -->

## ✅ Expected Behavior

<!-- What you expected to happen -->

## ❌ Actual Behavior

<!-- What actually happened -->

## 🔧 Hardware Configuration

**ESP32 Board:**
- Board model: <!-- e.g., ESP32-DevKitC, ESP32-S3, etc. -->
- Board variant: <!-- from platformio.ini -->

**RS485 Module:**
- Model: <!-- e.g., MAX485, SP485, XY-017 -->
- DE/RE pin used: <!-- GPIO number or "auto-direction" -->

**Inverter:**
- Brand/Model: <!-- e.g., Luxpower SNA-6000, EG4 18kPV -->
- Firmware version: <!-- if known -->
- RS485 connection: <!-- CT port or HDMI port -->

**Network:**
- Connection type: <!-- WiFi or Ethernet -->
- Network mode: <!-- DHCP or Static IP -->

## 📊 Logs

<!-- Include relevant logs from Serial or Telnet -->

<details>
<summary>Serial/Telnet Logs</summary>

```
Paste logs here
```

</details>

## 🔍 Additional Context

**Firmware Version:** <!-- e.g., 1.0.0 from config.h -->

**Configuration:**
- `OPENLUX_LOG_LEVEL`: <!-- e.g., 1 (INFO) -->
- `RS485_TX_PIN`, `RS485_RX_PIN`, `RS485_DE_PIN`: <!-- pin numbers -->
- Any other modified settings in `config.h`

**Screenshots:**
<!-- If applicable, add screenshots -->

## ✔️ Checklist

- [ ] I've checked existing issues and this is not a duplicate
- [ ] I've included all requested information
- [ ] I've attached relevant logs
- [ ] I've tested with the latest version
