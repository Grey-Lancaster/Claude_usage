# firmware-direct

Talks straight to `claude.ai` over HTTPS. The session cookie is stored
**encrypted at rest** (AES-256-GCM, key derived via PBKDF2-HMAC-SHA256
from a passphrase you choose) rather than in plaintext - see
[`ClaudeConfig.h`](src/ClaudeConfig.h) for exactly how. Nothing else needs
to be running - no PC, no relay.

## Supported boards

Three `platformio.ini` environments, all building the exact same app logic
(`AppLogic.h/.cpp`, `ClaudeConfig`, `ClaudeSetupServer`,
`ClaudeUsageClient`, and the shared `../common` dashboard) against
different board-bring-up files:

- **`cyd`** - Cheap Yellow Display (ESP32-2432S028R). `src/main.cpp`.
  Battle-tested on real hardware throughout this project's development.
- **`crowpanel7`** - Elecrow CrowPanel Advance 7.0" HMI (ESP32-S3,
  800x480 RGB IPS panel, GT911 capacitive touch). `src/main_crowpanel7.cpp`
  + `src/LovyanGFX_Driver_CrowPanel7.h`. Verified on real hardware - the
  config (partition table, PSRAM toolchain fork, LovyanGFX panel timings)
  is copied from TouchWifiProvisioner's `CrowPanel7_RollingClock` example.
- **`d1mini`** - Wemos/LOLIN D1 mini (MH-ET LIVE MiniKit ESP32 clone) +
  TFT 2.4" Touch Shield V1.0 (ILI9341 320x240 SPI, XPT2046 touch).
  `src/main_d1mini.cpp`. Verified on real hardware - display and touch
  both confirmed working; touch's IRQ pin (an unverified guess from a
  never-live ESPHome config block) turned out to be wrong and was
  dropped entirely, matching Wemos's own reference sketch for this
  shield, which doesn't wire an IRQ pin either. `TOUCH_X_MIN/MAX` and
  `TOUCH_Y_MIN/MAX` are real measured calibration values, not a
  placeholder.

All boards currently register the same `claudeusage`/`ClaudeUsage`
mDNS/OTA hostname - fine with only one on your network at a time, but
running more than one simultaneously would collide.

## How setup works

1. On first boot, the dashboard shows "Not configured" with a URL:
   **`http://claudeusage.local/`**. Open that in a browser on the same
   network.
2. The page walks you through finding your `ORG_ID` and session cookie
   (DevTools > Network on claude.ai, same as documented in
   [`../firmware-relay`](../firmware-relay)'s setup) and asks you to pick
   a **passphrase**.
3. That passphrase is used once to derive the encryption key and is then
   discarded - **it is never written to flash.** Only the encrypted
   cookie is stored.

## Why the dashboard is "locked" after every reboot

Because the passphrase isn't stored anywhere on the device, the device
can't decrypt the cookie on its own after a power cycle - it needs you to
type the passphrase again at `http://claudeusage.local/`. This is
deliberate: it's what makes the encryption real protection rather than
obfuscation. If the device auto-unlocked itself after a reboot, it would
need to store something on-device that lets it do that, and that
something would be exactly as extractable as the cookie itself - which
defeats the point.

## What this does and doesn't protect against

- **Does:** a flash dump (e.g. `esptool.py read_flash` on a stolen board)
  yields ciphertext, not your cookie. Recovering the real value requires
  the passphrase, which never touched the flash.
- **Doesn't:** this isn't a substitute for physical security or a strong
  passphrase - it raises the cost of extraction, it doesn't make it
  impossible. **If the device is ever lost, stolen, or you suspect the
  cookie was exposed, log out that session (or change your claude.ai
  password) rather than relying on the encryption alone.** Treat "device
  compromised" the same as "cookie compromised."
- The one-time act of typing the cookie/passphrase into the setup page
  crosses your LAN as plain HTTP (no TLS - a self-signed cert on an mDNS
  device isn't worth the complexity for an admin page only ever used on
  a trusted network). Fine at home; don't expose this page beyond your
  LAN.

## Build

```bash
cd firmware-direct
pio run -e cyd -t upload   # or -e crowpanel7
pio device monitor
```

## Updating firmware over Wi-Fi

Two ways, both need the device already running a build with this feature
(i.e. one USB flash to get here first):

- **Browser upload (recommended on Windows)**: visit
  `http://claudeusage.local/update`, sign in with username `admin` and
  the OTA password (see below), pick the `firmware.bin` from
  `pio run` (not the merged `- Bootable` image - that includes the
  bootloader/partitions, which this route doesn't touch), and upload.
  Reuses the same web server already running for setup, so it isn't
  affected by the Windows Firewall issue below.
- **`pio run -e cyd_ota -t upload`** (or `crowpanel7_ota` for that
  board) - ArduinoOTA/espota protocol: if
  nothing happens past "Authenticating...OK", Windows Firewall is almost
  certainly silently blocking the callback connection `espota` needs.
  Run this once in an **elevated** PowerShell:
  ```powershell
  New-NetFirewallRule -DisplayName "PlatformIO OTA (python)" -Direction Inbound -Program "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" -Action Allow -Protocol TCP
  ```

Either way, **change the default OTA password first** - it's `changeme`
via `-DOTA_PASSWORD` in `platformio.ini`'s `[env:cyd_ota]` section
(`build_flags`) and `upload_flags`' `--auth=`. Left as-is, anyone on your
LAN who finds the device can push arbitrary firmware to it.

## Notes

- Polls every 5 minutes (`POLL_INTERVAL_MS` in `src/AppLogic.cpp`) -
  deliberately conservative since this hits an undocumented internal
  endpoint, not a public rate-limited API meant for polling.
- TLS to claude.ai is validated against Let's Encrypt's ISRG Root X1
  (`src/RootCA.h`), not skipped via `setInsecure()` - this connection
  carries your session cookie, so an unvalidated TLS connection would be
  a real MITM exposure, not just a hygiene nit.
- The gear icon's "Reset Claude Account" wipes the stored ciphertext
  (equivalent to hitting Reset on the setup page) - use it before giving
  the device away or repurposing it.
