# Claude_usage

**[⚡ Flash it to your CYD now](https://grey-lancaster.github.io/Claude_usage/flash.html)** -
one click from your browser, no toolchain to install. Just plug in the
board over USB and go.

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
this project polls conservatively (5 minutes).

## Where does the cookie live?

A `claude.ai` session cookie is equivalent to being logged into your
account - not just "read your usage," but full account session access
(chat history, starting new chats/burning usage, settings, connectors)
until it expires or you kill the session. The board ([`firmware-direct`](firmware-direct))
holds the cookie itself and talks to claude.ai directly - fully
standalone, no PC required. It's stored encrypted (AES-256-GCM, key
derived from a passphrase you set at `http://claudeusage.local/`, never
written to flash) - a flash dump yields ciphertext, not the cookie.
That's real protection, not full-disk flash encryption (which this
project deliberately doesn't attempt - see [`firmware-direct`'s
README](firmware-direct) for why), and it's not a substitute for good
judgment: if the device is ever lost or stolen, log out the session or
change your claude.ai password regardless of the encryption. It also
boots locked - a passphrase is required to decrypt the cookie into RAM
on every power cycle, so a device that's simply been unplugged and taken
never has the cookie available at all.

Uses the display/dashboard code in [`common/`](common) and the
touchscreen Wi-Fi provisioning from
[TouchWifiProvisioner](https://github.com/Grey-Lancaster/TouchWifiProvisioner).

## Hardware

- **Cheap Yellow Display** (ESP32-2432S028R), ST7789 panel, resistive
  XPT2046 touch - same board TouchWifiProvisioner's CYD examples target.
  Battle-tested throughout this project's development.
- **Elecrow CrowPanel Advance 7.0" HMI** (ESP32-S3, 800x480 RGB IPS
  panel, GT911 capacitive touch) - `firmware-direct` only (the
  `crowpanel7` PlatformIO environment). Verified on real hardware - see
  [`firmware-direct`'s README](firmware-direct#supported-boards).
- **Wemos/LOLIN D1 mini (MH-ET LIVE MiniKit ESP32 clone) + TFT 2.4"
  Touch Shield V1.0** (ILI9341 320x240 SPI, XPT2046 touch) -
  `firmware-direct` only (the `d1mini` PlatformIO environment). Verified
  on real hardware, display and touch both working - see
  [`firmware-direct`'s README](firmware-direct#supported-boards).

Pin mapping lives in each variant's `platformio.ini`.

## Build

Most people don't need this section - use the
[flash page](https://grey-lancaster.github.io/Claude_usage/flash.html) at
the top of this README instead. This is for building from source (making
your own changes, or just preferring PlatformIO over a browser flasher).

```bash
git clone https://github.com/Grey-Lancaster/Claude_usage.git
cd Claude_usage/firmware-direct
pio run -e cyd -t upload   # or -e crowpanel7 / -e d1mini
```

See [`firmware-direct`'s README](firmware-direct) for first-time setup
(entering your org ID/cookie).

## Security

Summarized above under [Where does the cookie live?](#where-does-the-cookie-live);
[`firmware-direct`'s own README](firmware-direct) repeats the parts
specific to it. The short version: this endpoint requires a real account
session, and how carefully you handle that session's cookie is the main
thing to think about before deploying it.

## License

[MIT](LICENSE)
