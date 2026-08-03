# Changelog

All notable changes to this project are documented here. Format loosely
follows [Keep a Changelog](https://keepachangelog.com/).

## [0.9.8] - 2026-08-02

### Added
- The header mascot now does a little shake each time an update lands

### Fixed
- The mascot animation originally rotated (transform_angle), but that
  flickered instead of rotating smoothly on real hardware - an LVGL 8.x
  rendering quirk with rotating an object that has children (the eyes).
  Switched to a horizontal shake (transform_translate_x), which renders
  correctly since translation doesn't hit the same clipping/layer edge
  cases rotation does.

## [0.9.4] - 2026-08-02

### Added
- `CHANGELOG.md` and a GitHub Actions release workflow - pushing a
  "vX.Y.Z" tag now auto-publishes a GitHub Release from that version's
  changelog section, no manual `gh`/web UI step needed

## [0.9.0] - 2026-08-02

First tagged release - this entry covers the project's full feature set
to date, not just recent changes.

### Added
- Three hardware-verified boards: Cheap Yellow Display (ESP32-2432S028R),
  Elecrow CrowPanel Advance 7" (ESP32-S3), and Wemos/LOLIN D1 mini + TFT
  2.4" Touch Shield (MH-ET LIVE MiniKit ESP32 clone)
- Encrypted-at-rest session cookie storage (AES-256-GCM, PBKDF2-derived
  key) - never written to flash in plaintext, device boots locked every
  power cycle
- On-device web setup page (`http://claudeusage.local/`, with a raw-IP
  fallback) for provisioning, timezone, firmware OTA updates, and a
  screenshot-capture endpoint
- One-click browser flashing page (esp-web-tools) for all three boards
- Live status row: "Updated", a countdown to the next poll, and (screen
  space permitting) uptime
- A small mascot wiggle animation on every successful update
- Retry logic for transient Cloudflare-flagged 401/403 responses before
  reporting a real "cookie expired" error
- Illustrated cookie-finding guide and cookie-refresh companion script

### Removed
- `firmware-relay` and `relay-server` (a variant where the cookie never
  touches the device) - built early on but never tested on real
  hardware; removed since `firmware-direct`'s encrypted, boots-locked
  storage already covers the threat model it existed for
