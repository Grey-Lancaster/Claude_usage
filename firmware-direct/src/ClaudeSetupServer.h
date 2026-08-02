// Tiny on-device web UI at http://claudeusage.local/ for provisioning
// and unlocking the encrypted cookie (see ClaudeConfig.h). A browser
// text field handles a 100+ char cookie far better than either a serial
// terminal or the on-screen keyboard.
//
// Plain HTTP, no auth beyond the passphrase itself - anyone on your LAN
// can load the page, but without the right passphrase all they can do is
// guess (rate-limited only by PBKDF2's ~20k-round cost per attempt) or
// wipe the stored config via Reset, never read the cookie. The one-time
// act of typing the cookie/passphrase into this page does cross your LAN
// as plaintext HTTP, same caveat as any admin page on a device this
// size - fine on a trusted home network, not something to expose beyond
// it.
#pragma once

#include <Arduino.h>
#include <functional>

class WebServer;

namespace ClaudeSetupServer {

// Starts the mDNS responder ("claudeusage.local") and the HTTP server.
// Call once after Wi-Fi connects.
void begin();

// Call every loop() iteration.
void handleClient();

// Fires after a successful provision or unlock (i.e. whenever the cookie
// just became available in RAM) - the host app hooks this to trigger an
// immediate poll instead of waiting for the next scheduled one.
void setOnUnlocked(std::function<void()> cb);

// Fires after a successful timezone save (from either the initial setup
// form or the standalone "Save timezone" form) with the new IANA
// location string.
void setOnTimezoneChanged(std::function<void(const String &)> cb);

// Records the outcome of the most recent usage fetch, so the "Unlocked"
// page can show what's actually happening instead of a hardcoded "polling
// normally" that stayed put even while every fetch was failing (e.g. an
// expired cookie) - the device's own screen already surfaced that
// correctly, this page didn't. Call after every fetch attempt, success or
// failure; `detail` is ignored when `ok` is true.
void setLastFetchStatus(bool ok, const String &detail);

// Registers the board-specific handler for GET /screenshot.bmp - each
// board's main file owns its own display/LVGL objects, so it's the one
// that knows how to actually capture a frame (see
// common/ScreenshotCapture.h). Call any time before the request could
// arrive; unset means the route responds 404.
void setScreenshotHandler(std::function<void(WebServer &)> handler);

} // namespace ClaudeSetupServer
