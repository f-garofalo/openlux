# Release Checklist for OpenLux

Use this checklist before creating a new release.

## Pre-Release

- [ ] All tests pass locally
- [ ] Code compiles without warnings
- [ ] CHANGELOG.md updated with all changes
- [ ] Version number updated in `src/config.h` (FIRMWARE_VERSION)
- [ ] Version number updated in README.md badges
- [ ] Documentation updated (README.md, docs/)
- [ ] All TODO/FIXME comments addressed or documented
- [ ] secrets.h.example is up to date
- [ ] No secrets or credentials in code

## Code Quality

- [ ] Code formatted with clang-format
- [ ] No compiler warnings
- [ ] No dead code or commented-out blocks
- [ ] All public functions have documentation
- [ ] Complex logic has explanatory comments

## Testing

- [ ] Tested on ESP32 hardware
- [ ] WiFi connection works
- [ ] Ethernet works (if applicable)
- [ ] RS485 communication verified
- [ ] TCP server accepts connections
- [ ] Web dashboard accessible
- [ ] OTA updates work
- [ ] Home Assistant integration tested
- [ ] Read operations work
- [ ] Write operations work (if tested)
- [ ] Tested reconnection scenarios
- [ ] No memory leaks after 24h runtime

## Documentation

- [ ] README.md accurate and complete
- [ ] Installation instructions clear
- [ ] Hardware guide up to date
- [ ] Configuration examples correct
- [ ] CHANGELOG.md follows Keep a Changelog format
- [ ] All links work (no 404s)

## GitHub

- [ ] All issues referenced in CHANGELOG
- [ ] PR descriptions complete
- [ ] Branch is up to date with main
- [ ] CI/CD pipeline passes
- [ ] No merge conflicts

## Release

- [ ] Create Git tag: `git tag -a v1.x.x -m "Release v1.x.x"`
- [ ] Push tag: `git push origin v1.x.x`
- [ ] Create GitHub Release with changelog excerpt
- [ ] Attach compiled binaries to release
- [ ] Announce in discussions/README

## Post-Release

- [ ] Monitor issues for bug reports
- [ ] Update documentation if needed
- [ ] Plan next release features
- [ ] Thank contributors

---

**Version Format**: Semantic Versioning (MAJOR.MINOR.PATCH)
- MAJOR: Breaking changes
- MINOR: New features (backward compatible)
- PATCH: Bug fixes
