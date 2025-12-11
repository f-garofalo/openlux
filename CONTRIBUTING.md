# Contributing to OpenLux

Thank you for your interest in contributing to OpenLux! This document provides guidelines for contributing to the project.

## 🎯 How Can I Contribute?

### Reporting Bugs

Before creating bug reports, please check the [existing issues](https://github.com/f-garofalo/openlux/issues) to avoid duplicates.

**When filing a bug report, include:**
- A clear and descriptive title
- Steps to reproduce the issue
- Expected vs. actual behavior
- Hardware details (ESP32 board, RS485 module, inverter model)
- Firmware version and build logs
- Serial/Telnet logs showing the issue

### Suggesting Enhancements

Enhancement suggestions are welcome! Please:
- Use a clear and descriptive title
- Provide a detailed description of the proposed feature
- Explain why this enhancement would be useful
- Include examples of how it would work

### Code Contributions

1. **Fork the repository** and create a branch from `main`
2. **Make your changes** following the coding standards below
3. **Test thoroughly** on actual hardware if possible
4. **Commit your changes** with clear, descriptive messages
5. **Submit a pull request** with a comprehensive description

## 📝 Coding Standards

### C++ Style

- Use **clang-format** for code formatting (`.clang-format` provided)
- Follow existing code style and naming conventions
- Add comments for complex logic
- Use descriptive variable and function names

### Code Organization

```cpp
/**
 * @file filename.cpp
 * @brief Brief description
 *
 * @license GPL-3.0
 * @author OpenLux Contributors
 */

#include "header.h"
// ... rest of code
```

### Documentation

- Add Doxygen-style comments for public functions
- Update README.md if adding new features
- Update CHANGELOG.md following [Keep a Changelog](https://keepachangelog.com/)
- Include inline comments for non-obvious code

### Commit Messages

Follow [Conventional Commits](https://www.conventionalcommits.org/):

```
feat: add support for ESP32-S3
fix: resolve RS485 timeout issue
docs: update hardware wiring guide
refactor: simplify TCP protocol parser
```

## 🧪 Testing

### Before Submitting

- [ ] Code compiles without errors or warnings
- [ ] Tested on actual ESP32 hardware (if hardware-related)
- [ ] No secrets or credentials committed
- [ ] Documentation updated as needed
- [ ] CHANGELOG.md updated

### Hardware Testing

If possible, test on:
- Different ESP32 variants (ESP32, ESP32-S3, ESP32-C3)
- Different inverter models
- Both WiFi and Ethernet (if applicable)

## 📋 Pull Request Process

1. **Update documentation** for any new features
2. **Add yourself** to contributors if this is your first contribution
3. **Link related issues** in the PR description
4. **Wait for review** - maintainers will review and provide feedback
5. **Address feedback** and update PR as needed

## 🌟 Areas for Contribution

We especially welcome help with:

- **Testing** on different inverter models
- **Documentation** improvements and translations
- **Bug fixes** and stability improvements
- **Hardware compatibility** testing and guides
- **Protocol analysis** and documentation
- **Home Assistant integration** enhancements

## 🤝 Code of Conduct

### Our Pledge

We pledge to make participation in this project a harassment-free experience for everyone, regardless of age, body size, disability, ethnicity, gender identity and expression, level of experience, nationality, personal appearance, race, religion, or sexual identity and orientation.

### Our Standards

**Positive behavior:**
- Using welcoming and inclusive language
- Being respectful of differing viewpoints
- Gracefully accepting constructive criticism
- Focusing on what is best for the community
- Showing empathy towards other community members

**Unacceptable behavior:**
- Harassment, trolling, or insulting/derogatory comments
- Public or private harassment
- Publishing others' private information without permission
- Any conduct that could reasonably be considered inappropriate

### Enforcement

Violations may result in temporary or permanent ban from the project. Report issues to the project maintainers.

## 📄 License

By contributing to OpenLux, you agree that your contributions will be licensed under the GPL-3.0 License.

## ❓ Questions?

Feel free to:
- Open a [discussion](https://github.com/f-garofalo/openlux/discussions)
- Ask in [issues](https://github.com/f-garofalo/openlux/issues)
- Contact the maintainers

---

**Thank you for contributing to OpenLux! 🎉**
