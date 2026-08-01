# firmware-relay

The CYD never sees your session cookie - it polls
[`../relay-server`](../relay-server) (a small script on your PC) over
plain HTTP on your LAN. The relay is the only thing that ever holds the
cookie.

Trade-off: the relay script has to be running on your PC whenever you
want live data. If you'd rather the device be fully standalone (no PC
required), use [`../firmware-direct`](../firmware-direct) instead - see
the root README's [Security](../README.md#security) section for why that
variant carries different risk.

## Build

```bash
cd firmware-relay
pio run -t upload
pio device monitor
```

## First-time setup

1. Get [`../relay-server`](../relay-server) running on your PC first (see
   its README) and note the machine's LAN IP and port (default `8787`).
2. On first boot, or via the gear icon > "Change Relay Address", the
   device shows an on-screen keyboard - type `host:port`
   (e.g. `192.168.1.50:8787`) and tap Save.

## Notes

- Polls every 60 seconds (`POLL_INTERVAL_MS` in `src/main.cpp`) - cheap
  since it's a local-network hop to your own relay, not the actual
  claude.ai endpoint (the relay polls that separately, on its own
  schedule).
- No TLS, no auth on this hop - anything on your LAN that can reach the
  relay's port can read your usage numbers (not your cookie, which never
  leaves the relay). Fine for a home network; don't expose the relay's
  port beyond your LAN.
