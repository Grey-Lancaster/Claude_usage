# Changelog

All notable changes to this project are documented here. Format loosely
follows [Keep a Changelog](https://keepachangelog.com/).

## [0.9.24] - 2026-08-03

### Fixed
- The countdown label ("Next update in M:SS") was wide enough to wrap
  onto two lines inside its fixed-width box on CYD/D1 mini, once the
  status row actually became visible there. Shortened to "Next: M:SS" -
  wording only, no layout/alignment change, so CrowPanel7's 3-segment
  row is unaffected.

## [0.9.20] - 2026-08-03

### Fixed
- The on-device web setup page's "Upload & flash" button (firmware
  OTA update form) had no disable-on-submit guard, unlike the
  provision/unlock forms which already do this. On a slow upload,
  repeat clicks each fired a new `POST /update` while the first was
  still in flight - concurrent uploads left the device's
  `Update.begin()` in a stuck state and OTA updates failed with
  "Couldn't start (Update.begin() failed)" until a power cycle.
  The button now disables and reads "Uploading..." on the first click.

## [0.9.16] - 2026-08-03

### Fixed
- CYD/D1 mini (320x240): the status row ("Updated" / next-update
  countdown) was rendering entirely below the visible screen - header +
  the 3 meter cards alone already exceeded 240px of height at the
  padding/font sizes those boards shared with CrowPanel7's much taller
  display, and root's non-scrollable container silently clipped the
  rest. Tightened padding, bar height, and the gear button's size on
  the small-display layout path only (CrowPanel7 is untouched) to
  reclaim enough room for the status row to actually show.

## [0.9.12] - 2026-08-03

### Changed
- The mascot dance now runs for a full ~4 seconds (was under a second):
  oscillates several times before settling back to center, instead of a
  single quick shake

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
