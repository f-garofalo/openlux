# Security Policy

## 🔒 Reporting Security Vulnerabilities

The OpenLux project takes security seriously. If you discover a security vulnerability, please follow responsible disclosure practices.

### Reporting Process

**DO NOT** create a public GitHub issue for security vulnerabilities.

Instead, please report security issues by:

1. **Email**: Send details to the project maintainer
2. **GitHub Security Advisory**: Use the [Security Advisory](https://github.com/f-garofalo/openlux/security/advisories/new) feature (preferred)

### What to Include

When reporting a vulnerability, please include:

- Description of the vulnerability
- Steps to reproduce the issue
- Potential impact and severity
- Suggested fix (if known)
- Your contact information for follow-up

### Response Timeline

- **Initial Response**: Within 48 hours
- **Status Update**: Within 7 days
- **Fix Timeline**: Varies by severity (critical issues prioritized)

## 🛡️ Security Considerations

### Hardware Security

OpenLux is designed for local network use. Be aware:

- **Network Exposure**: The TCP server (port 8000) and web dashboard (port 80) are exposed on your network
- **Authentication**: Web dashboard uses basic HTTP auth (not HTTPS by default)
- **RS485 Access**: The device has direct access to your inverter via RS485

### Best Practices

#### Network Security

1. **Isolate on Local Network**: Do not expose OpenLux directly to the internet
2. **Use Firewall**: Configure your router to block external access
3. **Secure WiFi**: Use WPA2/WPA3 encryption on your WiFi network
4. **Change Default Passwords**: Update `WEB_DASH_USER` and `WEB_DASH_PASS` in `config.h`
5. **Secure OTA**: Change `OTA_PASSWORD` in `secrets.h` to a strong password

#### Physical Security

1. **Protect the Device**: Physical access = full control
2. **Secure Location**: Install in a protected area
3. **Serial Console**: Serial port provides unrestricted access

#### Inverter Protection

1. **Read-Only by Default**: Limit write access to trusted clients only
2. **Validate Commands**: Always verify register addresses before writing
3. **Monitor Logs**: Check logs regularly for unexpected activity
4. **Backup Configuration**: Save your inverter settings before using OpenLux

### Known Limitations

OpenLux is a community project with inherent limitations:

- ⚠️ **No HTTPS**: Web dashboard uses unencrypted HTTP
- ⚠️ **Basic Auth**: Web authentication is minimal (basic HTTP auth)
- ⚠️ **No User Management**: Single admin user for web dashboard
- ⚠️ **No Access Control**: TCP server (port 8000) has no authentication
- ⚠️ **WiFi Credentials**: Stored in plain text on the ESP32

### Threat Model

**In Scope:**
- Vulnerabilities in OpenLux firmware code
- Authentication/authorization bypass
- Buffer overflows or memory corruption
- Code injection vulnerabilities
- Denial of service attacks

**Out of Scope:**
- Physical attacks requiring device access
- Attacks requiring router/network access
- Social engineering
- Attacks on Home Assistant or third-party integrations
- Issues in ESP32 core libraries (report to Espressif)

## 🔐 Encryption & Data Storage

### Credentials Storage

- WiFi credentials are stored in ESP32 NVS (Non-Volatile Storage)
- OTA password is compiled into firmware
- No cloud storage or external transmission of credentials

### Network Communications

- **TCP Port 8000**: Unencrypted Modbus-like protocol
- **Web Dashboard**: Unencrypted HTTP with basic auth
- **Telnet**: Unencrypted logging (port 23)

**Recommendation**: Use network-level encryption (VPN/IPSec) if transmitting over untrusted networks.

## 📦 Dependency Security

OpenLux uses the following dependencies:

- `ESP32Async/AsyncTCP` - Async TCP library
- `tzapu/WiFiManager` - WiFi configuration portal
- ESP32 Arduino Core libraries

We monitor these for security updates. If you find a vulnerable dependency:

1. Check if an update is available
2. Test the updated version
3. Submit a PR with the dependency update

## 🚨 Security Checklist

Before deploying OpenLux:

- [ ] Changed default web dashboard password
- [ ] Changed OTA password from default
- [ ] Enabled firewall on router
- [ ] OpenLux not exposed to internet
- [ ] Using WPA2/WPA3 WiFi encryption
- [ ] Reviewed and understood inverter write commands
- [ ] Regular firmware updates installed
- [ ] Monitoring logs for anomalies

## 📢 Disclosure Policy

When a security vulnerability is fixed:

1. A security advisory will be published
2. The fix will be released in a new version
3. CHANGELOG will document the security fix
4. Credit will be given to the reporter (if desired)

## ⚠️ Disclaimer

OpenLux is provided "AS IS" without warranties. Users assume all risks associated with:

- Electrical equipment safety
- Solar inverter operation
- Network security
- Data integrity

**Use at your own risk. The developers are not liable for any damages.**

## 📞 Contact

For security issues, please use responsible disclosure:

- **Security Advisories**: [GitHub Security](https://github.com/f-garofalo/openlux/security)
- **General Security Questions**: Open a [Discussion](https://github.com/f-garofalo/openlux/discussions)

---

**Last Updated**: December 11, 2024
