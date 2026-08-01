# Claude_usage

A [Cheap Yellow Display](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display)
(ESP32-2432S028R) dashboard showing your live Claude.ai plan usage: current
session (5-hour) limit, weekly limit, and usage credits - the same numbers
shown in claude.ai's Settings > Usage panel.

Code by Grey and his buddy Claude.

## How this works

There's no public API for these numbers - they're internal to claude.ai's
own UI. This project talks to the same endpoint that UI calls:

```
GET https://claude.ai/api/organizations/<ORG_ID>/usage
```

authenticated with your browser's session cookie, found by watching
DevTools > Network while reloading Settings > Usage - see
[`docs/cookie-guide.md`](docs/cookie-guide.md) for the illustrated,
click-by-click version of that dig. It's **undocumented
and unofficial** - Anthropic could change or remove it without notice,
and this isn't a rate-limited public API meant for polling, which is why
both variants below poll conservatively (5 minutes).

## Two variants, one trade-off: where does the cookie live?

A `claude.ai` session cookie is equivalent to being logged into your
account - not just "read your usage," but full account session access
(chat history, starting new chats/burning usage, settings, connectors)
until it expires or you kill the session. Where you're willing to put
that determines which variant to build:

- **[`firmware-direct`](firmware-direct)** - the CYD holds the cookie
  itself and talks to claude.ai directly. Fully standalone, no PC
  required. The cookie is stored encrypted (AES-256-GCM, key derived from
  a passphrase you set at `http://claudeusage.local/`, never written to
  flash) - a flash dump yields ciphertext, not the cookie. That's real
  protection, not full-disk flash encryption (which this project
  deliberately doesn't attempt - see that variant's README for why), and
  it's not a substitute for good judgment: if the device is ever lost or
  stolen, log out the session or change your claude.ai password regardless
  of the encryption.
- **[`firmware-relay`](firmware-relay)** + **[`relay-server`](relay-server)** -
  the cookie lives only in a small script on your PC
  ([`relay-server`](relay-server)), which re-serves a simplified summary
  over plain HTTP on your LAN. The CYD never touches the cookie at all.
  Trade-off: that PC has to be running for the display to have live data.

Both share the display/dashboard code in [`common/`](common) and the
touchscreen Wi-Fi provisioning from
[TouchWifiProvisioner](https://github.com/Grey-Lancaster/TouchWifiProvisioner).

## Hardware

Cheap Yellow Display (ESP32-2432S028R), ST7789 panel, resistive XPT2046
touch - same board TouchWifiProvisioner's CYD examples target. Pin
mapping lives in each variant's `platformio.ini`.

## Build

Each variant is self-contained - `cd` into it and `pio run`, no manual
file copying:

```bash
git clone https://github.com/Grey-Lancaster/Claude_usage.git
cd Claude_usage/firmware-direct   # or firmware-relay
pio run -t upload
```

See that variant's README for first-time setup (entering your org ID /
cookie, or relay address).

## Security

Summarized above under [Two variants](#two-variants-one-trade-off-where-does-the-cookie-live);
each variant's own README repeats the parts specific to it. The short
version: this endpoint requires a real account session, and how carefully
you handle that session's cookie is the main thing to think about before
deploying either variant.

## License

[MIT](LICENSE)
