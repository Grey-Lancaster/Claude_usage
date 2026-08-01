# firmware-direct

The CYD talks straight to `claude.ai` over HTTPS. The session cookie is
stored **encrypted at rest** (AES-256-GCM, key derived via PBKDF2-HMAC-SHA256
from a passphrase you choose) rather than in plaintext - see
[`ClaudeConfig.h`](src/ClaudeConfig.h) for exactly how. Nothing else needs
to be running - no PC, no relay.

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
pio run -t upload
pio device monitor
```

## Notes

- Polls every 5 minutes (`POLL_INTERVAL_MS` in `src/main.cpp`) -
  deliberately conservative since this hits an undocumented internal
  endpoint, not a public rate-limited API meant for polling.
- TLS to claude.ai is validated against Let's Encrypt's ISRG Root X1
  (`src/RootCA.h`), not skipped via `setInsecure()` - this connection
  carries your session cookie, so an unvalidated TLS connection would be
  a real MITM exposure, not just a hygiene nit.
- The gear icon's "Reset Claude Account" wipes the stored ciphertext
  (equivalent to hitting Reset on the setup page) - use it before giving
  the device away or repurposing it.
