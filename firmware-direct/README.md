# firmware-direct

The CYD talks straight to `claude.ai` over HTTPS and holds your session
cookie on-device (in NVS via `Preferences`). Nothing else needs to be
running - no PC, no relay.

**Read the security note in the [root README](../README.md#security) before
using this variant on a device that leaves your desk.** In short: anyone
who gets the board and a USB cable can dump its flash and recover the
cookie, which is equivalent to being logged into your account. If that's
not an acceptable risk for where this device lives, use
[`../firmware-relay`](../firmware-relay) instead.

## Build

```bash
cd firmware-direct
pio run -t upload
pio device monitor
```

## First-time setup

1. On first boot (or after tapping the gear icon > "Reconfigure Claude
   Account"), the device prints a prompt over Serial and waits.
2. In a browser logged into `claude.ai`, open DevTools > Network, go to
   Settings > Usage, and reload/click the refresh icon next to "Last
   updated". Find the request to:
   ```
   GET https://claude.ai/api/organizations/<ORG_ID>/usage
   ```
3. Paste the `ORG_ID` (the UUID from that URL) into the serial prompt,
   press Enter.
4. Paste the full `cookie` request header value from that same request,
   press Enter. The device saves both to NVS and reboots.

The cookie is a live session token - it will eventually expire or rotate
(e.g. if you log out elsewhere, or after some period of inactivity), at
which point the dashboard will show "Cookie expired - reconfigure" and
you repeat step 2-4.

## Notes

- Polls every 5 minutes (`POLL_INTERVAL_MS` in `src/main.cpp`) -
  deliberately conservative since this hits an undocumented internal
  endpoint, not a public rate-limited API meant for polling.
- TLS is validated against Let's Encrypt's ISRG Root X1 (`src/RootCA.h`),
  not skipped via `setInsecure()` - this connection carries your session
  cookie, so an unvalidated TLS connection would be a real MITM exposure,
  not just a hygiene nit.
